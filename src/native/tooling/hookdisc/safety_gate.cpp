#include "tooling/hookdisc/safety_gate.h"

#include "injector/safety/safety_preflight.h"
#include "versioning/game_fingerprint.h"

namespace vrclient::tooling::hookdisc {

injector::process::TargetDescriptor modeledTargetFromConfig(
    const versioning::GameFingerprintConfig& config,
    const std::vector<SourceFlowMapping>& source_priority,
    const std::string& identity_evidence_source) {
  injector::process::TargetDescriptor target;

  // First allowed-source token in priority order that the config declares wins.
  for (const auto& mapping : source_priority) {
    bool present = false;
    for (const std::string& source : config.support_policy.allowed_sources) {
      if (source == mapping.first) {
        present = true;
        break;
      }
    }
    if (present) {
      target.flow = mapping.second;
      break;
    }
  }

  if (!config.executable_names.empty()) {
    target.executable_path = config.executable_names.front();
  }
  target.game_id_hint = config.game_id;
  target.architecture = config.support_policy.target_architecture;

  // Identity evidence from the first supported build that carries file hashes.
  for (const versioning::BuildFingerprint& build : config.builds) {
    if (build.supported && !build.file_hashes.empty()) {
      target.executable_sha256 = build.file_hashes.front().value;
      target.build_id_hint = build.build_id;
      if (!build.product_versions.empty()) {
        target.product_version = build.product_versions.front();
      }
      break;
    }
  }

  // Trusted ONLY because callers use this for a dry-run model or the controlled
  // smoke host's self-gate (see header). A live attach must supply real observed
  // evidence from process discovery instead of reusing this modeled descriptor.
  target.identity_evidence_trusted = true;
  target.identity_evidence_source = identity_evidence_source;
  return target;
}

HookSurfaceGateOutcome runHookSurfaceSafetyGate(
    const std::filesystem::path& game_config_path,
    const injector::process::TargetDescriptor& target,
    const HookSurfaceGateOptions& options,
    diagnostics::AsyncLogger* logger) {
  HookSurfaceGateOutcome outcome;
  outcome.dry_run = options.dry_run_model;

  const auto loaded = versioning::loadGameFingerprintConfig(game_config_path);
  if (!loaded.loaded) {
    outcome.reason_code = "fingerprint_config_invalid";
    outcome.message = loaded.message;
    return outcome;
  }
  outcome.game_id = loaded.config.game_id;
  outcome.controlled_smoke_target = loaded.config.controlled_smoke_target;

  // Autonomy invariant (CODE-ENFORCED, not config-derived): only the controlled
  // smoke host may be approved without an explicit human authorization. Any other
  // target — even one whose config happens to read offline_only + known_safe —
  // refuses here unless a human supplied private/modded confirmation or set
  // operator_authorized_live_target. This is the hard floor the live increment
  // inherits; it cannot be loosened by editing a config's online_risk value.
  const bool human_authorized = options.confirm_private_modded_session ||
                                options.confirm_mod_compatibility ||
                                options.operator_authorized_live_target;
  if (!loaded.config.controlled_smoke_target && !human_authorized) {
    outcome.reason_code = "not_controlled_smoke_target";
    outcome.message =
        "autonomous gate only auto-approves the controlled smoke host; a live or "
        "commercial target requires explicit human authorization";
    return outcome;
  }

  const auto identity = versioning::detectVersion(target, loaded.config, logger);
  outcome.build_id = identity.build_id;
  if (identity.status != versioning::DetectionStatus::KnownSupported) {
    outcome.reason_code = identity.reason_code;
    outcome.message = "identity not KnownSupported";
    return outcome;
  }

  // Observed runtime-hash evidence. The configured expected hash may stand in for
  // the observed hash ONLY for a dry-run model of the controlled smoke host. For
  // a live run (dry_run_model == false) or any non-smoke target, an empty
  // observed hash is left empty so runSafetyPreflight refuses with
  // `runtime_hash_missing` — never a self-satisfied build-hash gate (no fail-open
  // seam for a future live-attach caller that omits the observed hash).
  std::string observed_sha = options.observed_runtime_sha256;
  if (observed_sha.empty() && options.dry_run_model &&
      loaded.config.controlled_smoke_target) {
    observed_sha = identity.runtime.sha256;
  }

  auto request = injector::safety::makePreflightRequest(
      target,
      identity,
      loaded.config.controlled_smoke_target,
      options.runtime_binary_exists,
      observed_sha);

  // Multiplayer posture is opt-in only. The autonomous harness never sets these,
  // so a private/modded co-op policy refuses unless a human authorizes it.
  if (options.confirm_private_modded_session) {
    request.multiplayer_session_scope = "private_modded";
  }
  request.multiplayer_mod_compatibility_confirmed = options.confirm_mod_compatibility;

  const auto verdict = injector::safety::runSafetyPreflight(request, logger);
  outcome.reason_code = verdict.reason_code;
  outcome.message = verdict.message;
  outcome.approved = verdict.verdict == injector::safety::SafetyVerdict::Approved;
  return outcome;
}

}  // namespace vrclient::tooling::hookdisc

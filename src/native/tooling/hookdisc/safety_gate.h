#pragma once

// Bolt-on Phase B2 (RE-05): the hard safety gate for the hook-discovery harness.
//
// Before ANY in-process observation of a target that is not the current process,
// the harness MUST pass the existing Phase 3 safety preflight against the
// target's fingerprint config. There is no override and no force flag — by
// design (no evasion). This wraps loadGameFingerprintConfig -> detectVersion ->
// makePreflightRequest -> runSafetyPreflight and returns a single verdict.
//
// Autonomy rule (CODE-ENFORCED): the harness NEVER auto-approves any target
// other than the controlled smoke host. A non-controlled-smoke target (e.g.
// R.E.P.O.) refuses with reason `not_controlled_smoke_target` unless a human
// explicitly authorizes it via HookSurfaceGateOptions (confirming private/modded
// posture and/or operator_authorized_live_target). This invariant no longer
// depends on a config's online_risk value alone.
//
// Dry-run vs live: a modeled (dry-run) call may stand in the configured expected
// hash for the observed runtime hash, but ONLY for the controlled smoke host.
// A live call (dry_run_model == false) and any non-smoke target must supply a
// REAL observed_runtime_sha256 — otherwise the build-hash check refuses with
// `runtime_hash_missing` instead of silently self-satisfying (no fail-open seam
// for a future live-attach caller that forgets to populate the observed hash).

#include "diagnostics/logging/diagnostic_logger.h"
#include "injector/process/process_discovery.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace vrclient {
namespace versioning {
struct GameFingerprintConfig;
}  // namespace versioning
}  // namespace vrclient

namespace vrclient::tooling::hookdisc {

// One ordered (allowed-source token -> discovery flow) candidate. The shared
// descriptor builder picks the FIRST candidate whose token appears in the
// config's allowed_sources, mirroring the original per-call-site if/else chains.
using SourceFlowMapping =
    std::pair<std::string, injector::process::DiscoveryFlow>;

// Build a TargetDescriptor from a fingerprint config's configured identity
// evidence. Single home for the config->descriptor identity logic that the CLI
// (modeled dry run) and the obs host (cooperative in-target self-gate) both need,
// so a future allowed_sources value or evidence-field change is made ONCE.
//
// `source_priority` is the ordered set of source-token -> flow candidates to try
// (first match wins); `identity_evidence_source` is the provenance label written
// onto the descriptor. The flow defaults to DiscoveryFlow::DirectLaunch when no
// candidate token matches (the TargetDescriptor default). The descriptor is
// marked identity_evidence_trusted = true: callers MUST only treat it as trusted
// in a dry-run model or for the controlled smoke host's self-gate — a live attack
// to a real external process must supply REAL observed evidence instead.
injector::process::TargetDescriptor modeledTargetFromConfig(
    const versioning::GameFingerprintConfig& config,
    const std::vector<SourceFlowMapping>& source_priority,
    const std::string& identity_evidence_source);

struct HookSurfaceGateOptions {
  // Observed runtime-binary evidence. When observed_runtime_sha256 is empty the
  // gate substitutes the configured expected hash ONLY for a dry-run model of the
  // controlled smoke host (dry_run_model && controlled_smoke_target). For a live
  // target or any non-smoke target an empty observed hash refuses
  // (`runtime_hash_missing`) — the build-hash gate never self-satisfies.
  bool runtime_binary_exists = true;
  std::string observed_runtime_sha256;

  // Multiplayer posture. The autonomous CLI leaves these at their safe defaults
  // (offline / unconfirmed), which forces a commercial target to refuse. A human
  // operator may set them to authorize a private/modded co-op validation.
  bool confirm_private_modded_session = false;
  bool confirm_mod_compatibility = false;

  // Explicit human authorization for a non-controlled-smoke (live/commercial)
  // target. The autonomous CLI never sets this; without it (and without a
  // multiplayer confirmation) only the controlled smoke host is auto-approved.
  bool operator_authorized_live_target = false;

  // Marks this as a modeled, no-real-process dry run (the CLI `inspect <slug>`
  // path). A modeled approval is reported with dry_run == true so it can never be
  // mistaken for, or grow into, a live approval. Live attach sets this false.
  bool dry_run_model = false;
};

struct HookSurfaceGateOutcome {
  bool approved = false;
  std::string reason_code;
  std::string message;
  std::string game_id;
  std::string build_id;
  bool controlled_smoke_target = false;
  // True when the verdict came from a modeled dry run (no real process inspected).
  bool dry_run = false;
};

// Run the full identity + safety preflight chain for a candidate target. Returns
// approved == true only on SafetyVerdict::Approved.
HookSurfaceGateOutcome runHookSurfaceSafetyGate(
    const std::filesystem::path& game_config_path,
    const injector::process::TargetDescriptor& target,
    const HookSurfaceGateOptions& options,
    diagnostics::AsyncLogger* logger = nullptr);

}  // namespace vrclient::tooling::hookdisc

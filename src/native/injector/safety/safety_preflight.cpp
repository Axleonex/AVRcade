#include "injector/safety/safety_preflight.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace vrclient::injector::safety {
namespace {

std::string flowPolicyName(process::DiscoveryFlow flow) {
  switch (flow) {
    case process::DiscoveryFlow::DirectLaunch:
      return "direct";
    case process::DiscoveryFlow::AttachRunning:
      return "attach";
    case process::DiscoveryFlow::SteamLaunch:
      return "steam";
    case process::DiscoveryFlow::EpicLaunch:
      return "epic";
    case process::DiscoveryFlow::ManualPath:
      return "manual";
  }
  return "unknown";
}

bool sourceAllowed(const SafetyPreflightRequest& request) {
  const std::string source = flowPolicyName(request.target.flow);
  return std::find(
      request.allowed_sources.begin(),
      request.allowed_sources.end(),
      source) != request.allowed_sources.end();
}

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

bool isSha256Hash(const std::string& value) {
  return value.size() == 64 &&
      std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isxdigit(ch) != 0;
      });
}

bool antiCheatRiskAllowed(const std::string& value) {
  return value == "none" || value == "known_safe";
}

bool onlineRiskAllowed(const std::string& value) {
  return value == "none" || value == "offline_only";
}

bool privateModdedCoopPolicy(const std::string& value) {
  return value == "private_modded_coop";
}

const char* targetScope(bool controlled_smoke_target) {
  return controlled_smoke_target ? "controlled_smoke" : "selected_game";
}

SafetyPreflightResult refusal(
    std::string reason_code,
    std::string message,
    const SafetyPreflightRequest& request) {
  SafetyPreflightResult result;
  result.verdict = SafetyVerdict::Refused;
  result.reason_code = std::move(reason_code);
  result.message = std::move(message);
  result.diagnostics_fields = {
      {"preflight_verdict", safetyVerdictName(result.verdict)},
      {"reason_code", result.reason_code},
      {"game_id", request.identity.game_id},
      {"build_id", request.identity.build_id},
      {"target_scope", targetScope(request.controlled_smoke_target)},
      {"target_source", flowPolicyName(request.target.flow)},
      {"multiplayer_session_scope", request.multiplayer_session_scope},
      {"multiplayer_mod_compatibility_confirmed",
       request.multiplayer_mod_compatibility_confirmed ? "true" : "false"},
  };
  return result;
}

SafetyPreflightResult approval(const SafetyPreflightRequest& request) {
  SafetyPreflightResult result;
  result.verdict = SafetyVerdict::Approved;
  result.reason_code = "approved";
  result.message = "target passed selected-build safety preflight";
  result.diagnostics_fields = {
      {"preflight_verdict", safetyVerdictName(result.verdict)},
      {"reason_code", result.reason_code},
      {"game_id", request.identity.game_id},
      {"build_id", request.identity.build_id},
      {"target_scope", targetScope(request.controlled_smoke_target)},
      {"target_source", flowPolicyName(request.target.flow)},
      {"multiplayer_session_scope", request.multiplayer_session_scope},
      {"multiplayer_mod_compatibility_confirmed",
       request.multiplayer_mod_compatibility_confirmed ? "true" : "false"},
      {"runtime_hash_match", "true"},
  };
  return result;
}

void logPreflight(
    const SafetyPreflightResult& result,
    diagnostics::AsyncLogger* logger) {
  if (logger == nullptr) {
    return;
  }
  logger->log(
      result.verdict == SafetyVerdict::Approved ? diagnostics::Severity::Info
                                                : diagnostics::Severity::Warning,
      "safety_preflight_verdict",
      result.diagnostics_fields);
}

SafetyPreflightResult finish(
    SafetyPreflightResult result,
    diagnostics::AsyncLogger* logger) {
  logPreflight(result, logger);
  return result;
}

}  // namespace

const char* safetyVerdictName(SafetyVerdict verdict) {
  switch (verdict) {
    case SafetyVerdict::Approved:
      return "approved";
    case SafetyVerdict::Refused:
      return "refused";
  }
  return "refused";
}

SafetyPreflightRequest makePreflightRequest(
    const process::TargetDescriptor& target,
    const versioning::VersionDetectionResult& identity,
    bool controlled_smoke_target,
    bool runtime_binary_exists,
    std::string observed_runtime_sha256) {
  SafetyPreflightRequest request;
  request.target = target;
  request.identity = identity;
  request.controlled_smoke_target = controlled_smoke_target;
  request.allowed_sources = identity.support_policy.allowed_sources;
  request.target_architecture = identity.support_policy.target_architecture;
  request.anti_cheat_risk = identity.support_policy.anti_cheat_risk;
  request.online_risk = identity.support_policy.online_risk;
  request.runtime_binary_path = identity.runtime.path;
  request.runtime_binary_exists = runtime_binary_exists;
  request.runtime_binary_sha256 = std::move(observed_runtime_sha256);
  request.expected_runtime_sha256 = identity.runtime.sha256;
  request.runtime_architecture = identity.runtime.architecture;
  return request;
}

SafetyPreflightResult runSafetyPreflight(
    const SafetyPreflightRequest& request,
    diagnostics::AsyncLogger* logger) {
  if (request.identity.status != versioning::DetectionStatus::KnownSupported) {
    return finish(refusal(
        request.identity.status == versioning::DetectionStatus::Ambiguous
            ? "ambiguous_identity"
            : request.identity.status == versioning::DetectionStatus::KnownUnsupported
                ? "unsupported_identity"
                : "unknown_identity",
        "target identity is not a known supported build",
        request), logger);
  }
  if (!sourceAllowed(request)) {
    return finish(refusal(
        "unsafe_target_source",
        "target discovery source is not allowlisted for selected-build preflight",
        request), logger);
  }
  const std::string anti_cheat_risk = lower(request.anti_cheat_risk);
  if (!antiCheatRiskAllowed(anti_cheat_risk) && anti_cheat_risk != "known_risky") {
    return finish(refusal(
        "unknown_anti_cheat_risk",
        "unknown anti-cheat risk blocks selected-build preflight",
        request), logger);
  }
  if (anti_cheat_risk == "known_risky") {
    return finish(refusal(
        "unsafe_anti_cheat_risk",
        "known risky anti-cheat state blocks selected-build preflight",
        request), logger);
  }
  const std::string online_risk = lower(request.online_risk);
  const std::string session_scope = lower(request.multiplayer_session_scope);
  if (request.public_matchmaking_detected) {
    return finish(refusal(
        "public_matchmaking_risk",
        "public matchmaking or unknown public online state blocks selected-build preflight",
        request), logger);
  }
  if (onlineRiskAllowed(online_risk) && session_scope != "offline") {
    return finish(refusal(
        "offline_policy_multiplayer_scope",
        "offline-only target policy cannot approve multiplayer session scope",
        request), logger);
  }
  if (privateModdedCoopPolicy(online_risk)) {
    if (session_scope != "private_modded") {
      return finish(refusal(
          "private_multiplayer_not_confirmed",
          "private modded co-op policy requires confirmed private session scope",
          request), logger);
    }
    if (!request.multiplayer_mod_compatibility_confirmed) {
      return finish(refusal(
          "multiplayer_mod_compatibility_unconfirmed",
          "private modded co-op policy requires explicit compatibility confirmation",
          request), logger);
    }
  } else if (!onlineRiskAllowed(online_risk) && online_risk != "known_online") {
    return finish(refusal(
        "unknown_online_risk",
        "unknown online-mode risk blocks selected-build preflight",
        request), logger);
  }
  if (online_risk == "known_online") {
    return finish(refusal(
        "online_mode_risk",
        "known online-mode risk blocks selected-build preflight",
        request), logger);
  }
  if (request.target.runtime_already_loaded) {
    return finish(refusal(
        "runtime_already_loaded",
        "runtime marker is already present in the target process",
        request), logger);
  }
  if (!request.target_architecture.empty() &&
      request.target.architecture != request.target_architecture) {
    return finish(refusal(
        "architecture_mismatch",
        "target architecture does not match fingerprint policy",
        request), logger);
  }
  if (!request.runtime_architecture.empty() &&
      request.target.architecture != request.runtime_architecture) {
    return finish(refusal(
        "runtime_architecture_mismatch",
        "runtime binary architecture does not match target architecture",
        request), logger);
  }
  if (request.runtime_binary_path.empty() || !request.runtime_binary_exists) {
    return finish(refusal(
        "runtime_binary_missing",
        "runtime binary path is missing or unavailable",
        request), logger);
  }
  if (!isSha256Hash(request.expected_runtime_sha256) ||
      !isSha256Hash(request.runtime_binary_sha256)) {
    return finish(refusal(
        "runtime_hash_missing",
        "runtime binary hash evidence is missing or malformed",
        request), logger);
  }
  if (lower(request.runtime_binary_sha256) != lower(request.expected_runtime_sha256)) {
    return finish(refusal(
        "runtime_hash_mismatch",
        "runtime binary hash does not match fingerprint policy",
        request), logger);
  }
  return finish(approval(request), logger);
}

}  // namespace vrclient::injector::safety

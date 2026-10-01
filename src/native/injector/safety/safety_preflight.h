#pragma once

#include "diagnostics/logging/diagnostic_logger.h"
#include "injector/process/process_discovery.h"
#include "versioning/game_fingerprint.h"

#include <filesystem>
#include <string>
#include <vector>

namespace vrclient::injector::safety {

enum class SafetyVerdict {
  Approved,
  Refused
};

struct SafetyPreflightRequest {
  process::TargetDescriptor target;
  versioning::VersionDetectionResult identity;
  bool controlled_smoke_target = false;
  std::vector<std::string> allowed_sources;
  std::string target_architecture = "x64";
  std::string anti_cheat_risk = "unknown";
  std::string online_risk = "unknown";
  std::string multiplayer_session_scope = "offline";
  bool multiplayer_mod_compatibility_confirmed = false;
  bool public_matchmaking_detected = false;
  std::filesystem::path runtime_binary_path;
  bool runtime_binary_exists = false;
  std::string runtime_binary_sha256;
  std::string expected_runtime_sha256;
  std::string runtime_architecture = "x64";
};

struct SafetyPreflightResult {
  SafetyVerdict verdict = SafetyVerdict::Refused;
  std::string reason_code = "preflight_not_run";
  std::string message;
  std::vector<diagnostics::LogField> diagnostics_fields;
};

const char* safetyVerdictName(SafetyVerdict verdict);

SafetyPreflightRequest makePreflightRequest(
    const process::TargetDescriptor& target,
    const versioning::VersionDetectionResult& identity,
    bool controlled_smoke_target,
    bool runtime_binary_exists,
    std::string observed_runtime_sha256);

SafetyPreflightResult runSafetyPreflight(
    const SafetyPreflightRequest& request,
    diagnostics::AsyncLogger* logger = nullptr);

}  // namespace vrclient::injector::safety

#pragma once

#include "diagnostics/logging/diagnostic_logger.h"
#include "injector/process/process_discovery.h"

#include <filesystem>
#include <string>
#include <vector>

namespace vrclient::versioning {

enum class DetectionStatus {
  KnownSupported,
  KnownUnsupported,
  Unknown,
  Ambiguous
};

struct FileHashFingerprint {
  std::string algorithm = "sha256";
  std::string value;
};

struct SignatureFingerprint {
  std::string id;
  std::string pattern;
  int confidence = 0;
};

struct IdentityOffsetFingerprint {
  std::string name;
  std::string rva;
  bool phase3_identity_only = true;
};

struct BuildFingerprint {
  std::string build_id;
  bool supported = false;
  std::string refusal_reason;
  int confidence_threshold = 80;
  std::vector<FileHashFingerprint> file_hashes;
  std::vector<std::string> product_versions;
  std::vector<SignatureFingerprint> signatures;
  std::vector<IdentityOffsetFingerprint> offsets;
};

struct SupportPolicy {
  std::vector<std::string> allowed_sources;
  std::string target_architecture = "x64";
  std::string anti_cheat_risk = "unknown";
  std::string online_risk = "unknown";
};

struct RuntimeBinaryExpectation {
  std::filesystem::path path;
  std::string sha256;
  std::string architecture = "x64";
};

struct GameFingerprintConfig {
  int version = 1;
  std::string game_id;
  std::string display_name;
  bool controlled_smoke_target = false;
  std::vector<std::string> executable_names;
  SupportPolicy support_policy;
  RuntimeBinaryExpectation runtime;
  std::vector<BuildFingerprint> builds;
};

struct GameFingerprintLoadResult {
  bool loaded = false;
  std::string message;
  GameFingerprintConfig config;
};

struct VersionMatchEvidence {
  std::string type;
  std::string value;
  int confidence = 0;
};

struct VersionDetectionResult {
  DetectionStatus status = DetectionStatus::Unknown;
  std::string reason_code = "unknown_build";
  std::string game_id;
  std::string display_name;
  std::string build_id;
  int confidence = 0;
  bool refusal_ready = true;
  std::vector<VersionMatchEvidence> evidence;
  SupportPolicy support_policy;
  RuntimeBinaryExpectation runtime;
};

const char* detectionStatusName(DetectionStatus status);

GameFingerprintLoadResult loadGameFingerprintConfig(
    const std::filesystem::path& path);

VersionDetectionResult detectVersion(
    const injector::process::TargetDescriptor& target,
    const GameFingerprintConfig& config,
    diagnostics::AsyncLogger* logger = nullptr);

}  // namespace vrclient::versioning

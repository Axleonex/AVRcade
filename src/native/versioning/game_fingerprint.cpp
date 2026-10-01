#include "versioning/game_fingerprint.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <string_view>

namespace vrclient::versioning {
namespace {

std::string readText(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::in | std::ios::binary);
  if (!input) {
    return {};
  }
  return std::string(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
}

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

bool isSha256Hash(std::string_view value) {
  if (value.size() != 64) {
    return false;
  }
  return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
    return std::isxdigit(ch) != 0;
  });
}

bool isRepeatedCharacterHash(std::string_view value) {
  return value.size() == 64 &&
      std::all_of(value.begin(), value.end(), [first = value.front()](char ch) {
        return ch == first;
      });
}

bool isConfiguredSha256(std::string_view value) {
  return isSha256Hash(value) && !isRepeatedCharacterHash(value);
}

std::string filenameLower(const std::filesystem::path& path) {
  return lower(path.filename().generic_string());
}

bool executableNameAllowed(
    const injector::process::TargetDescriptor& target,
    const GameFingerprintConfig& config) {
  if (config.executable_names.empty()) {
    return false;
  }
  const std::string target_name = filenameLower(target.executable_path);
  return std::find_if(
      config.executable_names.begin(),
      config.executable_names.end(),
      [&target_name](const std::string& allowed) {
        return lower(allowed) == target_name;
      }) != config.executable_names.end();
}

std::optional<std::string> readString(std::string_view text, std::string_view key) {
  const std::regex pattern(
      "\"" + std::string(key) + R"json("\s*:\s*"([^"]*)")json",
      std::regex_constants::ECMAScript);
  std::cmatch match;
  if (std::regex_search(text.data(), text.data() + text.size(), match, pattern)) {
    return match[1].str();
  }
  return std::nullopt;
}

std::optional<int> readInt(std::string_view text, std::string_view key) {
  const std::regex pattern(
      "\"" + std::string(key) + R"("\s*:\s*([0-9]+))",
      std::regex_constants::ECMAScript);
  std::cmatch match;
  if (std::regex_search(text.data(), text.data() + text.size(), match, pattern)) {
    return std::stoi(match[1].str());
  }
  return std::nullopt;
}

std::optional<bool> readBool(std::string_view text, std::string_view key) {
  const std::regex pattern(
      "\"" + std::string(key) + R"("\s*:\s*(true|false))",
      std::regex_constants::ECMAScript);
  std::cmatch match;
  if (std::regex_search(text.data(), text.data() + text.size(), match, pattern)) {
    return match[1].str() == "true";
  }
  return std::nullopt;
}

std::optional<std::string_view> findObject(
    std::string_view text,
    std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\"";
  const size_t key_pos = text.find(needle);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }
  const size_t open = text.find('{', key_pos + needle.size());
  if (open == std::string_view::npos) {
    return std::nullopt;
  }

  int depth = 0;
  for (size_t i = open; i < text.size(); ++i) {
    if (text[i] == '{') {
      ++depth;
    } else if (text[i] == '}') {
      --depth;
      if (depth == 0) {
        return text.substr(open, i - open + 1);
      }
    }
  }
  return std::nullopt;
}

std::optional<std::string_view> findArray(
    std::string_view text,
    std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\"";
  const size_t key_pos = text.find(needle);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }
  const size_t open = text.find('[', key_pos + needle.size());
  if (open == std::string_view::npos) {
    return std::nullopt;
  }

  int array_depth = 0;
  int object_depth = 0;
  bool in_string = false;
  for (size_t i = open; i < text.size(); ++i) {
    const char ch = text[i];
    const bool escaped = i > 0 && text[i - 1] == '\\';
    if (ch == '"' && !escaped) {
      in_string = !in_string;
      continue;
    }
    if (in_string) {
      continue;
    }
    if (ch == '[') {
      ++array_depth;
    } else if (ch == ']') {
      --array_depth;
      if (array_depth == 0 && object_depth == 0) {
        return text.substr(open, i - open + 1);
      }
    } else if (ch == '{') {
      ++object_depth;
    } else if (ch == '}') {
      --object_depth;
    }
  }
  return std::nullopt;
}

std::vector<std::string> readStringArray(
    std::string_view text,
    std::string_view key) {
  std::vector<std::string> values;
  const auto array = findArray(text, key);
  if (!array) {
    return values;
  }

  const std::regex string_pattern(R"json("([^"]*)")json");
  auto begin = std::cregex_iterator(array->data(), array->data() + array->size(), string_pattern);
  const auto end = std::cregex_iterator();
  for (auto it = begin; it != end; ++it) {
    values.push_back((*it)[1].str());
  }
  return values;
}

std::vector<std::string_view> readObjectArray(
    std::string_view text,
    std::string_view key) {
  std::vector<std::string_view> objects;
  const auto array = findArray(text, key);
  if (!array) {
    return objects;
  }

  bool in_string = false;
  int depth = 0;
  size_t object_start = std::string_view::npos;
  for (size_t i = 0; i < array->size(); ++i) {
    const char ch = (*array)[i];
    const bool escaped = i > 0 && (*array)[i - 1] == '\\';
    if (ch == '"' && !escaped) {
      in_string = !in_string;
      continue;
    }
    if (in_string) {
      continue;
    }
    if (ch == '{') {
      if (depth == 0) {
        object_start = i;
      }
      ++depth;
    } else if (ch == '}') {
      --depth;
      if (depth == 0 && object_start != std::string_view::npos) {
        objects.push_back(array->substr(object_start, i - object_start + 1));
        object_start = std::string_view::npos;
      }
    }
  }
  return objects;
}

std::vector<FileHashFingerprint> readFileHashes(std::string_view text) {
  std::vector<FileHashFingerprint> hashes;
  for (std::string_view object : readObjectArray(text, "file_hashes")) {
    FileHashFingerprint hash;
    hash.algorithm = readString(object, "algorithm").value_or("sha256");
    hash.value = lower(readString(object, "value").value_or(""));
    if (!hash.value.empty()) {
      hashes.push_back(std::move(hash));
    }
  }
  return hashes;
}

std::vector<SignatureFingerprint> readSignatures(std::string_view text) {
  std::vector<SignatureFingerprint> signatures;
  for (std::string_view object : readObjectArray(text, "signatures")) {
    SignatureFingerprint signature;
    signature.id = readString(object, "id").value_or("");
    signature.pattern = readString(object, "pattern").value_or("");
    signature.confidence = readInt(object, "confidence").value_or(0);
    if (!signature.id.empty() || !signature.pattern.empty()) {
      signatures.push_back(std::move(signature));
    }
  }
  return signatures;
}

std::vector<IdentityOffsetFingerprint> readIdentityOffsets(std::string_view text) {
  std::vector<IdentityOffsetFingerprint> offsets;
  for (std::string_view object : readObjectArray(text, "offsets")) {
    IdentityOffsetFingerprint offset;
    offset.name = readString(object, "name").value_or("");
    offset.rva = readString(object, "rva").value_or("");
    offset.phase3_identity_only =
        readBool(object, "phase3_identity_only").value_or(true);
    if (!offset.name.empty()) {
      offsets.push_back(std::move(offset));
    }
  }
  return offsets;
}

BuildFingerprint readBuild(std::string_view object) {
  BuildFingerprint build;
  build.build_id = readString(object, "build_id").value_or("");
  build.supported = readString(object, "support").value_or("") == "supported";
  build.refusal_reason = readString(object, "refusal_reason").value_or("");
  build.confidence_threshold = readInt(object, "confidence_threshold").value_or(80);
  build.file_hashes = readFileHashes(object);
  build.product_versions = readStringArray(object, "product_versions");
  build.signatures = readSignatures(object);
  build.offsets = readIdentityOffsets(object);
  return build;
}

int matchBuild(
    const injector::process::TargetDescriptor& target,
    const BuildFingerprint& build,
    std::vector<VersionMatchEvidence>* evidence) {
  int confidence = 0;
  const std::string target_hash = lower(target.executable_sha256);
  for (const FileHashFingerprint& hash : build.file_hashes) {
    if (isSha256Hash(target_hash) && lower(hash.value) == target_hash) {
      confidence = std::max(confidence, 100);
      evidence->push_back({"sha256", hash.value, 100});
    }
  }

  for (const std::string& version : build.product_versions) {
    if (!target.product_version.empty() && target.product_version == version) {
      confidence = std::max(confidence, 65);
      evidence->push_back({"product_version", version, 65});
    }
  }

  for (const SignatureFingerprint& signature : build.signatures) {
    const bool found = std::find(
        target.signature_tags.begin(),
        target.signature_tags.end(),
        signature.id) != target.signature_tags.end() ||
        std::find(
            target.signature_tags.begin(),
            target.signature_tags.end(),
            signature.pattern) != target.signature_tags.end();
    if (found) {
      confidence = std::max(confidence, signature.confidence);
      evidence->push_back({"signature", signature.id, signature.confidence});
    }
  }

  return confidence;
}

bool supportPolicyValid(const SupportPolicy& policy) {
  const auto anti_cheat = lower(policy.anti_cheat_risk);
  const auto online = lower(policy.online_risk);
  const bool anti_cheat_valid =
      anti_cheat == "none" || anti_cheat == "known_safe" ||
      anti_cheat == "known_risky" || anti_cheat == "unknown";
  const bool online_valid =
      online == "none" || online == "offline_only" ||
      online == "private_modded_coop" ||
      online == "known_online" || online == "unknown";
  return !policy.allowed_sources.empty() &&
      !policy.target_architecture.empty() &&
      anti_cheat_valid &&
      online_valid;
}

bool buildConfigValid(const BuildFingerprint& build) {
  if (build.build_id.empty() ||
      build.confidence_threshold < 1 ||
      build.confidence_threshold > 100) {
    return false;
  }
  for (const FileHashFingerprint& hash : build.file_hashes) {
    if (lower(hash.algorithm) != "sha256" || !isConfiguredSha256(hash.value)) {
      return false;
    }
  }
  for (const SignatureFingerprint& signature : build.signatures) {
    if (signature.confidence < 1 || signature.confidence > 99) {
      return false;
    }
  }
  return true;
}

bool configValidForPhase3(const GameFingerprintConfig& config) {
  if (config.game_id.empty() ||
      config.display_name.empty() ||
      config.executable_names.empty() ||
      config.runtime.path.empty() ||
      config.runtime.architecture.empty() ||
      !isConfiguredSha256(config.runtime.sha256) ||
      !supportPolicyValid(config.support_policy) ||
      config.builds.empty()) {
    return false;
  }
  return std::all_of(config.builds.begin(), config.builds.end(), buildConfigValid);
}

void logDetection(
    const VersionDetectionResult& result,
    diagnostics::AsyncLogger* logger) {
  if (logger == nullptr) {
    return;
  }
  logger->log(
      result.status == DetectionStatus::KnownSupported ? diagnostics::Severity::Info
                                                       : diagnostics::Severity::Warning,
      "version_detection_result",
      {
          {"status", detectionStatusName(result.status)},
          {"reason_code", result.reason_code},
          {"game_id", result.game_id},
          {"build_id", result.build_id},
          {"confidence", std::to_string(result.confidence)},
      });
}

}  // namespace

const char* detectionStatusName(DetectionStatus status) {
  switch (status) {
    case DetectionStatus::KnownSupported:
      return "known_supported";
    case DetectionStatus::KnownUnsupported:
      return "known_unsupported";
    case DetectionStatus::Unknown:
      return "unknown";
    case DetectionStatus::Ambiguous:
      return "ambiguous";
  }
  return "unknown";
}

GameFingerprintLoadResult loadGameFingerprintConfig(
    const std::filesystem::path& path) {
  GameFingerprintLoadResult result;
  const std::string text = readText(path);
  if (text.empty()) {
    result.message = "fingerprint config not found or empty";
    return result;
  }

  result.config.version = readInt(text, "version").value_or(1);
  result.config.game_id = readString(text, "game_id").value_or("");
  result.config.display_name = readString(text, "display_name").value_or("");
  result.config.controlled_smoke_target =
      readBool(text, "controlled_smoke_target").value_or(false);
  result.config.executable_names = readStringArray(text, "executable_names");

  if (const auto policy = findObject(text, "support_policy")) {
    result.config.support_policy.allowed_sources =
        readStringArray(*policy, "allowed_sources");
    result.config.support_policy.target_architecture =
        readString(*policy, "target_architecture").value_or("x64");
    result.config.support_policy.anti_cheat_risk =
        readString(*policy, "anti_cheat_risk").value_or("unknown");
    result.config.support_policy.online_risk =
        readString(*policy, "online_risk").value_or("unknown");
  }

  if (const auto runtime = findObject(text, "runtime")) {
    result.config.runtime.path = readString(*runtime, "path").value_or("");
    result.config.runtime.sha256 = lower(readString(*runtime, "sha256").value_or(""));
    result.config.runtime.architecture =
        readString(*runtime, "architecture").value_or("x64");
  }

  for (std::string_view build_object : readObjectArray(text, "builds")) {
    result.config.builds.push_back(readBuild(build_object));
  }

  result.loaded = configValidForPhase3(result.config);
  result.message = result.loaded ? "fingerprint config loaded"
                                 : "fingerprint config missing required fields";
  return result;
}

VersionDetectionResult detectVersion(
    const injector::process::TargetDescriptor& target,
    const GameFingerprintConfig& config,
    diagnostics::AsyncLogger* logger) {
  VersionDetectionResult result;
  result.game_id = config.game_id;
  result.display_name = config.display_name;
  result.support_policy = config.support_policy;
  result.runtime = config.runtime;

  if (!target.identity_evidence_trusted) {
    result.status = DetectionStatus::Unknown;
    result.reason_code = "untrusted_identity_evidence";
    result.evidence.push_back({"target", target.executable_path.string(), 0});
    logDetection(result, logger);
    return result;
  }

  if (!executableNameAllowed(target, config)) {
    result.status = DetectionStatus::Unknown;
    result.reason_code = "executable_name_mismatch";
    result.evidence.push_back({"executable_name", target.executable_path.filename().string(), 0});
    logDetection(result, logger);
    return result;
  }

  struct Candidate {
    const BuildFingerprint* build = nullptr;
    int confidence = 0;
    std::vector<VersionMatchEvidence> evidence;
  };

  std::vector<Candidate> matches;
  for (const BuildFingerprint& build : config.builds) {
    Candidate candidate;
    candidate.build = &build;
    candidate.confidence = matchBuild(target, build, &candidate.evidence);
    if (candidate.confidence >= build.confidence_threshold) {
      matches.push_back(std::move(candidate));
    }
  }

  if (matches.empty()) {
    result.status = DetectionStatus::Unknown;
    result.reason_code = "unknown_build";
    result.evidence.push_back({"target", target.executable_path.string(), 0});
    logDetection(result, logger);
    return result;
  }

  std::sort(matches.begin(), matches.end(), [](const Candidate& left, const Candidate& right) {
    return left.confidence > right.confidence;
  });

  const int best_confidence = matches.front().confidence;
  const size_t tied_count = static_cast<size_t>(std::count_if(
      matches.begin(),
      matches.end(),
      [best_confidence](const Candidate& candidate) {
        return candidate.confidence == best_confidence;
      }));
  if (tied_count > 1) {
    result.status = DetectionStatus::Ambiguous;
    result.reason_code = "ambiguous_build";
    result.confidence = best_confidence;
    for (size_t i = 0; i < tied_count; ++i) {
      result.evidence.push_back(
          {"ambiguous_candidate", matches[i].build->build_id, best_confidence});
    }
    logDetection(result, logger);
    return result;
  }

  const BuildFingerprint& build = *matches.front().build;
  result.build_id = build.build_id;
  result.confidence = matches.front().confidence;
  result.evidence = std::move(matches.front().evidence);
  result.status = build.supported ? DetectionStatus::KnownSupported
                                  : DetectionStatus::KnownUnsupported;
  result.reason_code = build.supported
      ? "known_supported_build"
      : (build.refusal_reason.empty() ? "known_unsupported_build"
                                      : build.refusal_reason);
  result.refusal_ready = !build.supported;
  logDetection(result, logger);
  return result;
}

}  // namespace vrclient::versioning

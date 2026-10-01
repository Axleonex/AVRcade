#include "diagnostics/diagnostics_profile.h"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <string_view>
#include <system_error>

namespace vrclient::diagnostics {
namespace {

std::optional<std::string_view> findObject(std::string_view json, std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\"";
  const size_t key_pos = json.find(needle);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }

  const size_t open = json.find('{', key_pos + needle.size());
  if (open == std::string_view::npos) {
    return std::nullopt;
  }

  int depth = 0;
  for (size_t i = open; i < json.size(); ++i) {
    if (json[i] == '{') {
      ++depth;
    } else if (json[i] == '}') {
      --depth;
      if (depth == 0) {
        return json.substr(open, i - open + 1);
      }
    }
  }
  return std::nullopt;
}

std::optional<std::string_view> findValueToken(
    std::string_view json,
    std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\"";
  const size_t key_pos = json.find(needle);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }
  const size_t colon = json.find(':', key_pos + needle.size());
  if (colon == std::string_view::npos) {
    return std::nullopt;
  }

  size_t value_start = colon + 1;
  while (value_start < json.size() &&
         (json[value_start] == ' ' || json[value_start] == '\t' ||
          json[value_start] == '\r' || json[value_start] == '\n')) {
    ++value_start;
  }
  if (value_start >= json.size()) {
    return std::nullopt;
  }
  if (json[value_start] == '"') {
    const size_t close = json.find('"', value_start + 1);
    if (close == std::string_view::npos) {
      return std::nullopt;
    }
    return json.substr(value_start + 1, close - value_start - 1);
  }

  size_t value_end = value_start;
  while (value_end < json.size() && json[value_end] != ',' &&
         json[value_end] != '}' && json[value_end] != '\r' &&
         json[value_end] != '\n') {
    ++value_end;
  }
  while (value_end > value_start &&
         (json[value_end - 1] == ' ' || json[value_end - 1] == '\t')) {
    --value_end;
  }
  return json.substr(value_start, value_end - value_start);
}

std::optional<bool> readBool(std::string_view json, std::string_view key) {
  const auto token = findValueToken(json, key);
  if (!token) {
    return std::nullopt;
  }
  if (*token == "true") {
    return true;
  }
  if (*token == "false") {
    return false;
  }
  return std::nullopt;
}

std::optional<int> readInt(std::string_view json, std::string_view key) {
  const auto token = findValueToken(json, key);
  if (!token) {
    return std::nullopt;
  }
  int value = 0;
  const auto* first = token->data();
  const auto* last = token->data() + token->size();
  const auto result = std::from_chars(first, last, value);
  if (result.ec != std::errc() || result.ptr != last) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::string> readString(std::string_view json, std::string_view key) {
  const auto token = findValueToken(json, key);
  if (!token) {
    return std::nullopt;
  }
  return std::string(*token);
}

bool inRange(int value, int minimum, int maximum) {
  return value >= minimum && value <= maximum;
}

void applyCommon(
    const std::optional<std::string_view>& logging,
    const std::optional<std::string_view>& crash,
    const std::optional<std::string_view>& overlay,
    const std::optional<std::string_view>& export_section,
    const std::optional<std::string_view>& telemetry_aggregation,
    DiagnosticsProfile* profile) {
  if (logging) {
    if (const auto enabled = readBool(*logging, "enabled")) {
      profile->logging.enabled = *enabled;
    }
    if (const auto directory = readString(*logging, "directory")) {
      profile->logging.log_directory = *directory;
    }
    if (const auto max_buffer = readInt(*logging, "max_buffer_records")) {
      if (inRange(*max_buffer, 1, 1'000'000)) {
        profile->logging.max_buffer_records = static_cast<std::size_t>(*max_buffer);
      }
    }
    if (const auto max_retained = readInt(*logging, "max_retained_logs")) {
      if (inRange(*max_retained, 1, 10'000)) {
        profile->logging.max_retained_logs = static_cast<std::size_t>(*max_retained);
      }
    }
    if (const auto max_file = readInt(*logging, "max_file_bytes")) {
      if (inRange(*max_file, 4096, 1024 * 1024 * 1024)) {
        profile->logging.max_file_bytes = static_cast<std::uintmax_t>(*max_file);
      }
    }
    if (const auto flush = readBool(*logging, "flush_on_shutdown")) {
      profile->logging.flush_on_shutdown = *flush;
    }
  }
  if (crash) {
    if (const auto enabled = readBool(*crash, "enabled")) {
      profile->crash.enabled = *enabled;
    }
    if (const auto directory = readString(*crash, "directory")) {
      profile->crash.artifact_directory = *directory;
    }
    if (const auto max_recent = readInt(*crash, "max_recent_records")) {
      if (inRange(*max_recent, 1, 10'000)) {
        profile->crash.max_recent_records = static_cast<std::size_t>(*max_recent);
      }
    }
  }
  if (overlay) {
    if (const auto enabled = readBool(*overlay, "enabled")) {
      profile->overlay_enabled = *enabled;
    }
  }
  if (export_section) {
    if (const auto directory = readString(*export_section, "directory")) {
      profile->export_request.output_directory = *directory;
    }
    if (const auto include_environment = readBool(*export_section, "include_environment")) {
      profile->export_request.include_environment = *include_environment;
    }
  }
  if (telemetry_aggregation) {
    const bool local_only =
        readBool(*telemetry_aggregation, "local_only").value_or(true);
    const bool automatic_upload =
        readBool(*telemetry_aggregation, "automatic_upload").value_or(false);
    if (const auto enabled = readBool(*telemetry_aggregation, "enabled")) {
      profile->telemetry_aggregation_enabled =
          *enabled && local_only && !automatic_upload;
      profile->export_request.include_telemetry_summary =
          profile->telemetry_aggregation_enabled;
    }
  }
}

}  // namespace

DiagnosticsProfile defaultDiagnosticsProfile() {
  DiagnosticsProfile profile;
  profile.logging.file_prefix = "vrclient-runtime";
  profile.export_request.log_directory = profile.logging.log_directory;
  profile.export_request.crash_directory = profile.crash.artifact_directory;
  return profile;
}

DiagnosticsProfileLoadResult loadDiagnosticsProfileFromFile(const char* path) {
  DiagnosticsProfileLoadResult result;
  result.profile = defaultDiagnosticsProfile();
  if (path == nullptr || path[0] == '\0') {
    result.message = "using default diagnostics profile";
    return result;
  }

  std::ifstream input(path, std::ios::in | std::ios::binary);
  if (!input) {
    result.message = "diagnostics profile not found; using defaults";
    return result;
  }

  const std::string json((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());

  if (const auto enabled = readBool(json, "enabled")) {
    result.profile.enabled = *enabled;
  }

  applyCommon(
      findObject(json, "logging"),
      findObject(json, "crash_capture"),
      findObject(json, "overlay"),
      findObject(json, "export"),
      findObject(json, "telemetry_aggregation"),
      &result.profile);
  result.profile.export_request.log_directory = result.profile.logging.log_directory;
  result.profile.export_request.crash_directory = result.profile.crash.artifact_directory;
  result.loaded = true;
  result.message = "diagnostics profile loaded";
  return result;
}

}  // namespace vrclient::diagnostics

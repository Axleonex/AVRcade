#include "diagnostics/export/diagnostics_exporter.h"

#include "diagnostics/logging/diagnostic_logger.h"

#include <chrono>
#include <ctime>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <regex>
#include <sstream>
#include <system_error>

namespace vrclient::diagnostics {
namespace {

std::string exportTimestamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t raw = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &raw);
#else
  gmtime_r(&raw, &tm);
#endif
  char buffer[32]{};
  std::strftime(buffer, sizeof(buffer), "%Y%m%dT%H%M%SZ", &tm);
  return buffer;
}

std::string readTextFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::in | std::ios::binary);
  if (!input) {
    return {};
  }
  std::ostringstream text;
  text << input.rdbuf();
  return text.str();
}

bool writeTextFile(const std::filesystem::path& path, const std::string& text) {
  std::ofstream output(path, std::ios::out | std::ios::trunc | std::ios::binary);
  if (!output) {
    return false;
  }
  output << text;
  return true;
}

std::string processArchitectureName() {
#if defined(_M_X64) || defined(__x86_64__)
  return "x64";
#elif defined(_M_IX86) || defined(__i386__)
  return "x86";
#elif defined(_M_ARM64) || defined(__aarch64__)
  return "arm64";
#else
  return "unknown";
#endif
}

bool writeEnvironmentFile(
    const std::filesystem::path& destination,
    std::vector<std::filesystem::path>* copied) {
  std::error_code error;
  std::filesystem::create_directories(destination.parent_path(), error);
  if (error) {
    return false;
  }

  std::ostringstream text;
  text << "{\n"
       << "  \"runtime_version\": \"0.1.0\",\n"
       << "  \"process_architecture\": \"" << processArchitectureName() << "\",\n"
       << "  \"automatic_upload\": false\n"
       << "}\n";
  if (!writeTextFile(destination, text.str())) {
    return false;
  }
  copied->push_back(destination);
  return true;
}

void copyRedactedFiles(
    const std::filesystem::path& source_directory,
    const std::filesystem::path& destination_directory,
    std::vector<std::filesystem::path>* copied) {
  std::error_code error;
  if (!std::filesystem::exists(source_directory, error)) {
    return;
  }
  std::filesystem::create_directories(destination_directory, error);
  if (error) {
    return;
  }

  for (const auto& entry : std::filesystem::directory_iterator(source_directory, error)) {
    if (error || !entry.is_regular_file()) {
      continue;
    }
    const std::filesystem::path destination = destination_directory / entry.path().filename();
    if (writeTextFile(destination, redactDiagnosticText(readTextFile(entry.path())))) {
      copied->push_back(destination);
    }
  }
}

void copyOptionalRedactedFile(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::vector<std::filesystem::path>* copied) {
  std::error_code error;
  if (!std::filesystem::exists(source, error)) {
    return;
  }
  std::filesystem::create_directories(destination.parent_path(), error);
  if (error) {
    return;
  }
  if (writeTextFile(destination, redactDiagnosticText(readTextFile(source)))) {
    copied->push_back(destination);
  }
}

std::uintmax_t countRegularFiles(const std::filesystem::path& directory) {
  std::error_code error;
  if (!std::filesystem::exists(directory, error)) {
    return 0;
  }
  std::uintmax_t count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
    if (error) {
      break;
    }
    if (entry.is_regular_file()) {
      ++count;
    }
  }
  return count;
}

std::uintmax_t countNonEmptyLines(const std::filesystem::path& directory) {
  std::error_code error;
  if (!std::filesystem::exists(directory, error)) {
    return 0;
  }
  std::uintmax_t count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
    if (error || !entry.is_regular_file()) {
      continue;
    }
    std::ifstream input(entry.path(), std::ios::in | std::ios::binary);
    std::string line;
    while (std::getline(input, line)) {
      if (!line.empty()) {
        ++count;
      }
    }
  }
  return count;
}

bool writeTelemetrySummaryFile(
    const std::filesystem::path& destination,
    const DiagnosticsExportRequest& request,
    const std::string& session,
    std::vector<std::filesystem::path>* copied) {
  std::error_code error;
  std::filesystem::create_directories(destination.parent_path(), error);
  if (error) {
    return false;
  }

  const std::uintmax_t log_file_count = countRegularFiles(request.log_directory);
  const std::uintmax_t crash_file_count = countRegularFiles(request.crash_directory);
  const std::uintmax_t log_record_count = countNonEmptyLines(request.log_directory);

  std::ostringstream text;
  text << "{\n"
       << "  \"summary_type\": \"vrclient_local_telemetry_aggregate\",\n"
       << "  \"session_id\": \"" << jsonEscape(session) << "\",\n"
       << "  \"user_triggered\": true,\n"
       << "  \"local_only\": true,\n"
       << "  \"automatic_upload\": false,\n"
       << "  \"network_upload\": false,\n"
       << "  \"source\": \"local_export\",\n"
       << "  \"log_file_count\": " << log_file_count << ",\n"
       << "  \"log_record_count\": " << log_record_count << ",\n"
       << "  \"crash_artifact_count\": " << crash_file_count << "\n"
       << "}\n";

  if (!writeTextFile(destination, text.str())) {
    return false;
  }
  copied->push_back(destination);
  return true;
}

}  // namespace

std::string redactDiagnosticText(std::string_view text) {
  std::string redacted(text);

  if (const char* user_profile = std::getenv("USERPROFILE")) {
    const std::string profile(user_profile);
    if (!profile.empty()) {
      std::size_t pos = 0;
      while ((pos = redacted.find(profile, pos)) != std::string::npos) {
        redacted.replace(pos, profile.size(), "%USERPROFILE%");
        pos += std::string("%USERPROFILE%").size();
      }
    }
  }

  redacted = std::regex_replace(
      redacted,
      std::regex(R"([A-Za-z]:\\Users\\[^\\\s\r\n\"]+)"),
      "%USERPROFILE%");
  redacted = std::regex_replace(
      redacted,
      std::regex(R"secret(("?)(api[_-]?key|token|secret|password)\1(\s*[:=]\s*"?)[^",\s}]+("?))secret",
                 std::regex_constants::icase),
      "$1$2$1$3[REDACTED]$4");
  return redacted;
}

DiagnosticsExportResult exportDiagnosticsBundle(const DiagnosticsExportRequest& request) {
  DiagnosticsExportResult result;
  const std::string session =
      request.session_id.empty() ? makeSessionId() : request.session_id;
  result.bundle_directory =
      request.output_directory / ("vrclient-diagnostics-" + exportTimestamp() + "-" + session);

  std::error_code error;
  std::filesystem::create_directories(result.bundle_directory, error);
  if (error) {
    result.message = error.message();
    return result;
  }

  copyRedactedFiles(
      request.log_directory,
      result.bundle_directory / "logs",
      &result.files);
  copyRedactedFiles(
      request.crash_directory,
      result.bundle_directory / "crashes",
      &result.files);
  copyOptionalRedactedFile(
      request.runtime_profile_path,
      result.bundle_directory / "config" / "runtime-profile.json",
      &result.files);
  copyOptionalRedactedFile(
      request.diagnostics_profile_path,
      result.bundle_directory / "config" / "diagnostics-profile.json",
      &result.files);
  if (request.include_environment) {
    writeEnvironmentFile(result.bundle_directory / "environment.json", &result.files);
  }
  if (request.include_telemetry_summary) {
    writeTelemetrySummaryFile(
        result.bundle_directory / "telemetry" / "summary.json",
        request,
        session,
        &result.files);
  }

  std::ofstream manifest(
      result.bundle_directory / "manifest.json",
      std::ios::out | std::ios::trunc);
  if (!manifest) {
    result.message = "could not write export manifest";
    return result;
  }

  manifest << "{\n"
           << "  \"bundle_type\": \"vrclient_diagnostics\",\n"
           << "  \"session_id\": \"" << jsonEscape(session) << "\",\n"
           << "  \"user_triggered\": true,\n"
           << "  \"automatic_upload\": false,\n"
           << "  \"include_environment\": "
           << (request.include_environment ? "true" : "false") << ",\n"
           << "  \"include_telemetry_summary\": "
           << (request.include_telemetry_summary ? "true" : "false") << ",\n"
           << "  \"files\": [";
  for (std::size_t i = 0; i < result.files.size(); ++i) {
    if (i != 0) {
      manifest << ", ";
    }
    manifest << "\"" << jsonEscape(result.files[i].filename().string()) << "\"";
  }
  manifest << "]\n}\n";

  result.files.push_back(result.bundle_directory / "manifest.json");
  result.success = true;
  result.message = "diagnostics export created";
  return result;
}

}  // namespace vrclient::diagnostics

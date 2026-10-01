#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace vrclient::diagnostics {

struct DiagnosticsExportRequest {
  std::filesystem::path output_directory = "diagnostic-exports";
  std::filesystem::path log_directory = "logs/diagnostics";
  std::filesystem::path crash_directory = "logs/crashes";
  std::filesystem::path runtime_profile_path = "config/defaults/runtime-profile.json";
  std::filesystem::path diagnostics_profile_path = "config/defaults/diagnostics-profile.json";
  std::string session_id;
  bool include_environment = true;
  // B7/CRASH-02: explicit opt-in local aggregation. Default false preserves the
  // local-first/no-auto-upload posture; when true, export writes only a local
  // aggregate summary file into the user-triggered bundle.
  bool include_telemetry_summary = false;
};

struct DiagnosticsExportResult {
  bool success = false;
  std::filesystem::path bundle_directory;
  std::string message;
  std::vector<std::filesystem::path> files;
};

std::string redactDiagnosticText(std::string_view text);
DiagnosticsExportResult exportDiagnosticsBundle(const DiagnosticsExportRequest& request);

}  // namespace vrclient::diagnostics

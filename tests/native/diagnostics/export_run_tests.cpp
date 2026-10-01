// DIAG-06 closure (Phase 2): a ledgered diagnostics EXPORT RUN that redacts a
// REAL sensitive path end-to-end and proves the bundle is local + operator-
// triggered with NO automatic upload.
//
// Unlike the existing runExportRedactionTest() (which redacts a *fake*
// C:\Users\Alice path literal), this test seeds a log file with the LIVE
// %USERPROFILE% value read from the environment, runs exportDiagnosticsBundle,
// and asserts:
//   1. result.success and the bundle is a real local directory (no network).
//   2. the live %USERPROFILE% value is GONE from the copied log and replaced
//      with the literal token %USERPROFILE% (real-path redaction proven).
//   3. the env-var redaction leg is exercised INDEPENDENTLY of the path-regex
//      backstop (see below) so this closure does not silently pass if the
//      env-var leg regresses.
//   4. manifest.json carries "user_triggered": true and "automatic_upload": false.
//   5. environment.json exists and carries "automatic_upload": false.
//
// Env-var leg isolation (no-false-green hardening):
//   redactDiagnosticText has TWO legs that can both emit %USERPROFILE%:
//     (a) literal replacement of the exact $USERPROFILE value, and
//     (b) a generic path regex  [A-Za-z]:\\Users\\[^\\\s\r\n"]+ .
//   Because the live profile (C:\Users\<name>) is itself a \Users\ path, leg (b)
//   alone would redact a plain occurrence -- so a plain assertion cannot tell the
//   two legs apart. To pin leg (a) specifically we seed the live profile value
//   immediately followed by a non-separator char, e.g.  <profile>Z . Then:
//     * leg (a) removes the EXACT profile literal, leaving the trailing "Z"  ->
//       output token is exactly "%USERPROFILE%Z".
//     * leg (b) (greedy [^\\\s\r\n"]+) would instead swallow the trailing "Z"
//       and emit "%USERPROFILE%" with NO trailing "Z".
//   So the marker "%USERPROFILE%Z" (token + the surviving char) is produced ONLY
//   by the env-var leg. If leg (a) were removed/broken, the regex backstop would
//   emit "%USERPROFILE%" without the "Z" and this assertion would FAIL.
//
// Self-contained C++ harness (same style as diagnostics_tests.cpp): a free
// expect() that throws -> nonzero exit -> CTest RED. Scratch lands under
// temp_directory_path(); cleaned up on the success path.

#include "diagnostics/export/diagnostics_exporter.h"
#include "diagnostics/logging/diagnostic_logger.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string readText(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::in | std::ios::binary);
  std::ostringstream text;
  text << input.rdbuf();
  return text.str();
}

std::string liveUserProfile() {
  if (const char* user_profile = std::getenv("USERPROFILE")) {
    return std::string(user_profile);
  }
  return {};
}

void runExportRunRealUserProfileRedaction() {
  const std::string user_profile = liveUserProfile();
  // The redaction this closure proves keys off the live %USERPROFILE% env var.
  // On any Windows host (the only platform this build targets) it is set; refuse
  // to false-green if it is somehow empty.
  expect(!user_profile.empty(),
         "USERPROFILE must be set to prove real-path redaction (Windows host)");

  const auto root = std::filesystem::temp_directory_path() /
      ("vrclient-export-run-" + vrclient::diagnostics::makeSessionId());
  const auto logs = root / "logs";
  std::filesystem::create_directories(logs);

  // A real sensitive path under the live user profile, plus fake secrets.
  const std::string sensitive_save_path =
      user_profile + "\\Saved Games\\VRClient\\session.dat";

  // Env-var-leg isolation seed: the EXACT live profile literal immediately
  // followed by a non-separator char. Only the env-var leg leaves the trailing
  // char attached to the token (see file header). We use a value that is NOT a
  // valid filesystem path so its only redaction-worthiness is the env literal.
  const char kEnvLegMarkerSuffix = 'Z';
  const std::string env_leg_value = user_profile + kEnvLegMarkerSuffix;  // <profile>Z
  const std::string env_leg_expected_token =
      std::string("%USERPROFILE%") + kEnvLegMarkerSuffix;               // %USERPROFILE%Z
  {
    std::ofstream output(logs / "session.jsonl", std::ios::out | std::ios::trunc);
    output << "{\"event\":\"launch\",\"save_path\":\"" << sensitive_save_path
           << "\",\"home_marker\":\"" << env_leg_value << "\","
           << "\"token\":\"abcd1234deadbeef\","
           << "\"api_key\":\"sekret-key-value\",\"password\":\"hunter2\"}\n";
  }

  vrclient::diagnostics::DiagnosticsExportRequest request;
  request.output_directory = root / "exports";
  request.log_directory = logs;
  request.crash_directory = root / "crashes";  // intentionally absent -> skipped
  request.session_id = "diag06-export-run";
  request.include_environment = true;

  const auto result = vrclient::diagnostics::exportDiagnosticsBundle(request);
  expect(result.success, "diagnostics export run should succeed");
  expect(std::filesystem::is_directory(result.bundle_directory),
         "export bundle must be a real local directory");

  // (2) Real-path redaction: the live %USERPROFILE% value must be GONE and the
  // literal token present in the copied log.
  const auto copied_log = result.bundle_directory / "logs" / "session.jsonl";
  expect(std::filesystem::exists(copied_log), "copied log must exist in bundle");
  const std::string copied = readText(copied_log);
  expect(copied.find(user_profile) == std::string::npos,
         "live %USERPROFILE% value must be redacted out of the exported log");
  expect(copied.find("%USERPROFILE%") != std::string::npos,
         "redacted log must contain the literal %USERPROFILE% token");

  // (2b) Env-var-leg isolation: the marker "%USERPROFILE%Z" can ONLY be produced
  // by the literal env-var replacement leg. The greedy path-regex backstop would
  // consume the trailing 'Z' and emit "%USERPROFILE%" without it. So this pins
  // the env-var leg independently: it fails if leg (a) regresses, even though the
  // regex backstop still removes the live path. (See file header.)
  expect(copied.find(env_leg_value) == std::string::npos,
         "live <profile>Z value must be fully redacted (no live profile remnant)");
  expect(copied.find(env_leg_expected_token) != std::string::npos,
         "env-var redaction leg must emit %USERPROFILE%Z (regex backstop would "
         "drop the trailing char) -- proves the env-var leg, not just the regex");

  // Secrets must also be gone (defense-in-depth; same redactor leg).
  expect(copied.find("abcd1234deadbeef") == std::string::npos,
         "token value must be redacted");
  expect(copied.find("sekret-key-value") == std::string::npos,
         "api_key value must be redacted");
  expect(copied.find("hunter2") == std::string::npos,
         "password value must be redacted");

  // (3) Manifest is the policy anchor: operator-triggered, never auto-uploaded.
  const auto manifest_path = result.bundle_directory / "manifest.json";
  expect(std::filesystem::exists(manifest_path), "manifest.json must exist");
  const std::string manifest = readText(manifest_path);
  expect(manifest.find("\"bundle_type\": \"vrclient_diagnostics\"") != std::string::npos,
         "manifest must declare the diagnostics bundle type");
  expect(manifest.find("\"user_triggered\": true") != std::string::npos,
         "manifest must mark the export user_triggered");
  expect(manifest.find("\"automatic_upload\": false") != std::string::npos,
         "manifest must mark automatic_upload false (no auto-upload)");

  // (4) Environment file also records no automatic upload.
  const auto environment_path = result.bundle_directory / "environment.json";
  expect(std::filesystem::exists(environment_path), "environment.json must exist");
  const std::string environment = readText(environment_path);
  expect(environment.find("\"automatic_upload\": false") != std::string::npos,
         "environment.json must record automatic_upload false");

  // CRASH-02/03: telemetry aggregation is opt-in and local-only. The default
  // request must NOT write the aggregate summary.
  const auto no_summary_path = result.bundle_directory / "telemetry" / "summary.json";
  expect(!std::filesystem::exists(no_summary_path),
         "telemetry summary must be absent unless explicitly opted in");
  expect(manifest.find("\"include_telemetry_summary\": false") != std::string::npos,
         "manifest must record telemetry summary disabled by default");

  const auto crash_dir = root / "crashes";
  std::filesystem::create_directories(crash_dir);
  {
    std::ofstream output(crash_dir / "crash-local.json", std::ios::out | std::ios::trunc);
    output << "{\"artifact_type\":\"fallback_crash_report\","
           << "\"path\":\"" << sensitive_save_path << "\","
           << "\"token\":\"crash-secret\"}\n";
  }

  vrclient::diagnostics::DiagnosticsExportRequest aggregate_request;
  aggregate_request.output_directory = root / "exports";
  aggregate_request.log_directory = logs;
  aggregate_request.crash_directory = crash_dir;
  aggregate_request.session_id = "crash02-local-aggregate";
  aggregate_request.include_environment = false;
  aggregate_request.include_telemetry_summary = true;

  const auto aggregate =
      vrclient::diagnostics::exportDiagnosticsBundle(aggregate_request);
  expect(aggregate.success, "opt-in local telemetry aggregation should succeed");
  const auto summary_path = aggregate.bundle_directory / "telemetry" / "summary.json";
  expect(std::filesystem::exists(summary_path),
         "opt-in telemetry summary must be written locally");
  const std::string summary = readText(summary_path);
  expect(summary.find("\"summary_type\": \"vrclient_local_telemetry_aggregate\"") !=
             std::string::npos,
         "summary must declare the local aggregate type");
  expect(summary.find("\"local_only\": true") != std::string::npos,
         "summary must record local_only true");
  expect(summary.find("\"automatic_upload\": false") != std::string::npos,
         "summary must record automatic_upload false");
  expect(summary.find("\"network_upload\": false") != std::string::npos,
         "summary must record network_upload false");
  expect(summary.find("\"log_file_count\": 1") != std::string::npos,
         "summary must count local log files");
  expect(summary.find("\"log_record_count\": 1") != std::string::npos,
         "summary must count local log records");
  expect(summary.find("\"crash_artifact_count\": 1") != std::string::npos,
         "summary must count local crash artifacts");
  expect(summary.find(user_profile) == std::string::npos,
         "aggregate summary must not contain the live user profile");
  expect(summary.find("crash-secret") == std::string::npos,
         "aggregate summary must not contain crash secret values");

  const std::string aggregate_manifest =
      readText(aggregate.bundle_directory / "manifest.json");
  expect(aggregate_manifest.find("\"include_telemetry_summary\": true") !=
             std::string::npos,
         "manifest must record telemetry summary opt-in");
  expect(aggregate_manifest.find("\"automatic_upload\": false") != std::string::npos,
         "aggregate manifest must keep automatic_upload false");

  // Clean up the TEMP scratch on the success path (no residue staged anyway —
  // it lives under temp_directory_path()).
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
}

}  // namespace

int main() {
  runExportRunRealUserProfileRedaction();
  return 0;
}

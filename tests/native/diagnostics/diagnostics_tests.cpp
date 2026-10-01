#include "diagnostics/crash/crash_capture.h"
#include "diagnostics/diagnostics_profile.h"
#include "diagnostics/export/diagnostics_exporter.h"
#include "diagnostics/logging/diagnostic_logger.h"
#include "diagnostics/overlay/diagnostics_overlay.h"

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

std::filesystem::path testRoot() {
  return std::filesystem::temp_directory_path() /
      ("vrclient-diagnostics-tests-" + vrclient::diagnostics::makeSessionId());
}

void runLoggerFlushAndOverflowTest() {
  const auto root = testRoot();

  vrclient::diagnostics::SessionMetadata metadata;
  metadata.session_id = "logger-test";
  metadata.game_id = "test-game";
  metadata.build_id = "test-build";
  metadata.adapter_id = "test-adapter";
  metadata.launch_path = "C:/VRClient/vr_runtime_harness.exe";

  vrclient::diagnostics::LoggerConfig config;
  config.log_directory = root / "logs";
  config.max_buffer_records = 2;
  config.max_recent_records = 4;
  config.start_worker = false;

  vrclient::diagnostics::AsyncLogger logger;
  expect(logger.start(config, metadata), "logger should start");
  expect(logger.log(vrclient::diagnostics::Severity::Info, "first_event"),
         "first event should enqueue");
  expect(!logger.log(vrclient::diagnostics::Severity::Info, "overflow_event"),
         "bounded queue should reject overflow");
  expect(logger.droppedRecordCount() >= 1, "overflow should increment drop count");
  const auto log_path = logger.activeLogPath();
  logger.stop();

  expect(std::filesystem::exists(log_path), "log file should exist");
  const std::string log_text = readText(log_path);
  expect(log_text.find("\"event\":\"first_event\"") != std::string::npos,
         "log should contain first event");
  expect(log_text.find("\"game_id\":\"test-game\"") != std::string::npos,
         "log should contain metadata");
  expect(log_text.find("\"launch_path\":\"C:/VRClient/vr_runtime_harness.exe\"") !=
             std::string::npos,
         "log should contain launch path");
}

void runCrashArtifactTest() {
  const auto root = testRoot();

  vrclient::diagnostics::SessionMetadata metadata;
  metadata.session_id = "crash-test";
  metadata.game_id = "smoke-game";
  metadata.build_id = "smoke-build";
  metadata.adapter_id = "smoke-adapter";
  metadata.launch_path = "C:/VRClient/vr_runtime_harness.exe";

  vrclient::diagnostics::LoggerConfig log_config;
  log_config.log_directory = root / "logs";

  vrclient::diagnostics::AsyncLogger logger;
  expect(logger.start(log_config, metadata), "crash test logger should start");
  logger.log(vrclient::diagnostics::Severity::Error, "recent_error");

  vrclient::diagnostics::CrashArtifactConfig crash_config;
  crash_config.artifact_directory = root / "crashes";
  crash_config.session_id = metadata.session_id;

  vrclient::diagnostics::CrashCapture crash_capture;
  expect(crash_capture.install(crash_config, &logger), "crash capture should install");
  crash_capture.updateMetadata({
      "runtime_snapshot",
      0,
      VR_RUNTIME_STATE_RUNNING,
      metadata.game_id,
      metadata.build_id,
      metadata.adapter_id,
      metadata.launch_path,
  });

  vrclient::diagnostics::CrashArtifactMetadata crash_metadata;
  crash_metadata.reason = "forced_test_crash";
  crash_metadata.runtime_state = VR_RUNTIME_STATE_ERROR;
  const auto artifact = crash_capture.writeFinalArtifact(crash_metadata);
  logger.stop();

  expect(std::filesystem::exists(artifact), "crash artifact should exist");
  const std::string artifact_text = readText(artifact);
  expect(artifact_text.find("forced_test_crash") != std::string::npos,
         "artifact should contain crash reason");
  expect(artifact_text.find("recent_error") != std::string::npos,
         "artifact should contain recent logs");
  expect(artifact_text.find("smoke-game") != std::string::npos,
         "artifact should preserve game metadata");
  expect(artifact_text.find("vr_runtime_harness.exe") != std::string::npos,
         "artifact should preserve launch path");
}

void runOverlaySnapshotTest() {
  vrclient::diagnostics::DiagnosticsOverlay overlay;
  overlay.setEnabled(true);

  VrRuntimeFrameData frame{};
  frame.runtime_state = VR_RUNTIME_STATE_RUNNING;
  frame.timing.frame_index = 42;
  frame.timing.predicted_display_period_seconds = 0.0111;
  frame.dynamic_resolution_scale = 0.85f;
  frame.foveation_preset = VR_RUNTIME_FOVEATION_MEDIUM;

  VrRuntimeHeadsetState headset{};
  headset.session_active = 1;

  expect(overlay.updateFromFrame(frame, headset, "game", "build", "adapter"),
         "overlay should accept frame snapshot");
  expect(overlay.recordWarning("warning", "frame budget exceeded"),
         "overlay should accept warning");

  const auto lines = overlay.renderTextLines();
  expect(!lines.empty(), "overlay should render lines");
  expect(lines[0].find("running") != std::string::npos,
         "overlay should show runtime state");
  expect(lines.back().find("frame budget exceeded") != std::string::npos,
         "overlay should show recent warning");
}

void runExportRedactionTest() {
  const auto root = testRoot();
  const auto logs = root / "logs";
  std::filesystem::create_directories(logs);
  {
    std::ofstream output(logs / "session.jsonl", std::ios::out | std::ios::trunc);
    output << "path=C:\\Users\\Alice\\Saved Games token=abcd1234 "
           << "{\"api_key\":\"sekret\",\"password\":\"hunter2\"}\n";
  }

  vrclient::diagnostics::DiagnosticsExportRequest request;
  request.output_directory = root / "exports";
  request.log_directory = logs;
  request.crash_directory = root / "crashes";
  request.session_id = "export-test";
  request.include_environment = true;
  const auto result = vrclient::diagnostics::exportDiagnosticsBundle(request);
  expect(result.success, "diagnostics export should succeed");

  const std::string copied =
      readText(result.bundle_directory / "logs" / "session.jsonl");
  expect(copied.find("Alice") == std::string::npos, "export should redact user path");
  expect(copied.find("abcd1234") == std::string::npos, "export should redact token");
  expect(copied.find("sekret") == std::string::npos, "export should redact api key");
  expect(copied.find("hunter2") == std::string::npos, "export should redact password");
  expect(copied.find("\"api_key\":\"[REDACTED]\"") != std::string::npos,
         "quoted JSON secret should remain valid after redaction");
  expect(std::filesystem::exists(result.bundle_directory / "environment.json"),
         "environment file should exist when manifest includes environment");
}

void runProfileBoundsTest() {
  const auto root = testRoot();
  std::filesystem::create_directories(root);
  const auto profile_path = root / "diagnostics-profile.json";
  {
    std::ofstream output(profile_path, std::ios::out | std::ios::trunc);
    output << "{"
           << "\"version\":1,"
           << "\"enabled\":true,"
           << "\"logging\":{\"enabled\":true,\"directory\":\"logs\","
           << "\"max_buffer_records\":-1,\"max_retained_logs\":-5,"
           << "\"max_file_bytes\":-4096,\"flush_on_shutdown\":true},"
           << "\"crash_capture\":{\"enabled\":true,\"directory\":\"crashes\","
           << "\"max_recent_records\":-8},"
           << "\"overlay\":{\"enabled\":false},"
           << "\"export\":{\"directory\":\"exports\",\"include_environment\":true,"
           << "\"automatic_upload\":false}"
           << "}";
  }

  const auto loaded =
      vrclient::diagnostics::loadDiagnosticsProfileFromFile(profile_path.string().c_str());
  const auto defaults = vrclient::diagnostics::defaultDiagnosticsProfile();
  expect(loaded.loaded, "profile with invalid bounds should still load defaults");
  expect(loaded.profile.logging.max_buffer_records == defaults.logging.max_buffer_records,
         "negative logging buffer must not wrap");
  expect(loaded.profile.logging.max_retained_logs == defaults.logging.max_retained_logs,
         "negative retained count must not wrap");
  expect(loaded.profile.logging.max_file_bytes == defaults.logging.max_file_bytes,
         "negative file size must not wrap");
  expect(loaded.profile.crash.max_recent_records == defaults.crash.max_recent_records,
         "negative crash recent count must not wrap");
}

void runTelemetryAggregationProfileTest() {
  const auto root = testRoot();
  std::filesystem::create_directories(root);

  const auto enabled_path = root / "diagnostics-profile-telemetry-enabled.json";
  {
    std::ofstream output(enabled_path, std::ios::out | std::ios::trunc);
    output << "{"
           << "\"version\":1,"
           << "\"enabled\":true,"
           << "\"logging\":{\"enabled\":true,\"directory\":\"logs\","
           << "\"max_buffer_records\":64,\"max_retained_logs\":2,"
           << "\"max_file_bytes\":4096,\"flush_on_shutdown\":true},"
           << "\"crash_capture\":{\"enabled\":true,\"directory\":\"crashes\","
           << "\"max_recent_records\":8},"
           << "\"overlay\":{\"enabled\":false},"
           << "\"export\":{\"directory\":\"exports\",\"include_environment\":true,"
           << "\"automatic_upload\":false},"
           << "\"telemetry_aggregation\":{\"enabled\":true,\"local_only\":true,"
           << "\"automatic_upload\":false}"
           << "}";
  }

  const auto enabled = vrclient::diagnostics::loadDiagnosticsProfileFromFile(
      enabled_path.string().c_str());
  expect(enabled.loaded, "telemetry-enabled profile should load");
  expect(enabled.profile.telemetry_aggregation_enabled,
         "telemetry aggregation should enable only by explicit local-only opt-in");
  expect(enabled.profile.export_request.include_telemetry_summary,
         "telemetry opt-in must flow into the local export request");

  const auto unsafe_path = root / "diagnostics-profile-telemetry-unsafe.json";
  {
    std::ofstream output(unsafe_path, std::ios::out | std::ios::trunc);
    output << "{"
           << "\"version\":1,"
           << "\"enabled\":true,"
           << "\"logging\":{\"enabled\":true,\"directory\":\"logs\","
           << "\"max_buffer_records\":64,\"max_retained_logs\":2,"
           << "\"max_file_bytes\":4096,\"flush_on_shutdown\":true},"
           << "\"crash_capture\":{\"enabled\":true,\"directory\":\"crashes\","
           << "\"max_recent_records\":8},"
           << "\"overlay\":{\"enabled\":false},"
           << "\"export\":{\"directory\":\"exports\",\"include_environment\":true,"
           << "\"automatic_upload\":false},"
           << "\"telemetry_aggregation\":{\"enabled\":true,\"local_only\":false,"
           << "\"automatic_upload\":true}"
           << "}";
  }

  const auto unsafe = vrclient::diagnostics::loadDiagnosticsProfileFromFile(
      unsafe_path.string().c_str());
  expect(unsafe.loaded, "unsafe telemetry profile still loads fail-closed");
  expect(!unsafe.profile.telemetry_aggregation_enabled,
         "telemetry aggregation must fail closed when not local-only");
  expect(!unsafe.profile.export_request.include_telemetry_summary,
         "unsafe telemetry settings must not enable summary export");
}

void runJsonEscapeControlCharacterTest() {
  const std::string escaped =
      vrclient::diagnostics::jsonEscape(std::string("prefix") + char{1} + "suffix");
  expect(escaped.find("\\u0001") != std::string::npos,
         "control characters should be JSON escaped");
}

}  // namespace

int main() {
  runLoggerFlushAndOverflowTest();
  runCrashArtifactTest();
  runOverlaySnapshotTest();
  runExportRedactionTest();
  runProfileBoundsTest();
  runTelemetryAggregationProfileTest();
  runJsonEscapeControlCharacterTest();
  return 0;
}

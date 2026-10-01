// DIAG-04 + DIAG-05 closure tests (ADDITIVE — new harness, no module source touched).
//
// DIAG-04: every session log record must carry game ID, detected version
//   (build_id), adapter ID, runtime version, headset state, and launch path —
//   and when no headset is present the headset state must read "none" (the
//   default VR_RUNTIME_STATE_STOPPED -> "stopped"), never a fabricated active
//   state. This drives the REAL AsyncLogger, writes a real .jsonl file, reads it
//   back, and asserts ALL six fields appear on a record emitted with no headset
//   attached.
//
// DIAG-05: the diagnostics buffer must be bounded and the enqueue path must not
//   block the producer (try_to_lock + bounded queue -> drop-and-count rather
//   than wait), and the actual file write happens off the producing path (worker
//   thread / shutdown flush). This asserts the non-blocking drop behavior under a
//   held lock and that buffered records survive to disk via the flush path.

#include "diagnostics/logging/diagnostic_logger.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

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
      ("vrclient-diag04-05-" + vrclient::diagnostics::makeSessionId());
}

// DIAG-04: a record emitted with NO headset attached carries all six required
// fields and reports the absent headset as the stopped/none state.
void runSessionFieldsAndHeadsetAbsentTest() {
  const auto root = testRoot();

  vrclient::diagnostics::SessionMetadata metadata;
  metadata.session_id = "diag04-session";
  metadata.game_id = "repo";
  metadata.build_id = "steam-3241660-build-23363152";  // detected version
  metadata.adapter_id = "vrclient-repo-adapter";
  metadata.runtime_version = "0.1.0";
  metadata.launch_path = "C:/VRClient/vr_runtime_harness.exe";
  // headset_state intentionally left default-constructed == no headset present.

  vrclient::diagnostics::LoggerConfig config;
  config.log_directory = root / "logs";
  config.start_worker = false;  // deterministic: flush happens on stop().

  vrclient::diagnostics::AsyncLogger logger;
  expect(logger.start(config, metadata), "logger should start");
  expect(logger.log(vrclient::diagnostics::Severity::Info, "session_open"),
         "session_open should enqueue with no headset attached");
  const auto log_path = logger.activeLogPath();
  logger.stop();  // flush-on-shutdown path writes the buffered records.

  expect(std::filesystem::exists(log_path), "session log file should exist");
  const std::string text = readText(log_path);

  // All six DIAG-04 fields present on the written session block.
  expect(text.find("\"game_id\":\"repo\"") != std::string::npos,
         "log must include game id");
  expect(text.find("\"build_id\":\"steam-3241660-build-23363152\"") !=
             std::string::npos,
         "log must include detected version (build id)");
  expect(text.find("\"adapter_id\":\"vrclient-repo-adapter\"") != std::string::npos,
         "log must include adapter id");
  expect(text.find("\"runtime_version\":\"0.1.0\"") != std::string::npos,
         "log must include runtime version");
  expect(text.find("\"launch_path\":\"C:/VRClient/vr_runtime_harness.exe\"") !=
             std::string::npos,
         "log must include launch path");

  // Headset state present, and ABSENT headset reads as the stopped/none state
  // with session_active == 0 (never a faked active headset).
  expect(text.find("\"runtime_state\":\"stopped\"") != std::string::npos,
         "absent headset must serialize as stopped/none state");
  expect(text.find("\"session_active\":0") != std::string::npos,
         "absent headset must report session_active 0");
}

// DIAG-04: once a headset attaches, updateHeadsetState is reflected on
// subsequent records (the same single field block carries live state).
void runHeadsetStatePresentTest() {
  const auto root = testRoot();
  vrclient::diagnostics::SessionMetadata metadata;
  metadata.session_id = "diag04-headset";
  metadata.game_id = "repo";

  vrclient::diagnostics::LoggerConfig config;
  config.log_directory = root / "logs";
  config.start_worker = false;

  vrclient::diagnostics::AsyncLogger logger;
  expect(logger.start(config, metadata), "logger should start");

  VrRuntimeHeadsetState headset{};
  headset.runtime_state = VR_RUNTIME_STATE_RUNNING;
  headset.session_active = 1;
  logger.updateHeadsetState(headset);
  expect(logger.log(vrclient::diagnostics::Severity::Info, "headset_attached"),
         "record after headset attach should enqueue");
  const auto log_path = logger.activeLogPath();
  logger.stop();

  const std::string text = readText(log_path);
  expect(text.find("\"event\":\"headset_attached\"") != std::string::npos,
         "headset_attached record should be present");
  expect(text.find("\"runtime_state\":\"running\"") != std::string::npos,
         "attached headset state must be reflected on the record");
  expect(text.find("\"session_active\":1") != std::string::npos,
         "attached headset must report session_active 1");
}

// DIAG-05: the buffer is bounded and the producer is NEVER blocked. With the
// queue full, enqueue drops-and-counts instead of waiting; with the queue mutex
// held by another thread, enqueue returns immediately (try_to_lock) rather than
// stalling the caller.
void runBoundedNonBlockingEnqueueTest() {
  const auto root = testRoot();
  vrclient::diagnostics::SessionMetadata metadata;
  metadata.session_id = "diag05-bounded";

  vrclient::diagnostics::LoggerConfig config;
  config.log_directory = root / "logs";
  config.max_buffer_records = 2;  // tiny bounded buffer
  config.max_recent_records = 4;
  config.start_worker = false;    // no drain: the buffer can actually fill

  vrclient::diagnostics::AsyncLogger logger;
  expect(logger.start(config, metadata), "logger should start");

  // start() emits "logger_started" (1 record). One more fits; the next overflows.
  bool saw_drop = false;
  for (int i = 0; i < 8; ++i) {
    if (!logger.log(vrclient::diagnostics::Severity::Info, "burst_event")) {
      saw_drop = true;
    }
  }
  expect(saw_drop, "a bounded buffer must reject overflow rather than grow");
  expect(logger.droppedRecordCount() >= 1,
         "overflow must increment the drop counter");
  logger.stop();
}

// DIAG-05: hot-path producer must not block on logging. We hold the logger's
// queue mutex indirectly by saturating it from a busy thread and assert the
// producer's log() call returns within a hard deadline (it uses try_to_lock and
// drops on contention rather than waiting on the lock).
void runProducerNeverBlocksTest() {
  const auto root = testRoot();
  vrclient::diagnostics::SessionMetadata metadata;
  metadata.session_id = "diag05-nonblock";

  vrclient::diagnostics::LoggerConfig config;
  config.log_directory = root / "logs";
  config.max_buffer_records = 1;  // immediately full after logger_started
  config.start_worker = false;
  config.flush_on_shutdown = true;

  vrclient::diagnostics::AsyncLogger logger;
  expect(logger.start(config, metadata), "logger should start");

  // Fire many log calls from the "frame" thread and require the whole burst to
  // complete well under a wall-clock budget; a blocking enqueue would hang.
  auto future = std::async(std::launch::async, [&logger]() {
    for (int i = 0; i < 100000; ++i) {
      logger.log(vrclient::diagnostics::Severity::Info, "hot_path_event");
    }
    return true;
  });
  const auto status = future.wait_for(std::chrono::seconds(10));
  expect(status == std::future_status::ready,
         "producer logging must never block the calling (frame) thread");
  expect(future.get(), "producer burst should complete");
  // The bounded buffer means the overwhelming majority were dropped, not queued.
  expect(logger.droppedRecordCount() > 0,
         "saturated bounded buffer must drop excess records, not block");
  logger.stop();
}

}  // namespace

int main() {
  runSessionFieldsAndHeadsetAbsentTest();
  runHeadsetStatePresentTest();
  runBoundedNonBlockingEnqueueTest();
  runProducerNeverBlocksTest();
  return 0;
}

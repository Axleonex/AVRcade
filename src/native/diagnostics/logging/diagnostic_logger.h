#pragma once

#include "public/vr_runtime_api.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <initializer_list>
#include <iosfwd>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace vrclient::diagnostics {

enum class Severity {
  Trace,
  Debug,
  Info,
  Warning,
  Error,
  Critical
};

struct LogField {
  std::string key;
  std::string value;
};

struct SessionMetadata {
  std::string session_id;
  std::string game_id = "none";
  std::string build_id = "unknown";
  std::string adapter_id = "none";
  std::string runtime_version = "0.1.0";
  std::string launch_path = "unknown";
  VrRuntimeHeadsetState headset_state{};
};

struct LoggerConfig {
  bool enabled = true;
  bool start_worker = true;
  bool flush_on_shutdown = true;
  std::filesystem::path log_directory = "logs/diagnostics";
  std::string file_prefix = "vrclient-runtime";
  std::size_t max_buffer_records = 4096;
  std::size_t max_recent_records = 128;
  std::size_t max_retained_logs = 8;
  std::uintmax_t max_file_bytes = 4 * 1024 * 1024;
};

struct LogRecord {
  std::chrono::system_clock::time_point timestamp;
  Severity severity = Severity::Info;
  std::string event_name;
  std::size_t thread_id_hash = 0;
  SessionMetadata metadata;
  std::vector<LogField> fields;
};

class AsyncLogger {
 public:
  AsyncLogger() = default;
  ~AsyncLogger();

  AsyncLogger(const AsyncLogger&) = delete;
  AsyncLogger& operator=(const AsyncLogger&) = delete;

  bool start(LoggerConfig config, SessionMetadata metadata);
  void stop();

  bool log(
      Severity severity,
      std::string_view event_name,
      std::span<const LogField> fields = {});
  bool log(
      Severity severity,
      std::string_view event_name,
      std::initializer_list<LogField> fields);

  void updateSessionMetadata(const SessionMetadata& metadata);
  void updateHeadsetState(const VrRuntimeHeadsetState& headset_state);

  [[nodiscard]] bool enabled() const { return enabled_.load(); }
  [[nodiscard]] std::size_t droppedRecordCount() const {
    return dropped_records_.load();
  }
  [[nodiscard]] std::filesystem::path activeLogPath() const;
  [[nodiscard]] std::vector<LogRecord> recentRecords(std::size_t max_count) const;

 private:
  bool enqueue(LogRecord record);
  void workerLoop();
  void flushPendingRecords();
  bool openLogFile();
  void rotateLogIfNeeded();
  void pruneRetainedLogs();
  void writeRecord(const LogRecord& record);

  LoggerConfig config_;
  mutable std::mutex queue_mutex_;
  std::condition_variable queue_wake_;
  std::deque<LogRecord> queue_;
  std::deque<LogRecord> recent_;
  std::thread worker_;
  std::atomic_bool enabled_{false};
  std::atomic_bool stopping_{false};
  std::atomic_size_t dropped_records_{0};
  std::atomic_uint64_t rotation_sequence_{0};
  SessionMetadata metadata_;
  std::filesystem::path active_log_path_;
  std::ofstream* output_ = nullptr;
};

const char* severityName(Severity severity);
const char* runtimeStateName(VrRuntimeState state);
std::string makeSessionId();
std::string jsonEscape(std::string_view value);

}  // namespace vrclient::diagnostics

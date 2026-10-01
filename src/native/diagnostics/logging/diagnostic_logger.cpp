#include "diagnostics/logging/diagnostic_logger.h"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <functional>
#include <iomanip>
#include <new>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>

namespace vrclient::diagnostics {
namespace {

std::string timestampForFile(std::chrono::system_clock::time_point now) {
  const std::time_t raw = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &raw);
#else
  gmtime_r(&raw, &tm);
#endif

  std::ostringstream out;
  out << std::put_time(&tm, "%Y%m%dT%H%M%SZ");
  return out.str();
}

std::string timestampForJson(std::chrono::system_clock::time_point now) {
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) %
      1000;
  const std::time_t raw = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &raw);
#else
  gmtime_r(&raw, &tm);
#endif

  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << '.'
      << std::setw(3) << std::setfill('0') << millis.count() << 'Z';
  return out.str();
}

std::uintmax_t fileSizeOrZero(const std::filesystem::path& path) {
  std::error_code error;
  const std::uintmax_t size = std::filesystem::file_size(path, error);
  return error ? 0 : size;
}

}  // namespace

AsyncLogger::~AsyncLogger() {
  stop();
}

bool AsyncLogger::start(LoggerConfig config, SessionMetadata metadata) {
  stop();

  config_ = std::move(config);
  metadata_ = std::move(metadata);
  if (metadata_.session_id.empty()) {
    metadata_.session_id = makeSessionId();
  }
  if (config_.max_buffer_records == 0) {
    config_.max_buffer_records = 1;
  }
  if (config_.max_recent_records == 0) {
    config_.max_recent_records = 1;
  }

  if (!config_.enabled) {
    enabled_.store(false);
    return true;
  }

  enabled_.store(true);
  stopping_.store(false);
  if (!openLogFile()) {
    enabled_.store(false);
    return false;
  }

  log(Severity::Info, "logger_started", {
      {"log_path", active_log_path_.string()},
      {"buffer_records", std::to_string(config_.max_buffer_records)},
  });

  if (config_.start_worker) {
    worker_ = std::thread([this]() { workerLoop(); });
  }

  return true;
}

void AsyncLogger::stop() {
  if (!enabled_.load() && output_ == nullptr) {
    return;
  }

  if (enabled_.load()) {
    log(Severity::Info, "logger_stopping", {
        {"dropped_records", std::to_string(dropped_records_.load())},
    });
  }

  stopping_.store(true);
  queue_wake_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }

  if (config_.flush_on_shutdown) {
    flushPendingRecords();
  }

  if (output_ != nullptr) {
    output_->flush();
    delete output_;
    output_ = nullptr;
  }
  enabled_.store(false);
}

bool AsyncLogger::log(
    Severity severity,
    std::string_view event_name,
    std::span<const LogField> fields) {
  if (!enabled_.load() || event_name.empty()) {
    return false;
  }

  LogRecord record;
  record.timestamp = std::chrono::system_clock::now();
  record.severity = severity;
  record.event_name.assign(event_name.begin(), event_name.end());
  record.thread_id_hash = std::hash<std::thread::id>{}(std::this_thread::get_id());
  record.fields.assign(fields.begin(), fields.end());

  std::unique_lock lock(queue_mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    dropped_records_.fetch_add(1);
    return false;
  }

  if (queue_.size() >= config_.max_buffer_records) {
    dropped_records_.fetch_add(1);
    return false;
  }

  record.metadata = metadata_;
  recent_.push_back(record);
  while (recent_.size() > config_.max_recent_records) {
    recent_.pop_front();
  }

  queue_.push_back(std::move(record));
  lock.unlock();
  queue_wake_.notify_one();
  return true;
}

bool AsyncLogger::log(
    Severity severity,
    std::string_view event_name,
    std::initializer_list<LogField> fields) {
  return log(severity, event_name, std::span<const LogField>(fields.begin(), fields.size()));
}

void AsyncLogger::updateSessionMetadata(const SessionMetadata& metadata) {
  std::unique_lock lock(queue_mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    return;
  }
  metadata_ = metadata;
  if (metadata_.session_id.empty()) {
    metadata_.session_id = makeSessionId();
  }
}

void AsyncLogger::updateHeadsetState(const VrRuntimeHeadsetState& headset_state) {
  std::unique_lock lock(queue_mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    return;
  }
  metadata_.headset_state = headset_state;
}

std::filesystem::path AsyncLogger::activeLogPath() const {
  std::lock_guard lock(queue_mutex_);
  return active_log_path_;
}

std::vector<LogRecord> AsyncLogger::recentRecords(std::size_t max_count) const {
  std::lock_guard lock(queue_mutex_);
  std::vector<LogRecord> records;
  const std::size_t count = std::min(max_count, recent_.size());
  records.reserve(count);
  const auto begin = recent_.end() - static_cast<std::ptrdiff_t>(count);
  records.assign(begin, recent_.end());
  return records;
}

bool AsyncLogger::enqueue(LogRecord record) {
  std::unique_lock lock(queue_mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    dropped_records_.fetch_add(1);
    return false;
  }

  if (queue_.size() >= config_.max_buffer_records) {
    dropped_records_.fetch_add(1);
    return false;
  }

  recent_.push_back(record);
  while (recent_.size() > config_.max_recent_records) {
    recent_.pop_front();
  }

  queue_.push_back(std::move(record));
  lock.unlock();
  queue_wake_.notify_one();
  return true;
}

void AsyncLogger::workerLoop() {
  while (!stopping_.load()) {
    {
      std::unique_lock lock(queue_mutex_);
      queue_wake_.wait_for(lock, std::chrono::milliseconds(100), [this]() {
        return stopping_.load() || !queue_.empty();
      });
    }
    flushPendingRecords();
  }
  flushPendingRecords();
}

void AsyncLogger::flushPendingRecords() {
  std::deque<LogRecord> local;
  {
    std::lock_guard lock(queue_mutex_);
    local.swap(queue_);
  }

  for (const LogRecord& record : local) {
    writeRecord(record);
    rotateLogIfNeeded();
  }
  if (output_ != nullptr) {
    output_->flush();
  }
}

bool AsyncLogger::openLogFile() {
  std::error_code error;
  std::filesystem::create_directories(config_.log_directory, error);
  if (error) {
    return false;
  }

  const std::uint64_t sequence = rotation_sequence_.fetch_add(1);
  active_log_path_ = config_.log_directory /
      (config_.file_prefix + "-" + timestampForFile(std::chrono::system_clock::now()) +
       "-" + metadata_.session_id + "-" + std::to_string(sequence) + ".jsonl");

  delete output_;
  output_ = new (std::nothrow) std::ofstream(active_log_path_, std::ios::out | std::ios::app);
  if (output_ == nullptr || !*output_) {
    delete output_;
    output_ = nullptr;
    return false;
  }

  pruneRetainedLogs();
  return true;
}

void AsyncLogger::rotateLogIfNeeded() {
  if (output_ == nullptr || config_.max_file_bytes == 0) {
    return;
  }
  output_->flush();
  if (fileSizeOrZero(active_log_path_) < config_.max_file_bytes) {
    return;
  }
  delete output_;
  output_ = nullptr;
  openLogFile();
}

void AsyncLogger::pruneRetainedLogs() {
  if (config_.max_retained_logs == 0) {
    return;
  }

  std::vector<std::filesystem::directory_entry> candidates;
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(config_.log_directory, error)) {
    if (error) {
      return;
    }
    if (entry.is_regular_file() && entry.path().extension() == ".jsonl" &&
        entry.path().filename().string().find(config_.file_prefix) == 0) {
      candidates.push_back(entry);
    }
  }

  if (candidates.size() <= config_.max_retained_logs) {
    return;
  }

  std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
    std::error_code left_error;
    std::error_code right_error;
    return left.last_write_time(left_error) < right.last_write_time(right_error);
  });

  const std::size_t remove_count = candidates.size() - config_.max_retained_logs;
  for (std::size_t i = 0; i < remove_count; ++i) {
    std::filesystem::remove(candidates[i].path(), error);
  }
}

void AsyncLogger::writeRecord(const LogRecord& record) {
  if (output_ == nullptr || !*output_) {
    return;
  }

  const auto& meta = record.metadata;
  const auto& headset = meta.headset_state;
  *output_ << "{\"timestamp_utc\":\"" << timestampForJson(record.timestamp)
           << "\",\"severity\":\"" << severityName(record.severity)
           << "\",\"event\":\"" << jsonEscape(record.event_name)
           << "\",\"thread_id_hash\":" << record.thread_id_hash
           << ",\"session\":{\"session_id\":\"" << jsonEscape(meta.session_id)
           << "\",\"game_id\":\"" << jsonEscape(meta.game_id)
           << "\",\"build_id\":\"" << jsonEscape(meta.build_id)
           << "\",\"adapter_id\":\"" << jsonEscape(meta.adapter_id)
           << "\",\"runtime_version\":\"" << jsonEscape(meta.runtime_version)
           << "\",\"launch_path\":\"" << jsonEscape(meta.launch_path)
           << "\"},\"headset\":{\"runtime_state\":\""
           << runtimeStateName(headset.runtime_state)
           << "\",\"session_active\":" << headset.session_active
           << ",\"view_count\":" << headset.view_count
           << ",\"refresh_hz\":" << headset.current_refresh_hz
           << "},\"fields\":{";

  for (std::size_t i = 0; i < record.fields.size(); ++i) {
    if (i != 0) {
      *output_ << ',';
    }
    *output_ << "\"" << jsonEscape(record.fields[i].key) << "\":\""
             << jsonEscape(record.fields[i].value) << "\"";
  }

  *output_ << "}}\n";
}

const char* severityName(Severity severity) {
  switch (severity) {
    case Severity::Trace:
      return "trace";
    case Severity::Debug:
      return "debug";
    case Severity::Info:
      return "info";
    case Severity::Warning:
      return "warning";
    case Severity::Error:
      return "error";
    case Severity::Critical:
      return "critical";
  }
  return "info";
}

const char* runtimeStateName(VrRuntimeState state) {
  switch (state) {
    case VR_RUNTIME_STATE_STOPPED:
      return "stopped";
    case VR_RUNTIME_STATE_INITIALIZING:
      return "initializing";
    case VR_RUNTIME_STATE_READY:
      return "ready";
    case VR_RUNTIME_STATE_RUNNING:
      return "running";
    case VR_RUNTIME_STATE_DEGRADED:
      return "degraded";
    case VR_RUNTIME_STATE_LOSS_PENDING:
      return "loss_pending";
    case VR_RUNTIME_STATE_EXITING:
      return "exiting";
    case VR_RUNTIME_STATE_ERROR:
      return "error";
  }
  return "unknown";
}

std::string makeSessionId() {
  static std::atomic_uint64_t counter{0};
  return timestampForFile(std::chrono::system_clock::now()) + "-" +
      std::to_string(counter.fetch_add(1));
}

std::string jsonEscape(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size() + 8);
  for (const unsigned char ch : value) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        if (ch < 0x20) {
          std::ostringstream code;
          code << "\\u" << std::hex << std::setw(4) << std::setfill('0')
               << static_cast<int>(ch);
          escaped += code.str();
        } else {
          escaped += static_cast<char>(ch);
        }
        break;
    }
  }
  return escaped;
}

}  // namespace vrclient::diagnostics

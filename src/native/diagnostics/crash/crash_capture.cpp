#include "diagnostics/crash/crash_capture.h"

// crash_capture.h pulls in <windows.h> on Windows, which is the prerequisite
// for <dbghelp.h>. Keep dbghelp.h inside the _WIN32 guard and AFTER the project
// header so the non-Windows build never sees it.
#if defined(_WIN32)
#  include <dbghelp.h>
#endif

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

namespace vrclient::diagnostics {
namespace {

std::string crashTimestamp() {
  const auto now = std::chrono::system_clock::now();
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

void writeRecentRecordsJson(std::ofstream& output, const std::vector<LogRecord>& records) {
  output << '[';
  for (std::size_t i = 0; i < records.size(); ++i) {
    if (i != 0) {
      output << ',';
    }
    output << "{\"severity\":\"" << severityName(records[i].severity)
           << "\",\"event\":\"" << jsonEscape(records[i].event_name) << "\"}";
  }
  output << ']';
}

}  // namespace

CrashCapture* CrashCapture::instance_ = nullptr;

CrashCapture::~CrashCapture() {
  uninstall();
}

bool CrashCapture::install(CrashArtifactConfig config, AsyncLogger* logger) {
  uninstall();

  config_ = std::move(config);
  logger_ = logger;
  if (config_.session_id.empty()) {
    config_.session_id = makeSessionId();
  }
  if (!config_.enabled) {
    return true;
  }

  std::error_code error;
  std::filesystem::create_directories(config_.artifact_directory, error);
  if (error) {
    if (logger_ != nullptr) {
      logger_->log(Severity::Warning, "crash_capture_unavailable", {
          {"reason", error.message()},
      });
    }
    return false;
  }

  instance_ = this;
#if defined(_WIN32)
  // Resolve MiniDumpWriteDump NOW (startup), not mid-crash, so the last-chance
  // filter never takes the loader lock via LoadLibrary. Dbghelp.dll is a core
  // system DLL; we keep it resident for the process lifetime (no FreeLibrary).
  HMODULE dbghelp = GetModuleHandleW(L"Dbghelp.dll");
  if (dbghelp == nullptr) {
    dbghelp = LoadLibraryW(L"Dbghelp.dll");
  }
  minidump_write_dump_ =
      dbghelp != nullptr ? GetProcAddress(dbghelp, "MiniDumpWriteDump") : nullptr;
  previous_filter_ = SetUnhandledExceptionFilter(&CrashCapture::handleUnhandledException);
#endif
  installed_.store(true);
  if (logger_ != nullptr) {
    logger_->log(Severity::Info, "crash_capture_installed", {
        {"artifact_directory", config_.artifact_directory.string()},
    });
  }
  return true;
}

void CrashCapture::uninstall() {
  if (!installed_.load()) {
    return;
  }
#if defined(_WIN32)
  SetUnhandledExceptionFilter(previous_filter_);
  previous_filter_ = nullptr;
#endif
  installed_.store(false);
  if (instance_ == this) {
    instance_ = nullptr;
  }
}

void CrashCapture::updateMetadata(const CrashArtifactMetadata& metadata) {
  std::lock_guard lock(mutex_);
  last_metadata_ = metadata;
}

std::filesystem::path CrashCapture::writeFinalArtifact(
    const CrashArtifactMetadata& metadata) {
  if (!config_.enabled || writing_.exchange(true)) {
    return {};
  }

  std::lock_guard lock(mutex_);
  CrashArtifactMetadata merged = last_metadata_;
  merged.reason = metadata.reason;
  merged.exception_code = metadata.exception_code;
  if (metadata.runtime_state != VR_RUNTIME_STATE_STOPPED ||
      merged.runtime_state == VR_RUNTIME_STATE_STOPPED) {
    merged.runtime_state = metadata.runtime_state;
  }
  if (metadata.game_id != "none") {
    merged.game_id = metadata.game_id;
  }
  if (metadata.build_id != "unknown") {
    merged.build_id = metadata.build_id;
  }
  if (metadata.adapter_id != "none") {
    merged.adapter_id = metadata.adapter_id;
  }
  if (metadata.launch_path != "unknown") {
    merged.launch_path = metadata.launch_path;
  }
  std::filesystem::path path = writeArtifact(merged, true, true);
  writing_.store(false);
  return path;
}

std::filesystem::path CrashCapture::lastArtifactPath() const {
  std::lock_guard lock(mutex_);
  return last_artifact_path_;
}

std::filesystem::path CrashCapture::writeArtifact(
    const CrashArtifactMetadata& metadata,
    bool include_recent_records,
    bool log_write_result) {
  std::error_code error;
  std::filesystem::create_directories(config_.artifact_directory, error);
  if (error) {
    if (log_write_result && logger_ != nullptr) {
      logger_->log(Severity::Error, "crash_artifact_write_failed", {
          {"reason", error.message()},
      });
    }
    return {};
  }

  const std::filesystem::path artifact_path =
      config_.artifact_directory /
      ("crash-" + crashTimestamp() + "-" + config_.session_id + ".json");
  std::ofstream output(artifact_path, std::ios::out | std::ios::trunc);
  if (!output) {
    if (log_write_result && logger_ != nullptr) {
      logger_->log(Severity::Error, "crash_artifact_write_failed", {
          {"reason", "could not open artifact path"},
      });
    }
    return {};
  }

  output << "{\n"
         << "  \"artifact_type\": \"fallback_crash_report\",\n"
         << "  \"session_id\": \"" << jsonEscape(config_.session_id) << "\",\n"
         << "  \"reason\": \"" << jsonEscape(metadata.reason) << "\",\n"
         << "  \"exception_code\": " << metadata.exception_code << ",\n"
         << "  \"runtime_state\": \"" << runtimeStateName(metadata.runtime_state) << "\",\n"
         << "  \"game_id\": \"" << jsonEscape(metadata.game_id) << "\",\n"
         << "  \"build_id\": \"" << jsonEscape(metadata.build_id) << "\",\n"
         << "  \"adapter_id\": \"" << jsonEscape(metadata.adapter_id) << "\",\n"
         << "  \"launch_path\": \"" << jsonEscape(metadata.launch_path) << "\",\n"
         << "  \"recent_log_records\": ";

  if (include_recent_records && logger_ != nullptr) {
    writeRecentRecordsJson(output, logger_->recentRecords(config_.max_recent_records));
  } else {
    output << "[]";
  }

  output << "\n}\n";
  output.flush();
  if (log_write_result || include_recent_records) {
    last_artifact_path_ = artifact_path;
  }

  if (log_write_result && logger_ != nullptr) {
    logger_->log(Severity::Critical, "crash_artifact_written", {
        {"path", artifact_path.string()},
        {"reason", metadata.reason},
    });
  }
  return artifact_path;
}

std::filesystem::path CrashCapture::writeUnhandledExceptionArtifact(
    std::int32_t exception_code) {
  if (!config_.enabled || writing_.exchange(true)) {
    return {};
  }

  CrashArtifactMetadata metadata = last_metadata_;
  metadata.reason = "unhandled_exception";
  metadata.exception_code = exception_code;
  const std::filesystem::path path = writeArtifact(metadata, false, false);
  writing_.store(false);
  return path;
}

#if defined(_WIN32)
// B7/CRASH-01: write a real Windows minidump from inside the last-chance SEH
// filter, using the live EXCEPTION_POINTERS the OS handed us.
//
// dbghelp acquisition: MiniDumpWriteDump is resolved ONCE at install() (startup,
// where the loader lock is safe) into minidump_write_dump_ via GetModuleHandleW
// (Dbghelp.dll is a core system DLL) with a LoadLibraryW fallback, then
// GetProcAddress. The last-chance filter therefore NEVER calls LoadLibrary
// mid-crash (loader-lock-safe). This also keeps vr_diagnostics' CMake link list
// untouched (no link-time dbghelp import). If resolution failed (or dbghelp is
// unavailable) writeMinidump returns {} and the proven fallback JSON still runs.
//
// Crash-safety: only Win32 syscalls + the path string (built exactly like the
// existing fallback JSON path). No logger, no std::ofstream, no C++ exceptions.
std::filesystem::path CrashCapture::writeMinidump(EXCEPTION_POINTERS* exception_info) {
  if (!config_.enabled || exception_info == nullptr) {
    return {};
  }

  std::error_code error;
  std::filesystem::create_directories(config_.artifact_directory, error);
  // Best-effort: even if create_directories reports an error, the dir may
  // already exist; attempt the write anyway and let CreateFileW be the gate.

  const std::filesystem::path dump_path =
      config_.artifact_directory /
      ("crash-" + crashTimestamp() + "-" + config_.session_id + ".dmp");

  // path::c_str() is wchar_t* on Windows -> matches the wide CreateFileW.
  HANDLE file = CreateFileW(dump_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return {};
  }

  // MiniDumpWriteDump was resolved at install() (startup) -> no LoadLibrary
  // mid-crash. If it was unavailable, fall through so the JSON still writes.
  if (minidump_write_dump_ == nullptr) {
    CloseHandle(file);
    return {};
  }
  auto write_dump =
      reinterpret_cast<decltype(&MiniDumpWriteDump)>(minidump_write_dump_);

  MINIDUMP_EXCEPTION_INFORMATION mei{};
  mei.ThreadId = GetCurrentThreadId();
  mei.ExceptionPointers = exception_info;
  mei.ClientPointers = FALSE;  // EXCEPTION_POINTERS live in THIS process.

  // Keep it small + robust: MiniDumpNormal (faulting-thread stacks + module
  // list) is enough for a usable crash callstack and walks the least process
  // state mid-crash. MiniDumpWithDataSegs adds globals at low cost.
  const MINIDUMP_TYPE dump_type = static_cast<MINIDUMP_TYPE>(
      MiniDumpNormal | MiniDumpWithDataSegs);

  const BOOL ok = write_dump(GetCurrentProcess(), GetCurrentProcessId(), file,
                             dump_type, &mei, nullptr, nullptr);
  CloseHandle(file);

  if (ok == FALSE) {
    // Remove the partial/empty file so a failed dump never masquerades as a
    // valid one; the fallback JSON remains the source of truth.
    DeleteFileW(dump_path.c_str());
    return {};
  }
  return dump_path;
}

LONG WINAPI CrashCapture::handleUnhandledException(EXCEPTION_POINTERS* exception_info) {
  if (instance_ != nullptr) {
    std::int32_t exception_code = 0;
    if (exception_info != nullptr && exception_info->ExceptionRecord != nullptr) {
      exception_code =
          static_cast<std::int32_t>(exception_info->ExceptionRecord->ExceptionCode);
    }
    // B7/CRASH-01: write the real .dmp FIRST (additive). Even if it fails, the
    // proven fallback JSON write below still runs unconditionally, so the
    // existing artifact path and CTest assertions never regress.
    instance_->writeMinidump(exception_info);
    instance_->writeUnhandledExceptionArtifact(exception_code);
  }
  return EXCEPTION_EXECUTE_HANDLER;
}
#endif

}  // namespace vrclient::diagnostics

#pragma once

#include "diagnostics/logging/diagnostic_logger.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

#if defined(_WIN32)
#  include <windows.h>
#endif

namespace vrclient::diagnostics {

struct CrashArtifactConfig {
  bool enabled = true;
  std::filesystem::path artifact_directory = "logs/crashes";
  std::string session_id;
  std::size_t max_recent_records = 32;
};

struct CrashArtifactMetadata {
  std::string reason = "unknown";
  std::int32_t exception_code = 0;
  VrRuntimeState runtime_state = VR_RUNTIME_STATE_STOPPED;
  std::string game_id = "none";
  std::string build_id = "unknown";
  std::string adapter_id = "none";
  std::string launch_path = "unknown";
};

class CrashCapture {
 public:
  CrashCapture() = default;
  ~CrashCapture();

  CrashCapture(const CrashCapture&) = delete;
  CrashCapture& operator=(const CrashCapture&) = delete;

  bool install(CrashArtifactConfig config, AsyncLogger* logger);
  void uninstall();

  void updateMetadata(const CrashArtifactMetadata& metadata);
  std::filesystem::path writeFinalArtifact(const CrashArtifactMetadata& metadata);
  [[nodiscard]] std::filesystem::path lastArtifactPath() const;

 private:
#if defined(_WIN32)
  static LONG WINAPI handleUnhandledException(EXCEPTION_POINTERS* exception_info);
  LPTOP_LEVEL_EXCEPTION_FILTER previous_filter_ = nullptr;

  // B7/CRASH-01: write a real OS minidump (.dmp) from inside the last-chance
  // SEH filter using the live EXCEPTION_POINTERS. Crash-safe (Win32 syscalls
  // only). Returns the .dmp path on success, {} on any failure so the proven
  // fallback JSON write still runs. MiniDumpWriteDump is resolved ONCE at
  // install() (startup) into minidump_write_dump_, so the filter never calls
  // LoadLibrary mid-crash (loader-lock-safe).
  FARPROC minidump_write_dump_ = nullptr;
  std::filesystem::path writeMinidump(EXCEPTION_POINTERS* exception_info);
#endif

  static CrashCapture* instance_;

  std::filesystem::path writeArtifact(
      const CrashArtifactMetadata& metadata,
      bool include_recent_records,
      bool log_write_result);
  std::filesystem::path writeUnhandledExceptionArtifact(std::int32_t exception_code);

  CrashArtifactConfig config_;
  AsyncLogger* logger_ = nullptr;
  mutable std::mutex mutex_;
  std::filesystem::path last_artifact_path_;
  CrashArtifactMetadata last_metadata_;
  std::atomic_bool installed_{false};
  std::atomic_bool writing_{false};
};

}  // namespace vrclient::diagnostics

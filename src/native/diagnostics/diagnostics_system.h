#pragma once

#include "diagnostics/crash/crash_capture.h"
#include "diagnostics/diagnostics_profile.h"
#include "diagnostics/logging/diagnostic_logger.h"
#include "diagnostics/overlay/diagnostics_overlay.h"

#include <string>
#include <string_view>

namespace vrclient::diagnostics {

class DiagnosticsSystem {
 public:
  DiagnosticsSystem() = default;
  ~DiagnosticsSystem();

  DiagnosticsSystem(const DiagnosticsSystem&) = delete;
  DiagnosticsSystem& operator=(const DiagnosticsSystem&) = delete;

  bool initialize(std::string application_name, const char* diagnostics_profile_path);
  void shutdown();

  void setRuntimeIds(std::string game_id, std::string build_id, std::string adapter_id);
  void updateHeadsetState(const VrRuntimeHeadsetState& headset_state);
  void updateFrameSnapshot(
      const VrRuntimeFrameData& frame,
      const VrRuntimeHeadsetState& headset_state);

  void logStateTransition(
      VrRuntimeState previous,
      VrRuntimeState next,
      int32_t detail_code);
  void logRuntimeEvent(Severity severity, std::string_view event_name);
  void logRuntimeError(
      std::string_view event_name,
      VrRuntimeResult result,
      int32_t detail_code);

  [[nodiscard]] AsyncLogger& logger() { return logger_; }
  [[nodiscard]] CrashCapture& crashCapture() { return crash_capture_; }
  [[nodiscard]] DiagnosticsOverlay& overlay() { return overlay_; }
  [[nodiscard]] const SessionMetadata& metadata() const { return metadata_; }

 private:
  void syncMetadata();

  DiagnosticsProfile profile_;
  SessionMetadata metadata_;
  AsyncLogger logger_;
  CrashCapture crash_capture_;
  DiagnosticsOverlay overlay_;
  bool initialized_ = false;
};

}  // namespace vrclient::diagnostics

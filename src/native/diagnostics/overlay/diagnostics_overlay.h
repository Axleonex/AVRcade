#pragma once

#include "public/vr_runtime_api.h"

#include <array>
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace vrclient::diagnostics {

struct OverlayLine {
  std::string severity;
  std::string text;
};

struct DiagnosticsOverlaySnapshot {
  bool enabled = false;
  VrRuntimeState runtime_state = VR_RUNTIME_STATE_STOPPED;
  std::string openxr_session_state = "unknown";
  std::string game_id = "none";
  std::string build_id = "unknown";
  std::string adapter_id = "none";
  std::uint64_t frame_index = 0;
  double frame_time_ms = 0.0;
  float dynamic_resolution_scale = 1.0f;
  VrRuntimeFoveationPreset foveation_preset = VR_RUNTIME_FOVEATION_OFF;
  std::array<OverlayLine, 6> recent_warnings{};
  std::size_t recent_warning_count = 0;
};

class DiagnosticsOverlay {
 public:
  void setEnabled(bool enabled);
  [[nodiscard]] bool enabled() const;

  bool updateFromFrame(
      const VrRuntimeFrameData& frame,
      const VrRuntimeHeadsetState& headset,
      std::string game_id,
      std::string build_id,
      std::string adapter_id);
  bool recordWarning(std::string severity, std::string text);

  [[nodiscard]] DiagnosticsOverlaySnapshot snapshot() const;
  [[nodiscard]] std::vector<std::string> renderTextLines() const;

 private:
  mutable std::mutex mutex_;
  DiagnosticsOverlaySnapshot snapshot_;
};

}  // namespace vrclient::diagnostics

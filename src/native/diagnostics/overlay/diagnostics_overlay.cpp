#include "diagnostics/overlay/diagnostics_overlay.h"

#include "diagnostics/logging/diagnostic_logger.h"

#include <sstream>
#include <utility>

namespace vrclient::diagnostics {
namespace {

const char* overlayFoveationPresetName(VrRuntimeFoveationPreset preset) {
  switch (preset) {
    case VR_RUNTIME_FOVEATION_OFF:
      return "off";
    case VR_RUNTIME_FOVEATION_LOW:
      return "low";
    case VR_RUNTIME_FOVEATION_MEDIUM:
      return "medium";
    case VR_RUNTIME_FOVEATION_HIGH:
      return "high";
  }
  return "off";
}

}  // namespace

void DiagnosticsOverlay::setEnabled(bool enabled) {
  std::lock_guard lock(mutex_);
  snapshot_.enabled = enabled;
}

bool DiagnosticsOverlay::enabled() const {
  std::lock_guard lock(mutex_);
  return snapshot_.enabled;
}

bool DiagnosticsOverlay::updateFromFrame(
    const VrRuntimeFrameData& frame,
    const VrRuntimeHeadsetState& headset,
    std::string game_id,
    std::string build_id,
    std::string adapter_id) {
  std::unique_lock lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    return false;
  }

  snapshot_.runtime_state = frame.runtime_state;
  snapshot_.openxr_session_state = headset.session_active != 0 ? "active" : "inactive";
  snapshot_.game_id = std::move(game_id);
  snapshot_.build_id = std::move(build_id);
  snapshot_.adapter_id = std::move(adapter_id);
  snapshot_.frame_index = frame.timing.frame_index;
  snapshot_.frame_time_ms = frame.timing.predicted_display_period_seconds * 1000.0;
  snapshot_.dynamic_resolution_scale = frame.dynamic_resolution_scale;
  snapshot_.foveation_preset = frame.foveation_preset;
  return true;
}

bool DiagnosticsOverlay::recordWarning(std::string severity, std::string text) {
  std::unique_lock lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    return false;
  }

  if (snapshot_.recent_warning_count < snapshot_.recent_warnings.size()) {
    snapshot_.recent_warnings[snapshot_.recent_warning_count++] = {
        std::move(severity),
        std::move(text),
    };
  } else {
    for (std::size_t i = 1; i < snapshot_.recent_warnings.size(); ++i) {
      snapshot_.recent_warnings[i - 1] = std::move(snapshot_.recent_warnings[i]);
    }
    snapshot_.recent_warnings.back() = {std::move(severity), std::move(text)};
  }
  return true;
}

DiagnosticsOverlaySnapshot DiagnosticsOverlay::snapshot() const {
  std::lock_guard lock(mutex_);
  return snapshot_;
}

std::vector<std::string> DiagnosticsOverlay::renderTextLines() const {
  const DiagnosticsOverlaySnapshot copy = snapshot();
  if (!copy.enabled) {
    return {};
  }

  std::vector<std::string> lines;
  lines.reserve(5 + copy.recent_warning_count);
  lines.push_back("Runtime: " + std::string(runtimeStateName(copy.runtime_state)));
  lines.push_back("OpenXR session: " + copy.openxr_session_state);
  lines.push_back("Game/build/adapter: " + copy.game_id + " / " + copy.build_id +
                  " / " + copy.adapter_id);

  std::ostringstream frame;
  frame << "Frame: " << copy.frame_index << "  scale: "
        << copy.dynamic_resolution_scale << "  foveation: "
        << overlayFoveationPresetName(copy.foveation_preset);
  lines.push_back(frame.str());

  for (std::size_t i = 0; i < copy.recent_warning_count; ++i) {
    lines.push_back(copy.recent_warnings[i].severity + ": " +
                    copy.recent_warnings[i].text);
  }
  return lines;
}

}  // namespace vrclient::diagnostics

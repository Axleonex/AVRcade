#include "perf/dynamic_resolution.h"

#include <algorithm>

namespace vrclient::runtime::perf {

void DynamicResolutionController::reset(const DynamicResolutionProfile& profile) {
  profile_ = profile;
  current_scale_ = std::clamp(profile.initial_scale, profile.min_scale, profile.max_scale);
  stable_frames_ = 0;
  missed_frames_ = 0;
}

void DynamicResolutionController::recordFrame(float cpu_ms, float gpu_ms, bool missed_frame) {
  if (!profile_.enabled) {
    current_scale_ = 1.0f;
    stable_frames_ = 0;
    missed_frames_ = 0;
    return;
  }

  const float worst_ms = std::max(cpu_ms, gpu_ms);
  const bool over_budget = worst_ms > profile_.frame_time_budget_ms;

  if (missed_frame || over_budget) {
    ++missed_frames_;
    stable_frames_ = 0;
    if (missed_frames_ >= 2) {
      current_scale_ = std::max(profile_.min_scale, current_scale_ - profile_.step_scale);
      missed_frames_ = 0;
    }
    return;
  }

  missed_frames_ = 0;
  ++stable_frames_;
  if (stable_frames_ >= 45) {
    current_scale_ = std::min(profile_.max_scale, current_scale_ + profile_.step_scale);
    stable_frames_ = 0;
  }
}

}  // namespace vrclient::runtime::perf

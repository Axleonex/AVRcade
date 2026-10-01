#include "perf/frame_pacing.h"

namespace vrclient::runtime::perf {

void FramePacingGuardrails::reset(const FramePacingProfile& profile) {
  profile_ = profile;
  missed_frame_streak_ = 0;
  recovery_frame_count_ = 0;
}

bool FramePacingGuardrails::shouldEnterDegradedState(
    float cpu_ms,
    float gpu_ms,
    bool missed_frame) {
  const bool over_guard =
      cpu_ms > profile_.cpu_guard_ms || gpu_ms > profile_.gpu_guard_ms;

  if (missed_frame || over_guard) {
    ++missed_frame_streak_;
    recovery_frame_count_ = 0;
    return missed_frame_streak_ > profile_.max_missed_frames;
  }

  missed_frame_streak_ = 0;
  ++recovery_frame_count_;
  return false;
}

bool FramePacingGuardrails::shouldRecover() const {
  return recovery_frame_count_ >= profile_.recovery_frames;
}

}  // namespace vrclient::runtime::perf

#pragma once

#include "perf/runtime_profile.h"

namespace vrclient::runtime::perf {

class FramePacingGuardrails {
 public:
  void reset(const FramePacingProfile& profile);
  bool shouldEnterDegradedState(float cpu_ms, float gpu_ms, bool missed_frame);
  bool shouldRecover() const;

 private:
  FramePacingProfile profile_;
  int missed_frame_streak_ = 0;
  int recovery_frame_count_ = 0;
};

}  // namespace vrclient::runtime::perf

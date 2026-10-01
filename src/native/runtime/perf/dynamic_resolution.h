#pragma once

#include "perf/runtime_profile.h"

namespace vrclient::runtime::perf {

class DynamicResolutionController {
 public:
  void reset(const DynamicResolutionProfile& profile);

  float scale() const { return current_scale_; }

  void recordFrame(float cpu_ms, float gpu_ms, bool missed_frame);

 private:
  DynamicResolutionProfile profile_;
  float current_scale_ = 1.0f;
  int stable_frames_ = 0;
  int missed_frames_ = 0;
};

}  // namespace vrclient::runtime::perf

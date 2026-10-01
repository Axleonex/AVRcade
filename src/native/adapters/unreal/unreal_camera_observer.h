#pragma once

#include "adapters/unreal/unreal_camera_contract.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace vrclient::adapters::unreal {

enum class UnrealCameraObservationResult {
  Ready,
  Pending,
  Missing,
  Ambiguous,
  InvalidSample,
  NonMonotonic,
  InvalidConfiguration,
};

struct UnrealCameraObservationSnapshot {
  UnrealCameraSample sample{};
  std::size_t consecutive_sample_count = 0;
  bool ready = false;
};

class UnrealCameraObserver {
 public:
  UnrealCameraObserver(
      std::size_t required_consecutive_samples,
      std::int64_t maximum_sample_age_ns);

  UnrealCameraObservationResult observe(
      const std::vector<UnrealCameraSample>& candidates,
      std::int64_t predicted_display_time_ns);
  UnrealCameraObservationSnapshot snapshot() const;
  void reset();

 private:
  void resetLocked();

  const std::size_t required_consecutive_samples_;
  const std::int64_t maximum_sample_age_ns_;
  mutable std::mutex mutex_;
  UnrealCameraObservationSnapshot snapshot_{};
};

}  // namespace vrclient::adapters::unreal

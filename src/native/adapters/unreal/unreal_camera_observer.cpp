#include "adapters/unreal/unreal_camera_observer.h"

namespace vrclient::adapters::unreal {

UnrealCameraObserver::UnrealCameraObserver(
    std::size_t required_consecutive_samples,
    std::int64_t maximum_sample_age_ns)
    : required_consecutive_samples_(required_consecutive_samples),
      maximum_sample_age_ns_(maximum_sample_age_ns) {}

UnrealCameraObservationResult UnrealCameraObserver::observe(
    const std::vector<UnrealCameraSample>& candidates,
    std::int64_t predicted_display_time_ns) {
  std::scoped_lock lock(mutex_);
  if (required_consecutive_samples_ == 0 || maximum_sample_age_ns_ < 0) {
    resetLocked();
    return UnrealCameraObservationResult::InvalidConfiguration;
  }

  const UnrealCameraSample* active_candidate = nullptr;
  for (const auto& candidate : candidates) {
    if (!candidate.active) {
      continue;
    }
    if (validateUnrealCameraSample(
            candidate,
            predicted_display_time_ns,
            maximum_sample_age_ns_) != UnrealCameraSampleResult::Ready) {
      resetLocked();
      return UnrealCameraObservationResult::InvalidSample;
    }
    if (active_candidate != nullptr) {
      resetLocked();
      return UnrealCameraObservationResult::Ambiguous;
    }
    active_candidate = &candidate;
  }
  if (active_candidate == nullptr) {
    resetLocked();
    return UnrealCameraObservationResult::Missing;
  }

  if (snapshot_.sample.camera_id == active_candidate->camera_id) {
    if (active_candidate->sample_index <= snapshot_.sample.sample_index) {
      resetLocked();
      return UnrealCameraObservationResult::NonMonotonic;
    }
    ++snapshot_.consecutive_sample_count;
  } else {
    snapshot_.consecutive_sample_count = 1;
  }
  snapshot_.sample = *active_candidate;
  snapshot_.ready =
      snapshot_.consecutive_sample_count >= required_consecutive_samples_;
  return snapshot_.ready ? UnrealCameraObservationResult::Ready
                         : UnrealCameraObservationResult::Pending;
}

UnrealCameraObservationSnapshot UnrealCameraObserver::snapshot() const {
  std::scoped_lock lock(mutex_);
  return snapshot_;
}

void UnrealCameraObserver::reset() {
  std::scoped_lock lock(mutex_);
  resetLocked();
}

void UnrealCameraObserver::resetLocked() {
  snapshot_ = {};
}

}  // namespace vrclient::adapters::unreal

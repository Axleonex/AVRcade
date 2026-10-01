#include "adapters/unreal/meccha_camera_identity_gate.h"

#include <algorithm>
#include <cmath>

namespace vrclient::adapters::unreal {

bool mecchaStereoProjectionMatches(
    float observed_vertical_fov_degrees,
    float observed_aspect_ratio,
    float expected_vertical_fov_degrees,
    float expected_aspect_ratio,
    float fov_tolerance_degrees,
    float aspect_tolerance) {
  return std::isfinite(observed_vertical_fov_degrees) &&
         std::isfinite(observed_aspect_ratio) &&
         std::isfinite(expected_vertical_fov_degrees) &&
         std::isfinite(expected_aspect_ratio) &&
         std::isfinite(fov_tolerance_degrees) &&
         std::isfinite(aspect_tolerance) &&
         observed_vertical_fov_degrees > 1.0F &&
         observed_vertical_fov_degrees < 179.0F &&
         observed_aspect_ratio > 0.0F &&
         expected_vertical_fov_degrees > 1.0F &&
         expected_vertical_fov_degrees < 179.0F &&
         expected_aspect_ratio > 0.0F &&
         fov_tolerance_degrees >= 0.0F && aspect_tolerance >= 0.0F &&
         std::abs(observed_vertical_fov_degrees -
                  expected_vertical_fov_degrees) <= fov_tolerance_degrees &&
         std::abs(observed_aspect_ratio - expected_aspect_ratio) <=
             aspect_tolerance;
}

MecchaCameraIdentityGate::MecchaCameraIdentityGate(
    std::size_t required_consecutive_samples,
    std::int64_t maximum_observation_gap_ns,
    std::int64_t recent_identity_window_ns)
    : required_consecutive_samples_(required_consecutive_samples),
      maximum_observation_gap_ns_(maximum_observation_gap_ns),
      recent_identity_window_ns_(recent_identity_window_ns) {}

MecchaCameraIdentityResult MecchaCameraIdentityGate::observe(
    std::uint64_t camera_id, std::int64_t observation_time_ns) {
  if (camera_id == 0 || observation_time_ns <= 0 ||
      required_consecutive_samples_ == 0 || maximum_observation_gap_ns_ < 0 ||
      recent_identity_window_ns_ < 0) {
    reset();
    return MecchaCameraIdentityResult::InvalidObservation;
  }

  RecentIdentity* observed = nullptr;
  RecentIdentity* vacant = nullptr;
  for (auto& recent : recent_) {
    if (recent.camera_id != 0 && recent.last_seen_ns > observation_time_ns) {
      reset();
      return MecchaCameraIdentityResult::InvalidObservation;
    }
    if (recent.camera_id != 0 &&
        observation_time_ns - recent.last_seen_ns > recent_identity_window_ns_) {
      recent = {};
    }
    if (recent.camera_id == camera_id) {
      observed = &recent;
    } else if (recent.camera_id == 0 && vacant == nullptr) {
      vacant = &recent;
    }
  }
  if (overflow_last_seen_ns_ > observation_time_ns) {
    reset();
    return MecchaCameraIdentityResult::InvalidObservation;
  }
  if (overflow_last_seen_ns_ != 0 &&
      observation_time_ns - overflow_last_seen_ns_ > recent_identity_window_ns_) {
    overflow_last_seen_ns_ = 0;
  }

  std::int64_t previous_seen_ns = 0;
  if (observed != nullptr) {
    previous_seen_ns = observed->last_seen_ns;
    observed->last_seen_ns = observation_time_ns;
  } else if (vacant != nullptr) {
    vacant->camera_id = camera_id;
    vacant->last_seen_ns = observation_time_ns;
  } else {
    overflow_last_seen_ns_ = observation_time_ns;
  }

  std::size_t recent_identity_count = 0;
  for (const auto& recent : recent_) {
    if (recent.camera_id != 0) {
      ++recent_identity_count;
    }
  }
  if (recent_identity_count != 1 || overflow_last_seen_ns_ != 0) {
    candidate_camera_id_ = 0;
    candidate_samples_ = 0;
    candidate_last_seen_ns_ = 0;
    stable_camera_id_ = 0;
    return MecchaCameraIdentityResult::Ambiguous;
  }

  if (camera_id != candidate_camera_id_ || previous_seen_ns == 0 ||
      observation_time_ns - previous_seen_ns > maximum_observation_gap_ns_) {
    candidate_camera_id_ = camera_id;
    candidate_samples_ = 1;
  } else {
    ++candidate_samples_;
  }
  candidate_last_seen_ns_ = observation_time_ns;
  stable_camera_id_ = candidate_samples_ >= required_consecutive_samples_
      ? candidate_camera_id_
      : 0;
  return stable_camera_id_ != 0 ? MecchaCameraIdentityResult::Ready
                                : MecchaCameraIdentityResult::Pending;
}

bool MecchaCameraIdentityGate::allows(
    std::uint64_t camera_id, std::int64_t now_ns) const {
  if (camera_id == 0 || camera_id != stable_camera_id_ ||
      now_ns < candidate_last_seen_ns_ ||
      now_ns - candidate_last_seen_ns_ > maximum_observation_gap_ns_ ||
      (overflow_last_seen_ns_ != 0 && now_ns >= overflow_last_seen_ns_ &&
       now_ns - overflow_last_seen_ns_ <= recent_identity_window_ns_)) {
    return false;
  }
  std::size_t recent_identity_count = 0;
  bool found = false;
  for (const auto& recent : recent_) {
    if (recent.camera_id == 0 || now_ns < recent.last_seen_ns ||
        now_ns - recent.last_seen_ns > recent_identity_window_ns_) {
      continue;
    }
    ++recent_identity_count;
    found = found || recent.camera_id == camera_id;
  }
  return found && recent_identity_count == 1;
}

std::uint64_t MecchaCameraIdentityGate::stableCameraId() const {
  return stable_camera_id_;
}

void MecchaCameraIdentityGate::reset() {
  recent_ = {};
  overflow_last_seen_ns_ = 0;
  candidate_camera_id_ = 0;
  candidate_samples_ = 0;
  candidate_last_seen_ns_ = 0;
  stable_camera_id_ = 0;
}

MecchaStereoCameraIdentityGate::MecchaStereoCameraIdentityGate(
    std::size_t required_complete_pairs,
    std::int64_t maximum_observation_gap_ns,
    std::int64_t recent_identity_window_ns)
    : required_complete_pairs_(required_complete_pairs),
      maximum_observation_gap_ns_(maximum_observation_gap_ns),
      recent_identity_window_ns_(recent_identity_window_ns) {}

MecchaCameraIdentityResult MecchaStereoCameraIdentityGate::observe(
    std::uint64_t camera_id,
    std::int64_t observation_time_ns) {
  if (camera_id == 0 || observation_time_ns <= 0 ||
      required_complete_pairs_ == 0 || maximum_observation_gap_ns_ < 0 ||
      recent_identity_window_ns_ < 0 ||
      observation_time_ns < last_observation_ns_) {
    reset();
    return MecchaCameraIdentityResult::InvalidObservation;
  }
  if (last_observation_ns_ != 0 &&
      observation_time_ns - last_observation_ns_ >
          maximum_observation_gap_ns_) {
    candidate_pair_ = {};
    stable_pair_ = {};
    candidate_seen_mask_ = 0;
    complete_pair_count_ = 0;
  }
  last_observation_ns_ = observation_time_ns;

  RecentIdentity* match = nullptr;
  RecentIdentity* vacant = nullptr;
  for (auto& recent : recent_) {
    if (recent.camera_id != 0 &&
        observation_time_ns - recent.last_seen_ns >
            recent_identity_window_ns_) {
      recent = {};
    }
    if (recent.camera_id == camera_id) {
      match = &recent;
    } else if (recent.camera_id == 0 && vacant == nullptr) {
      vacant = &recent;
    }
  }
  if (match != nullptr) {
    match->last_seen_ns = observation_time_ns;
  } else if (vacant != nullptr) {
    vacant->camera_id = camera_id;
    vacant->last_seen_ns = observation_time_ns;
  } else {
    stable_pair_ = {};
    candidate_pair_ = {};
    candidate_seen_mask_ = 0;
    complete_pair_count_ = 0;
    return MecchaCameraIdentityResult::Ambiguous;
  }

  std::array<std::uint64_t, 2> pair{};
  std::size_t count = 0;
  for (const auto& recent : recent_) {
    if (recent.camera_id != 0) {
      if (count >= pair.size()) {
        stable_pair_ = {};
        candidate_pair_ = {};
        candidate_seen_mask_ = 0;
        complete_pair_count_ = 0;
        return MecchaCameraIdentityResult::Ambiguous;
      }
      pair[count++] = recent.camera_id;
    }
  }
  if (count < 2) {
    stable_pair_ = {};
    return MecchaCameraIdentityResult::Pending;
  }
  std::sort(pair.begin(), pair.end());
  if (pair != candidate_pair_) {
    candidate_pair_ = pair;
    stable_pair_ = {};
    candidate_seen_mask_ = 0;
    // Both members are already fresh when the second identity establishes the
    // candidate pair, so that first observed left/right cycle counts.
    complete_pair_count_ = 1;
  } else {
    candidate_seen_mask_ |= camera_id == candidate_pair_[0] ? 0x1U : 0x2U;
    if (candidate_seen_mask_ == 0x3U) {
      ++complete_pair_count_;
      candidate_seen_mask_ = 0;
    }
  }
  if (complete_pair_count_ >= required_complete_pairs_) {
    stable_pair_ = candidate_pair_;
    return MecchaCameraIdentityResult::Ready;
  }
  return MecchaCameraIdentityResult::Pending;
}

bool MecchaStereoCameraIdentityGate::allows(
    std::uint64_t camera_id,
    std::int64_t now_ns) const {
  if (stable_pair_[0] == 0 || stable_pair_[1] == 0 ||
      (camera_id != stable_pair_[0] && camera_id != stable_pair_[1]) ||
      now_ns < last_observation_ns_ ||
      now_ns - last_observation_ns_ > maximum_observation_gap_ns_) {
    return false;
  }
  std::size_t fresh_count = 0;
  bool found = false;
  for (const auto& recent : recent_) {
    if (recent.camera_id == 0 || now_ns < recent.last_seen_ns ||
        now_ns - recent.last_seen_ns > recent_identity_window_ns_) {
      continue;
    }
    ++fresh_count;
    found = found || recent.camera_id == camera_id;
  }
  return found && fresh_count == 2;
}

std::array<std::uint64_t, 2>
MecchaStereoCameraIdentityGate::stableCameraIds() const {
  return stable_pair_;
}

void MecchaStereoCameraIdentityGate::reset() {
  recent_ = {};
  candidate_pair_ = {};
  stable_pair_ = {};
  candidate_seen_mask_ = 0;
  complete_pair_count_ = 0;
  last_observation_ns_ = 0;
}

}  // namespace vrclient::adapters::unreal

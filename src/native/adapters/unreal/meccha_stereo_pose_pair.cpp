#include "adapters/unreal/meccha_stereo_pose_pair.h"

#include <cmath>

namespace vrclient::adapters::unreal {
namespace {

bool finiteLocation(const UnrealVector3d& location) {
  return std::isfinite(location.x) && std::isfinite(location.y) &&
      std::isfinite(location.z);
}

bool finitePose(const UnrealOrientationPoseSample& pose) {
  if (!pose.valid || pose.publish_time_ns <= 0) {
    return false;
  }
  for (const double value : pose.unreal_delta.values) {
    if (!std::isfinite(value)) {
      return false;
    }
  }
  return true;
}

std::size_t eyeIndex(
    std::uint64_t camera_id,
    const std::array<std::uint64_t, 2>& eye_ids) {
  return camera_id == eye_ids[0] ? 0U : 1U;
}

double distance(
    const UnrealVector3d& first,
    const UnrealVector3d& second) {
  const double dx = second.x - first.x;
  const double dy = second.y - first.y;
  const double dz = second.z - first.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

}  // namespace

MecchaStereoPosePairResult MecchaStereoPosePairCoordinator::select(
    std::uint64_t camera_id,
    const std::array<std::uint64_t, 2>& stable_eye_ids,
    std::uint64_t render_generation,
    const UnrealVector3d& camera_location_uu,
    std::int64_t observation_time_ns,
    const UnrealOrientationPoseSample& latest_pose,
    bool latest_write_enabled,
    MecchaStereoPosePairSelection* output) {
  if (output == nullptr || stable_eye_ids[0] == 0 || stable_eye_ids[1] == 0 ||
      stable_eye_ids[0] == stable_eye_ids[1] || observation_time_ns <= 0 ||
      !finiteLocation(camera_location_uu)) {
    return MecchaStereoPosePairResult::InvalidInput;
  }
  *output = {};
  if (camera_id != stable_eye_ids[0] && camera_id != stable_eye_ids[1]) {
    if (pair_active_) {
      ++incomplete_pair_count_;
      clearActivePair();
    }
    return MecchaStereoPosePairResult::InvalidIdentity;
  }
  if (pair_active_ && (active_eye_ids_ != stable_eye_ids ||
                       render_generation < active_render_generation_)) {
    ++incomplete_pair_count_;
    clearActivePair();
    return MecchaStereoPosePairResult::InvalidIdentity;
  }

  const bool starts_new_generation =
      !pair_active_ || render_generation > active_render_generation_;
  if (starts_new_generation) {
    if (pair_active_ && seen_eye_mask_ != 0x3U) {
      ++incomplete_pair_count_;
    }
    clearActivePair();
    if (!finitePose(latest_pose)) {
      return MecchaStereoPosePairResult::InvalidInput;
    }
    pair_active_ = true;
    active_render_generation_ = render_generation;
    active_eye_ids_ = stable_eye_ids;
    active_pose_ = latest_pose;
    active_write_enabled_ = latest_write_enabled;
    first_observation_time_ns_ = observation_time_ns;
    const auto index = eyeIndex(camera_id, stable_eye_ids);
    active_eye_locations_[index] = camera_location_uu;
    seen_eye_mask_ = static_cast<std::uint8_t>(1U << index);
    output->pose = active_pose_;
    output->eye_camera_ids = active_eye_ids_;
    output->eye_locations_uu = active_eye_locations_;
    output->completed_pair_count = completed_pair_count_;
    output->incomplete_pair_count = incomplete_pair_count_;
    output->duplicate_eye_count = duplicate_eye_count_;
    output->render_generation = active_render_generation_;
    output->write_enabled = active_write_enabled_;
    output->pose_validation_time_ns = first_observation_time_ns_;
    return MecchaStereoPosePairResult::ReadyFirstEye;
  }

  const auto index = eyeIndex(camera_id, stable_eye_ids);
  const auto eye_bit = static_cast<std::uint8_t>(1U << index);
  if ((seen_eye_mask_ & eye_bit) != 0) {
    ++duplicate_eye_count_;
    output->pose = active_pose_;
    output->eye_camera_ids = active_eye_ids_;
    output->eye_locations_uu = active_eye_locations_;
    output->completed_pair_count = completed_pair_count_;
    output->incomplete_pair_count = incomplete_pair_count_;
    output->duplicate_eye_count = duplicate_eye_count_;
    output->render_generation = active_render_generation_;
    output->write_enabled = active_write_enabled_;
    output->pose_validation_time_ns = first_observation_time_ns_;
    return MecchaStereoPosePairResult::ReadyDuplicateEye;
  }

  active_eye_locations_[index] = camera_location_uu;
  seen_eye_mask_ |= eye_bit;
  ++completed_pair_count_;
  output->pose = active_pose_;
  output->eye_camera_ids = active_eye_ids_;
  output->eye_locations_uu = active_eye_locations_;
  output->completed_pair_count = completed_pair_count_;
  output->incomplete_pair_count = incomplete_pair_count_;
  output->duplicate_eye_count = duplicate_eye_count_;
  output->pose_frame_mismatch_count = 0;
  output->render_generation = active_render_generation_;
  output->write_enabled = active_write_enabled_;
  output->pose_validation_time_ns = first_observation_time_ns_;
  output->eye_observation_delta_ns =
      observation_time_ns - first_observation_time_ns_;
  output->eye_separation_uu =
      distance(active_eye_locations_[0], active_eye_locations_[1]);
  return MecchaStereoPosePairResult::ReadyPairComplete;
}

std::uint64_t MecchaStereoPosePairCoordinator::completedPairCount() const {
  return completed_pair_count_;
}

std::uint64_t MecchaStereoPosePairCoordinator::incompletePairCount() const {
  return incomplete_pair_count_;
}

std::uint64_t MecchaStereoPosePairCoordinator::duplicateEyeCount() const {
  return duplicate_eye_count_;
}

void MecchaStereoPosePairCoordinator::clearActivePair() {
  pair_active_ = false;
  active_render_generation_ = 0;
  seen_eye_mask_ = 0;
  active_eye_ids_ = {};
  active_eye_locations_ = {};
  active_pose_ = {};
  active_write_enabled_ = false;
  first_observation_time_ns_ = 0;
}

void MecchaStereoPosePairCoordinator::reset() {
  clearActivePair();
  completed_pair_count_ = 0;
  incomplete_pair_count_ = 0;
  duplicate_eye_count_ = 0;
}

}  // namespace vrclient::adapters::unreal

#pragma once

#include "adapters/unreal/unreal_camera_contract.h"
#include "adapters/unreal/unreal_orientation_only.h"

#include <array>
#include <cstdint>

namespace vrclient::adapters::unreal {

enum class MecchaStereoPosePairResult {
  ReadyFirstEye,
  ReadyPairComplete,
  ReadyDuplicateEye,
  InvalidIdentity,
  InvalidInput,
};

struct MecchaStereoPosePairSelection {
  UnrealOrientationPoseSample pose{};
  std::array<std::uint64_t, 2> eye_camera_ids{};
  std::array<UnrealVector3d, 2> eye_locations_uu{};
  std::uint64_t completed_pair_count = 0;
  std::uint64_t incomplete_pair_count = 0;
  std::uint64_t duplicate_eye_count = 0;
  std::uint64_t pose_frame_mismatch_count = 0;
  std::uint64_t render_generation = 0;
  bool write_enabled = false;
  std::int64_t pose_validation_time_ns = 0;
  std::int64_t eye_observation_delta_ns = 0;
  double eye_separation_uu = 0.0;
};

// Serial coordinator for the exact two stable FSceneView identities. The first
// eye in a game Present generation latches the latest tracking pose; every
// other eligible view before the next Present must reuse that same pose. This
// prevents the asynchronous sampler from advancing between eye constructors.
class MecchaStereoPosePairCoordinator {
 public:
  MecchaStereoPosePairCoordinator() = default;

  MecchaStereoPosePairResult select(
      std::uint64_t camera_id,
      const std::array<std::uint64_t, 2>& stable_eye_ids,
      std::uint64_t render_generation,
      const UnrealVector3d& camera_location_uu,
      std::int64_t observation_time_ns,
      const UnrealOrientationPoseSample& latest_pose,
      bool latest_write_enabled,
      MecchaStereoPosePairSelection* output);

  std::uint64_t completedPairCount() const;
  std::uint64_t incompletePairCount() const;
  std::uint64_t duplicateEyeCount() const;
  void reset();

 private:
  void clearActivePair();

  bool pair_active_ = false;
  std::uint64_t active_render_generation_ = 0;
  std::uint8_t seen_eye_mask_ = 0;
  std::array<std::uint64_t, 2> active_eye_ids_{};
  std::array<UnrealVector3d, 2> active_eye_locations_{};
  UnrealOrientationPoseSample active_pose_{};
  bool active_write_enabled_ = false;
  std::int64_t first_observation_time_ns_ = 0;
  std::uint64_t completed_pair_count_ = 0;
  std::uint64_t incomplete_pair_count_ = 0;
  std::uint64_t duplicate_eye_count_ = 0;
};

}  // namespace vrclient::adapters::unreal

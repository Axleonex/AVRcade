#pragma once

#include "public/vr_runtime_api.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vrclient::adapters::unreal {

struct UnrealRotationMatrix3d {
  std::array<double, 9> values{};
};

struct UnrealOrientationPoseSample {
  UnrealRotationMatrix3d unreal_delta{};
  std::uint64_t frame_index = 0;
  std::int64_t publish_time_ns = 0;
  bool valid = false;
};

enum class UnrealOrientationPoseResult {
  Ready,
  InvalidOutput,
  MissingOrientation,
  InvalidTiming,
  FuturePose,
  StalePose,
  NonFiniteInput,
  InvalidSceneView,
};

UnrealOrientationPoseResult makeUnrealOrientationPoseSample(
    const VrRuntimePose& reference_pose,
    const VrRuntimePose& current_pose,
    std::uint64_t frame_index,
    std::int64_t publish_time_ns,
    UnrealOrientationPoseSample* output);

UnrealOrientationPoseResult applyUnrealOrientationOnlyPose(
    const UnrealRotationMatrix3d& current_view,
    const UnrealOrientationPoseSample& pose,
    std::int64_t now_ns,
    std::int64_t maximum_age_ns,
    UnrealRotationMatrix3d* output);

UnrealOrientationPoseResult
applyMeccha24508135OrientationOnlyToSceneViewInitOptions(
    void* options,
    std::size_t byte_count,
    const UnrealOrientationPoseSample& pose,
    std::int64_t now_ns,
    std::int64_t maximum_age_ns);

}  // namespace vrclient::adapters::unreal

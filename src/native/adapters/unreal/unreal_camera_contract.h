#pragma once

#include "public/vr_runtime_api.h"

#include <array>
#include <cstdint>

namespace vrclient::adapters::unreal {

struct UnrealVector3d {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

// Engine-space camera data: centimeters, X forward, Y right, Z up, and
// pitch/yaw/roll in degrees. Conversion into OpenXR coordinates belongs behind
// a separate tested boundary.
struct UnrealCameraSample {
  std::uint64_t camera_id = 0;
  std::uint64_t sample_index = 0;
  std::int64_t sample_time_ns = 0;
  bool active = false;
  UnrealVector3d location_uu{};
  UnrealVector3d rotation_degrees{};
  float vertical_fov_degrees = 0.0F;
  float aspect_ratio = 0.0F;
  float near_clip_uu = 0.0F;
  float far_clip_uu = 0.0F;
};

enum class UnrealCameraSampleResult {
  Ready,
  InvalidTiming,
  MissingIdentity,
  Inactive,
  FutureSample,
  Stale,
  NonFiniteTransform,
  InvalidProjection,
};

UnrealCameraSampleResult validateUnrealCameraSample(
    const UnrealCameraSample& sample,
    std::int64_t predicted_display_time_ns,
    std::int64_t maximum_age_ns);

struct UnrealEyeCameraView {
  UnrealVector3d location_uu{};
  UnrealVector3d rotation_degrees{};
  float fov_angle_left = 0.0F;
  float fov_angle_right = 0.0F;
  float fov_angle_up = 0.0F;
  float fov_angle_down = 0.0F;
};

struct UnrealStereoCameraFrame {
  std::uint64_t camera_id = 0;
  std::uint64_t camera_sample_index = 0;
  std::uint64_t runtime_frame_index = 0;
  std::array<UnrealEyeCameraView, 2> eyes{};
};

enum class UnrealStereoCameraResult {
  Ready,
  InvalidOutput,
  InvalidCameraSample,
  InvalidRuntimeFrame,
  InvalidReferencePose,
  InvalidWorldScale,
  NonFiniteResult,
};

UnrealStereoCameraResult composeUnrealStereoCameraFrame(
    const UnrealCameraSample& camera,
    const VrRuntimePose& reference_head_pose,
    const VrRuntimeFrameData& runtime_frame,
    double world_units_per_meter,
    std::int64_t maximum_camera_age_ns,
    UnrealStereoCameraFrame* output);

}  // namespace vrclient::adapters::unreal

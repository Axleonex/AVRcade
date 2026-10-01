#include "adapters/unreal/unreal_orientation_only.h"

#include <cmath>
#include <cstring>

namespace vrclient::adapters::unreal {
namespace {

struct Quaterniond {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double w = 1.0;
};

bool finiteMatrix(const UnrealRotationMatrix3d& matrix) {
  for (double value : matrix.values) {
    if (!std::isfinite(value)) {
      return false;
    }
  }
  return true;
}

bool normalized(const VrRuntimePose& pose, Quaterniond* output) {
  if (output == nullptr || pose.orientation_valid == 0 ||
      !std::isfinite(pose.orientation.x) ||
      !std::isfinite(pose.orientation.y) ||
      !std::isfinite(pose.orientation.z) ||
      !std::isfinite(pose.orientation.w)) {
    return false;
  }
  const double length = std::sqrt(
      static_cast<double>(pose.orientation.x) * pose.orientation.x +
      static_cast<double>(pose.orientation.y) * pose.orientation.y +
      static_cast<double>(pose.orientation.z) * pose.orientation.z +
      static_cast<double>(pose.orientation.w) * pose.orientation.w);
  if (!std::isfinite(length) || length <= 1.0e-12) {
    return false;
  }
  *output = {
      pose.orientation.x / length,
      pose.orientation.y / length,
      pose.orientation.z / length,
      pose.orientation.w / length,
  };
  return true;
}

Quaterniond conjugate(const Quaterniond& value) {
  return {-value.x, -value.y, -value.z, value.w};
}

Quaterniond multiply(const Quaterniond& left, const Quaterniond& right) {
  return {
      left.w * right.x + left.x * right.w + left.y * right.z -
          left.z * right.y,
      left.w * right.y - left.x * right.z + left.y * right.w +
          left.z * right.x,
      left.w * right.z + left.x * right.y - left.y * right.x +
          left.z * right.w,
      left.w * right.w - left.x * right.x - left.y * right.y -
          left.z * right.z,
  };
}

UnrealRotationMatrix3d quaternionMatrix(const Quaterniond& value) {
  return {{
      1.0 - 2.0 * (value.y * value.y + value.z * value.z),
      2.0 * (value.x * value.y - value.z * value.w),
      2.0 * (value.x * value.z + value.y * value.w),
      2.0 * (value.x * value.y + value.z * value.w),
      1.0 - 2.0 * (value.x * value.x + value.z * value.z),
      2.0 * (value.y * value.z - value.x * value.w),
      2.0 * (value.x * value.z - value.y * value.w),
      2.0 * (value.y * value.z + value.x * value.w),
      1.0 - 2.0 * (value.x * value.x + value.y * value.y),
  }};
}

UnrealRotationMatrix3d multiply(
    const UnrealRotationMatrix3d& left,
    const UnrealRotationMatrix3d& right) {
  UnrealRotationMatrix3d output{};
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t column = 0; column < 3; ++column) {
      for (std::size_t inner = 0; inner < 3; ++inner) {
        output.values[row * 3 + column] +=
            left.values[row * 3 + inner] *
            right.values[inner * 3 + column];
      }
    }
  }
  return output;
}

UnrealRotationMatrix3d transpose(const UnrealRotationMatrix3d& value) {
  return {{
      value.values[0], value.values[3], value.values[6],
      value.values[1], value.values[4], value.values[7],
      value.values[2], value.values[5], value.values[8],
  }};
}

UnrealRotationMatrix3d xrRotationToUnreal(const Quaterniond& rotation) {
  const UnrealRotationMatrix3d change{{
      0.0, 0.0, -1.0,
      1.0, 0.0, 0.0,
      0.0, 1.0, 0.0,
  }};
  return multiply(
      multiply(change, quaternionMatrix(rotation)), transpose(change));
}

}  // namespace

UnrealOrientationPoseResult makeUnrealOrientationPoseSample(
    const VrRuntimePose& reference_pose,
    const VrRuntimePose& current_pose,
    std::uint64_t frame_index,
    std::int64_t publish_time_ns,
    UnrealOrientationPoseSample* output) {
  if (output == nullptr) {
    return UnrealOrientationPoseResult::InvalidOutput;
  }
  if (publish_time_ns <= 0) {
    return UnrealOrientationPoseResult::InvalidTiming;
  }
  Quaterniond reference{};
  Quaterniond current{};
  if (!normalized(reference_pose, &reference) ||
      !normalized(current_pose, &current)) {
    return UnrealOrientationPoseResult::MissingOrientation;
  }
  UnrealOrientationPoseSample created{};
  created.unreal_delta =
      xrRotationToUnreal(multiply(conjugate(reference), current));
  if (!finiteMatrix(created.unreal_delta)) {
    return UnrealOrientationPoseResult::NonFiniteInput;
  }
  created.frame_index = frame_index;
  created.publish_time_ns = publish_time_ns;
  created.valid = true;
  *output = created;
  return UnrealOrientationPoseResult::Ready;
}

UnrealOrientationPoseResult applyUnrealOrientationOnlyPose(
    const UnrealRotationMatrix3d& current_view,
    const UnrealOrientationPoseSample& pose,
    std::int64_t now_ns,
    std::int64_t maximum_age_ns,
    UnrealRotationMatrix3d* output) {
  if (output == nullptr) {
    return UnrealOrientationPoseResult::InvalidOutput;
  }
  if (!pose.valid) {
    return UnrealOrientationPoseResult::MissingOrientation;
  }
  if (now_ns <= 0 || maximum_age_ns < 0 || pose.publish_time_ns <= 0) {
    return UnrealOrientationPoseResult::InvalidTiming;
  }
  if (pose.publish_time_ns > now_ns) {
    return UnrealOrientationPoseResult::FuturePose;
  }
  if (now_ns - pose.publish_time_ns > maximum_age_ns) {
    return UnrealOrientationPoseResult::StalePose;
  }
  if (!finiteMatrix(current_view) || !finiteMatrix(pose.unreal_delta)) {
    return UnrealOrientationPoseResult::NonFiniteInput;
  }
  constexpr UnrealRotationMatrix3d kAxisPermutation{{
      0.0, 0.0, 1.0,
      1.0, 0.0, 0.0,
      0.0, 1.0, 0.0,
  }};
  const UnrealRotationMatrix3d view_space_delta = multiply(
      multiply(transpose(kAxisPermutation), pose.unreal_delta),
      kAxisPermutation);
  const UnrealRotationMatrix3d composed =
      multiply(current_view, view_space_delta);
  if (!finiteMatrix(composed)) {
    return UnrealOrientationPoseResult::NonFiniteInput;
  }
  *output = composed;
  return UnrealOrientationPoseResult::Ready;
}

UnrealOrientationPoseResult
applyMeccha24508135OrientationOnlyToSceneViewInitOptions(
    void* options,
    std::size_t byte_count,
    const UnrealOrientationPoseSample& pose,
    std::int64_t now_ns,
    std::int64_t maximum_age_ns) {
  constexpr std::size_t kViewMatrixOffset = 0x20;
  constexpr std::size_t kRequiredBytes =
      kViewMatrixOffset + 12 * sizeof(double);
  if (options == nullptr || byte_count < kRequiredBytes) {
    return UnrealOrientationPoseResult::InvalidSceneView;
  }
  auto* bytes = static_cast<std::uint8_t*>(options);
  UnrealRotationMatrix3d current{};
  for (std::size_t row = 0; row < 3; ++row) {
    std::memcpy(
        current.values.data() + row * 3,
        bytes + kViewMatrixOffset + row * 4 * sizeof(double),
        3 * sizeof(double));
  }
  UnrealRotationMatrix3d oriented{};
  const auto result = applyUnrealOrientationOnlyPose(
      current, pose, now_ns, maximum_age_ns, &oriented);
  if (result != UnrealOrientationPoseResult::Ready) {
    return result;
  }
  for (std::size_t row = 0; row < 3; ++row) {
    std::memcpy(
        bytes + kViewMatrixOffset + row * 4 * sizeof(double),
        oriented.values.data() + row * 3,
        3 * sizeof(double));
  }
  return UnrealOrientationPoseResult::Ready;
}

}  // namespace vrclient::adapters::unreal

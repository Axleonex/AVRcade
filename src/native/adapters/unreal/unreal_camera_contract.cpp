#include "adapters/unreal/unreal_camera_contract.h"

#include <cmath>
#include <numbers>

namespace vrclient::adapters::unreal {
namespace {

bool finiteVector(const UnrealVector3d& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
      std::isfinite(value.z);
}

struct Quaterniond {
  double x;
  double y;
  double z;
  double w;
};

struct Matrix3d {
  double m[3][3]{};
};

bool validPose(const VrRuntimePose& pose) {
  const double norm =
      static_cast<double>(pose.orientation.x) * pose.orientation.x +
      static_cast<double>(pose.orientation.y) * pose.orientation.y +
      static_cast<double>(pose.orientation.z) * pose.orientation.z +
      static_cast<double>(pose.orientation.w) * pose.orientation.w;
  return pose.orientation_valid != 0 && pose.position_valid != 0 &&
      std::isfinite(pose.orientation.x) &&
      std::isfinite(pose.orientation.y) &&
      std::isfinite(pose.orientation.z) &&
      std::isfinite(pose.orientation.w) && norm > 1.0e-12 &&
      std::isfinite(pose.position.x) && std::isfinite(pose.position.y) &&
      std::isfinite(pose.position.z);
}

Quaterniond normalized(const VrRuntimeQuat& value) {
  const double length = std::sqrt(
      static_cast<double>(value.x) * value.x +
      static_cast<double>(value.y) * value.y +
      static_cast<double>(value.z) * value.z +
      static_cast<double>(value.w) * value.w);
  return {value.x / length, value.y / length, value.z / length, value.w / length};
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

UnrealVector3d rotate(
    const Quaterniond& rotation,
    const UnrealVector3d& value) {
  const Quaterniond vector{value.x, value.y, value.z, 0.0};
  const Quaterniond rotated =
      multiply(multiply(rotation, vector), conjugate(rotation));
  return {rotated.x, rotated.y, rotated.z};
}

Matrix3d quaternionMatrix(const Quaterniond& q) {
  Matrix3d out{};
  out.m[0][0] = 1.0 - 2.0 * (q.y * q.y + q.z * q.z);
  out.m[0][1] = 2.0 * (q.x * q.y - q.z * q.w);
  out.m[0][2] = 2.0 * (q.x * q.z + q.y * q.w);
  out.m[1][0] = 2.0 * (q.x * q.y + q.z * q.w);
  out.m[1][1] = 1.0 - 2.0 * (q.x * q.x + q.z * q.z);
  out.m[1][2] = 2.0 * (q.y * q.z - q.x * q.w);
  out.m[2][0] = 2.0 * (q.x * q.z - q.y * q.w);
  out.m[2][1] = 2.0 * (q.y * q.z + q.x * q.w);
  out.m[2][2] = 1.0 - 2.0 * (q.x * q.x + q.y * q.y);
  return out;
}

Matrix3d multiply(const Matrix3d& left, const Matrix3d& right) {
  Matrix3d out{};
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      for (int inner = 0; inner < 3; ++inner) {
        out.m[row][column] += left.m[row][inner] * right.m[inner][column];
      }
    }
  }
  return out;
}

Matrix3d unrealRotationMatrix(const UnrealVector3d& rotation_degrees) {
  constexpr double kRadians = std::numbers::pi / 180.0;
  const double pitch = rotation_degrees.x * kRadians;
  const double yaw = rotation_degrees.y * kRadians;
  const double roll = rotation_degrees.z * kRadians;
  const double sp = std::sin(pitch);
  const double cp = std::cos(pitch);
  const double sy = std::sin(yaw);
  const double cy = std::cos(yaw);
  const double sr = std::sin(roll);
  const double cr = std::cos(roll);
  Matrix3d out{};
  out.m[0][0] = cp * cy;
  out.m[1][0] = cp * sy;
  out.m[2][0] = sp;
  out.m[0][1] = sr * sp * cy - cr * sy;
  out.m[1][1] = sr * sp * sy + cr * cy;
  out.m[2][1] = -sr * cp;
  out.m[0][2] = -(cr * sp * cy + sr * sy);
  out.m[1][2] = cy * sr - cr * sp * sy;
  out.m[2][2] = cr * cp;
  return out;
}

UnrealVector3d transform(
    const Matrix3d& matrix,
    const UnrealVector3d& value) {
  return {
      matrix.m[0][0] * value.x + matrix.m[0][1] * value.y +
          matrix.m[0][2] * value.z,
      matrix.m[1][0] * value.x + matrix.m[1][1] * value.y +
          matrix.m[1][2] * value.z,
      matrix.m[2][0] * value.x + matrix.m[2][1] * value.y +
          matrix.m[2][2] * value.z,
  };
}

Matrix3d xrRotationToUnreal(const Quaterniond& rotation) {
  const Matrix3d change{{{0.0, 0.0, -1.0},
                          {1.0, 0.0, 0.0},
                          {0.0, 1.0, 0.0}}};
  const Matrix3d change_transpose{{{0.0, 1.0, 0.0},
                                    {0.0, 0.0, 1.0},
                                    {-1.0, 0.0, 0.0}}};
  return multiply(multiply(change, quaternionMatrix(rotation)), change_transpose);
}

UnrealVector3d matrixToUnrealRotation(const Matrix3d& matrix) {
  constexpr double kDegrees = 180.0 / std::numbers::pi;
  const double planar = std::hypot(matrix.m[0][0], matrix.m[1][0]);
  return {
      std::atan2(matrix.m[2][0], planar) * kDegrees,
      std::atan2(matrix.m[1][0], matrix.m[0][0]) * kDegrees,
      std::atan2(-matrix.m[2][1], matrix.m[2][2]) * kDegrees,
  };
}

UnrealVector3d xrVectorToUnreal(const UnrealVector3d& value) {
  return {-value.z, value.x, value.y};
}

}  // namespace

UnrealCameraSampleResult validateUnrealCameraSample(
    const UnrealCameraSample& sample,
    std::int64_t predicted_display_time_ns,
    std::int64_t maximum_age_ns) {
  if (predicted_display_time_ns <= 0 || maximum_age_ns < 0) {
    return UnrealCameraSampleResult::InvalidTiming;
  }
  if (sample.camera_id == 0 || sample.sample_time_ns <= 0) {
    return UnrealCameraSampleResult::MissingIdentity;
  }
  if (!sample.active) {
    return UnrealCameraSampleResult::Inactive;
  }
  if (sample.sample_time_ns > predicted_display_time_ns) {
    return UnrealCameraSampleResult::FutureSample;
  }
  if (predicted_display_time_ns - sample.sample_time_ns > maximum_age_ns) {
    return UnrealCameraSampleResult::Stale;
  }
  if (!finiteVector(sample.location_uu) ||
      !finiteVector(sample.rotation_degrees)) {
    return UnrealCameraSampleResult::NonFiniteTransform;
  }
  if (!std::isfinite(sample.vertical_fov_degrees) ||
      sample.vertical_fov_degrees <= 1.0F ||
      sample.vertical_fov_degrees >= 179.0F ||
      !std::isfinite(sample.aspect_ratio) || sample.aspect_ratio <= 0.0F ||
      !std::isfinite(sample.near_clip_uu) || sample.near_clip_uu <= 0.0F ||
      !std::isfinite(sample.far_clip_uu) ||
      sample.far_clip_uu <= sample.near_clip_uu) {
    return UnrealCameraSampleResult::InvalidProjection;
  }
  return UnrealCameraSampleResult::Ready;
}

UnrealStereoCameraResult composeUnrealStereoCameraFrame(
    const UnrealCameraSample& camera,
    const VrRuntimePose& reference_head_pose,
    const VrRuntimeFrameData& runtime_frame,
    double world_units_per_meter,
    std::int64_t maximum_camera_age_ns,
    UnrealStereoCameraFrame* output) {
  if (output == nullptr) {
    return UnrealStereoCameraResult::InvalidOutput;
  }
  if (validateUnrealCameraSample(
          camera,
          runtime_frame.timing.predicted_display_time_ns,
          maximum_camera_age_ns) != UnrealCameraSampleResult::Ready) {
    return UnrealStereoCameraResult::InvalidCameraSample;
  }
  if (runtime_frame.eye_count != 2 ||
      runtime_frame.eyes[0].eye != VR_RUNTIME_EYE_LEFT ||
      runtime_frame.eyes[1].eye != VR_RUNTIME_EYE_RIGHT ||
      !validPose(runtime_frame.head_pose) ||
      !validPose(runtime_frame.eyes[0].pose) ||
      !validPose(runtime_frame.eyes[1].pose)) {
    return UnrealStereoCameraResult::InvalidRuntimeFrame;
  }
  if (!validPose(reference_head_pose)) {
    return UnrealStereoCameraResult::InvalidReferencePose;
  }
  if (!std::isfinite(world_units_per_meter) || world_units_per_meter <= 0.0) {
    return UnrealStereoCameraResult::InvalidWorldScale;
  }

  const Quaterniond reference = normalized(reference_head_pose.orientation);
  const Quaterniond inverse_reference = conjugate(reference);
  const Matrix3d base_rotation = unrealRotationMatrix(camera.rotation_degrees);
  UnrealStereoCameraFrame composed{};
  composed.camera_id = camera.camera_id;
  composed.camera_sample_index = camera.sample_index;
  composed.runtime_frame_index = runtime_frame.timing.frame_index;

  for (std::size_t eye_index = 0; eye_index < composed.eyes.size(); ++eye_index) {
    const auto& source = runtime_frame.eyes[eye_index];
    const Quaterniond eye_rotation = normalized(source.pose.orientation);
    const Quaterniond relative_rotation = multiply(inverse_reference, eye_rotation);
    const UnrealVector3d stage_delta{
        static_cast<double>(source.pose.position.x - reference_head_pose.position.x),
        static_cast<double>(source.pose.position.y - reference_head_pose.position.y),
        static_cast<double>(source.pose.position.z - reference_head_pose.position.z),
    };
    const UnrealVector3d local_xr_delta = rotate(inverse_reference, stage_delta);
    UnrealVector3d local_unreal_delta = xrVectorToUnreal(local_xr_delta);
    local_unreal_delta.x *= world_units_per_meter;
    local_unreal_delta.y *= world_units_per_meter;
    local_unreal_delta.z *= world_units_per_meter;
    const UnrealVector3d world_delta = transform(base_rotation, local_unreal_delta);
    const Matrix3d eye_world_rotation =
        multiply(base_rotation, xrRotationToUnreal(relative_rotation));

    auto& destination = composed.eyes[eye_index];
    destination.location_uu = {
        camera.location_uu.x + world_delta.x,
        camera.location_uu.y + world_delta.y,
        camera.location_uu.z + world_delta.z,
    };
    destination.rotation_degrees = matrixToUnrealRotation(eye_world_rotation);
    destination.fov_angle_left = source.fov_angle_left;
    destination.fov_angle_right = source.fov_angle_right;
    destination.fov_angle_up = source.fov_angle_up;
    destination.fov_angle_down = source.fov_angle_down;
    if (!finiteVector(destination.location_uu) ||
        !finiteVector(destination.rotation_degrees)) {
      return UnrealStereoCameraResult::NonFiniteResult;
    }
  }
  *output = composed;
  return UnrealStereoCameraResult::Ready;
}

}  // namespace vrclient::adapters::unreal

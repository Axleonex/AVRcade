#include "adapters/unreal/unreal_scene_view_decoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vrclient::adapters::unreal {
namespace {

template <typename T>
T readAt(const std::uint8_t* bytes, std::size_t offset) {
  T value{};
  std::memcpy(&value, bytes + offset, sizeof(value));
  return value;
}

double radiansToDegrees(double value) {
  constexpr double kPi = 3.14159265358979323846;
  return value * 180.0 / kPi;
}

}  // namespace

UnrealSceneViewDecodeResult decodeMeccha24508135SceneViewInitOptions(
    const std::uint8_t* bytes,
    std::size_t byte_count,
    std::uint64_t sample_index,
    std::int64_t sample_time_ns,
    UnrealCameraSample* camera,
    UnrealSceneViewIdentity* identity) {
  if (bytes == nullptr || camera == nullptr ||
      byte_count < kMeccha24508135SceneViewCaptureSize || sample_index == 0 ||
      sample_time_ns <= 0) {
    return UnrealSceneViewDecodeResult::InvalidInput;
  }

  UnrealCameraSample decoded{};
  decoded.location_uu = {
      readAt<double>(bytes, 0x00),
      readAt<double>(bytes, 0x08),
      readAt<double>(bytes, 0x10)};
  double view[3][3]{};
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t column = 0; column < 3; ++column) {
      view[row][column] = readAt<double>(
          bytes, 0x20 + (row * 4 + column) * sizeof(double));
    }
  }
  const double m00 = readAt<double>(bytes, 0xA0 + 0 * sizeof(double));
  const double m11 = readAt<double>(bytes, 0xA0 + 5 * sizeof(double));
  const double depth_a = readAt<double>(bytes, 0xA0 + 10 * sizeof(double));
  const double depth_b = readAt<double>(bytes, 0xA0 + 14 * sizeof(double));

  bool finite = std::isfinite(decoded.location_uu.x) &&
      std::isfinite(decoded.location_uu.y) &&
      std::isfinite(decoded.location_uu.z) && std::isfinite(m00) &&
      std::isfinite(m11) && std::isfinite(depth_a) && std::isfinite(depth_b);
  for (const auto& row : view) {
    for (double value : row) {
      finite = finite && std::isfinite(value);
    }
  }
  if (!finite) {
    return UnrealSceneViewDecodeResult::NonFinite;
  }

  UnrealSceneViewIdentity decoded_identity{};
  // UE 5.6 adds 0x18 bytes between the base projection-data layout and
  // FSceneViewInitOptions pointers. These offsets are proven for Meccha build
  // 24508135 by the pinned constructor reads at +0x2F and +0x40.
  decoded_identity.view_family = readAt<std::uint64_t>(bytes, 0x158);
  decoded_identity.scene_view_state = readAt<std::uint64_t>(bytes, 0x160);
  decoded_identity.actor = readAt<std::uint64_t>(bytes, 0x168);
  const auto plausible_pointer = [](std::uint64_t value) {
    return value >= 0x10000 && value <= 0x00007FFFFFFFFFFFULL &&
        (value & 0x7) == 0;
  };
  decoded.camera_id = decoded_identity.scene_view_state;
  if (!plausible_pointer(decoded_identity.view_family) ||
      !plausible_pointer(decoded_identity.scene_view_state)) {
    return UnrealSceneViewDecodeResult::MissingIdentity;
  }

  // UE constructs this matrix as InverseRotation * AxisPermutation. Recover
  // the original UE rotation matrix with R = AxisPermutation * transpose(View).
  constexpr double axis[3][3] = {
      {0.0, 0.0, 1.0},
      {1.0, 0.0, 0.0},
      {0.0, 1.0, 0.0}};
  double rotation[3][3]{};
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t column = 0; column < 3; ++column) {
      for (std::size_t k = 0; k < 3; ++k) {
        rotation[row][column] += axis[row][k] * view[column][k];
      }
    }
  }
  const double pitch = std::asin(std::clamp(rotation[0][2], -1.0, 1.0));
  const double yaw = std::atan2(rotation[0][1], rotation[0][0]);
  const double roll = std::atan2(-rotation[1][2], rotation[2][2]);
  decoded.rotation_degrees = {
      radiansToDegrees(pitch),
      radiansToDegrees(yaw),
      radiansToDegrees(roll)};

  constexpr double kMinimumProjectionScale = 1e-8;
  if (std::abs(m00) < kMinimumProjectionScale ||
      std::abs(m11) < kMinimumProjectionScale || depth_b <= 0.0) {
    return UnrealSceneViewDecodeResult::InvalidProjection;
  }
  constexpr double kPi = 3.14159265358979323846;
  decoded.vertical_fov_degrees = static_cast<float>(
      2.0 * std::atan(1.0 / std::abs(m11)) * 180.0 / kPi);
  decoded.aspect_ratio = static_cast<float>(std::abs(m11 / m00));
  const double near_clip = depth_b / (1.0 - depth_a);
  const double far_clip = std::abs(depth_a) < 1e-12
      ? 1'000'000'000.0
      : -depth_b / depth_a;
  if (!std::isfinite(near_clip) || !std::isfinite(far_clip) ||
      near_clip <= 0.0 || far_clip <= near_clip ||
      decoded.vertical_fov_degrees <= 1.0F ||
      decoded.vertical_fov_degrees >= 179.0F ||
      decoded.aspect_ratio <= 0.0F) {
    return UnrealSceneViewDecodeResult::InvalidProjection;
  }
  decoded.near_clip_uu = static_cast<float>(near_clip);
  decoded.far_clip_uu = static_cast<float>(far_clip);
  decoded.sample_index = sample_index;
  decoded.sample_time_ns = sample_time_ns;
  decoded.active = true;

  *camera = decoded;
  if (identity != nullptr) {
    *identity = decoded_identity;
  }
  return UnrealSceneViewDecodeResult::Ready;
}

}  // namespace vrclient::adapters::unreal

#include "adapters/unreal/meccha_stereo_projection.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace vrclient::adapters::unreal {
namespace {

constexpr std::size_t kProjectionMatrixOffset = 0xA0;
constexpr std::size_t kProjectionScaleXOffset =
    kProjectionMatrixOffset + 0 * sizeof(double);
constexpr std::size_t kProjectionScaleYOffset =
    kProjectionMatrixOffset + 5 * sizeof(double);
constexpr std::size_t kRequiredBytes =
    kProjectionScaleYOffset + sizeof(double);
constexpr double kStockFovToleranceDegrees = 0.1;
constexpr double kStockAspectTolerance = 0.002;
constexpr double kTargetFovToleranceDegrees = 0.001;
constexpr double kTargetAspectTolerance = 0.0001;
constexpr double kPi = 3.14159265358979323846;

double readDouble(const std::uint8_t* bytes, std::size_t offset) {
  double value = 0.0;
  std::memcpy(&value, bytes + offset, sizeof(value));
  return value;
}

void writeDouble(std::uint8_t* bytes, std::size_t offset, double value) {
  std::memcpy(bytes + offset, &value, sizeof(value));
}

}  // namespace

MecchaStereoProjectionNormalizeResult
normalizeMeccha24517175StereoProjection(
    void* scene_view_init_options,
    std::size_t byte_count) {
  if (scene_view_init_options == nullptr || byte_count < kRequiredBytes) {
    return MecchaStereoProjectionNormalizeResult::InvalidInput;
  }

  auto* bytes = static_cast<std::uint8_t*>(scene_view_init_options);
  const double source_scale_x = readDouble(bytes, kProjectionScaleXOffset);
  const double source_scale_y = readDouble(bytes, kProjectionScaleYOffset);
  if (!std::isfinite(source_scale_x) || !std::isfinite(source_scale_y)) {
    return MecchaStereoProjectionNormalizeResult::NonFiniteProjection;
  }
  if (source_scale_x <= 0.0 || source_scale_y <= 0.0) {
    return MecchaStereoProjectionNormalizeResult::UnexpectedSourceProjection;
  }

  const double source_vertical_fov_degrees =
      2.0 * std::atan(1.0 / source_scale_y) * 180.0 / kPi;
  const double source_aspect_ratio = source_scale_y / source_scale_x;
  if (!std::isfinite(source_vertical_fov_degrees) ||
      !std::isfinite(source_aspect_ratio)) {
    return MecchaStereoProjectionNormalizeResult::NonFiniteProjection;
  }
  if (std::abs(
          source_vertical_fov_degrees -
          kMeccha24517175NormalizedStereoVerticalFovDegrees) <=
          kTargetFovToleranceDegrees &&
      std::abs(
          source_aspect_ratio -
          kMeccha24517175NormalizedStereoAspectRatio) <=
          kTargetAspectTolerance) {
    return MecchaStereoProjectionNormalizeResult::AlreadyNormalized;
  }
  if (std::abs(
          source_vertical_fov_degrees -
          kMeccha24517175StockStereoVerticalFovDegrees) >
          kStockFovToleranceDegrees ||
      std::abs(
          source_aspect_ratio - kMeccha24517175StockStereoAspectRatio) >
          kStockAspectTolerance) {
    return MecchaStereoProjectionNormalizeResult::UnexpectedSourceProjection;
  }

  const double target_scale_y = 1.0 / std::tan(
      kMeccha24517175NormalizedStereoVerticalFovDegrees * kPi / 360.0);
  const double target_scale_x =
      target_scale_y / kMeccha24517175NormalizedStereoAspectRatio;
  if (!std::isfinite(target_scale_x) || !std::isfinite(target_scale_y) ||
      target_scale_x <= 0.0 || target_scale_y <= 0.0) {
    return MecchaStereoProjectionNormalizeResult::NonFiniteProjection;
  }

  writeDouble(bytes, kProjectionScaleXOffset, target_scale_x);
  writeDouble(bytes, kProjectionScaleYOffset, target_scale_y);
  return MecchaStereoProjectionNormalizeResult::Ready;
}

}  // namespace vrclient::adapters::unreal

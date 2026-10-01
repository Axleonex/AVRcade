#pragma once

#include <cstddef>

namespace vrclient::adapters::unreal {

constexpr double kMeccha24517175StockStereoVerticalFovDegrees =
    90.254837036132812;
constexpr double kMeccha24517175StockStereoAspectRatio =
    1.0467959642410278;
constexpr double kMeccha24517175NormalizedStereoVerticalFovDegrees = 110.0;
constexpr double kMeccha24517175NormalizedStereoAspectRatio = 1.0;

enum class MecchaStereoProjectionNormalizeResult {
  Ready,
  AlreadyNormalized,
  InvalidInput,
  NonFiniteProjection,
  UnexpectedSourceProjection,
};

// Replaces only the two angular scale terms in the exact stock fake-stereo
// projection observed from Meccha build 24517175. Unknown, already-normalized,
// non-finite, or undersized inputs are left byte-for-byte unchanged.
MecchaStereoProjectionNormalizeResult
normalizeMeccha24517175StereoProjection(
    void* scene_view_init_options,
    std::size_t byte_count);

}  // namespace vrclient::adapters::unreal

#pragma once

#include "adapters/unreal/unreal_camera_contract.h"

#include <cstddef>
#include <cstdint>

namespace vrclient::adapters::unreal {

constexpr std::size_t kMeccha24508135SceneViewCaptureSize = 0x1A0;

struct UnrealSceneViewIdentity {
  std::uint64_t view_family = 0;
  std::uint64_t scene_view_state = 0;
  std::uint64_t actor = 0;
  std::int32_t player_index = -1;
  std::int32_t stereo_pass = -1;
};

enum class UnrealSceneViewDecodeResult {
  Ready,
  InvalidInput,
  NonFinite,
  MissingIdentity,
  InvalidProjection,
};

UnrealSceneViewDecodeResult decodeMeccha24508135SceneViewInitOptions(
    const std::uint8_t* bytes,
    std::size_t byte_count,
    std::uint64_t sample_index,
    std::int64_t sample_time_ns,
    UnrealCameraSample* camera,
    UnrealSceneViewIdentity* identity = nullptr);

}  // namespace vrclient::adapters::unreal

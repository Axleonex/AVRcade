#pragma once

#include "adapters/unreal/d3d12_observer.h"
#include "adapters/unreal/unreal_frame_contract.h"
#include "public/vr_runtime_api.h"

#include <cstdint>

namespace vrclient::adapters::unreal {

enum class UnrealOpenXrBridgeResult {
  Ready,
  InvalidArgument,
  ObservationNotReady,
  InvalidFrame,
  InvalidStereoTargets,
};

UnrealOpenXrBridgeResult makeOpenXrD3D12Binding(
    const D3D12ObservationSnapshot& observation,
    VrRuntimeD3D12Binding* binding);

UnrealOpenXrBridgeResult makeUnrealFrameSubmission(
    const VrRuntimeFrameData& frame,
    const VrRuntimeRenderTarget* targets,
    std::uint32_t target_count,
    UnrealFrameSubmission* submission);

}  // namespace vrclient::adapters::unreal

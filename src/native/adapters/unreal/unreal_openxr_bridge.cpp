#include "adapters/unreal/unreal_openxr_bridge.h"

#include <cstring>

namespace vrclient::adapters::unreal {
namespace {

void copyMatrix(const VrRuntimeMatrix4& source, UnrealMatrix4* destination) {
  static_assert(sizeof(source.m) == sizeof(destination->values));
  std::memcpy(destination->values, source.m, sizeof(source.m));
}

}  // namespace

UnrealOpenXrBridgeResult makeOpenXrD3D12Binding(
    const D3D12ObservationSnapshot& observation,
    VrRuntimeD3D12Binding* binding) {
  if (binding == nullptr) {
    return UnrealOpenXrBridgeResult::InvalidArgument;
  }
  *binding = {};
  if (!observation.ready || observation.device == nullptr ||
      observation.queue == nullptr || observation.swapchain == nullptr) {
    return UnrealOpenXrBridgeResult::ObservationNotReady;
  }

  binding->size = sizeof(*binding);
  binding->device = observation.device;
  binding->queue = observation.queue;
  return UnrealOpenXrBridgeResult::Ready;
}

UnrealOpenXrBridgeResult makeUnrealFrameSubmission(
    const VrRuntimeFrameData& frame,
    const VrRuntimeRenderTarget* targets,
    std::uint32_t target_count,
    UnrealFrameSubmission* submission) {
  if (targets == nullptr || submission == nullptr) {
    return UnrealOpenXrBridgeResult::InvalidArgument;
  }
  *submission = {};
  if (frame.eye_count != 2 ||
      frame.timing.predicted_display_time_ns <= 0) {
    return UnrealOpenXrBridgeResult::InvalidFrame;
  }
  if (target_count != 2) {
    return UnrealOpenXrBridgeResult::InvalidStereoTargets;
  }

  bool eye_seen[2]{};
  submission->frame_index = frame.timing.frame_index;
  submission->predicted_display_time_ns =
      frame.timing.predicted_display_time_ns;
  submission->renderer = RendererApi::D3D12;

  for (std::uint32_t index = 0; index < target_count; ++index) {
    const VrRuntimeRenderTarget& target = targets[index];
    if (target.backend != VR_RUNTIME_GRAPHICS_BACKEND_D3D12 ||
        target.eye < VR_RUNTIME_EYE_LEFT ||
        target.eye > VR_RUNTIME_EYE_RIGHT) {
      return UnrealOpenXrBridgeResult::InvalidStereoTargets;
    }
    const auto eye_index = static_cast<std::uint32_t>(target.eye);
    if (eye_seen[eye_index] || target.color_texture == nullptr ||
        target.width == 0 || target.height == 0) {
      return UnrealOpenXrBridgeResult::InvalidStereoTargets;
    }
    eye_seen[eye_index] = true;

    const VrRuntimeEyeView& eye_view = frame.eyes[eye_index];
    if (eye_view.eye != target.eye) {
      return UnrealOpenXrBridgeResult::InvalidStereoTargets;
    }
    UnrealEyeSubmission& eye = submission->eyes[eye_index];
    copyMatrix(eye_view.view, &eye.view);
    copyMatrix(eye_view.projection, &eye.projection);
    eye.color_target = target.color_texture;
    eye.color_array_index = target.color_array_index;
    eye.width = target.width;
    eye.height = target.height;
    eye.color_format = target.color_format;
  }

  return validateUnrealFrameSubmission(*submission) ==
          UnrealSubmissionResult::Ready
      ? UnrealOpenXrBridgeResult::Ready
      : UnrealOpenXrBridgeResult::InvalidFrame;
}

}  // namespace vrclient::adapters::unreal

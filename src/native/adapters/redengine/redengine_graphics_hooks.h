#pragma once

#include "plugins/sdk/redengine_services.h"
#include "public/vr_runtime_api.h"

#include <cstdint>
#include <vector>

namespace vrclient::adapters::redengine {

enum class GraphicsHookResult {
  Installed,
  AlreadyInstalled,
  ModuleUnavailable,
  ExportUnavailable,
  HookFailed,
};

GraphicsHookResult startGraphicsObservation();
bool stopGraphicsObservation();
void requestVrcamCapture();
bool makeD3D12Binding(VrRuntimeD3D12Binding* binding);
VrRuntimeResult renderCapturedVrcamFrame(
    const VrRuntimeFrameData& frame,
    const VrRuntimeRenderTarget* targets,
    std::uint32_t target_count);
VrRuntimeResult readbackCapturedVrcam(
    std::vector<std::uint8_t>* pixels,
    std::uint32_t* width,
    std::uint32_t* height);
void queryGraphicsObservation(VrRedengineGraphicsSnapshot* snapshot);

}  // namespace vrclient::adapters::redengine

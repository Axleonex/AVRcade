#pragma once

#include "public/vr_runtime_api.h"

#include <d3d12.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace vrclient::adapters::unreal {

enum class D3D12SceneRelaySourceLayout {
  Monoscopic,
  SideBySideStereo,
};

struct D3D12SceneRelayUvTransform {
  float scale_u = 1.0F;
  float bias_u = 0.0F;
};

struct D3D12SceneRelayProjection {
  float vertical_fov_degrees = 0.0F;
  float eye_aspect_ratio = 0.0F;
};

struct D3D12SceneRelayProjectionUv {
  float u = 0.0F;
  float v = 0.0F;
};

D3D12SceneRelayUvTransform sceneRelayUvTransform(
    D3D12SceneRelaySourceLayout layout,
    VrRuntimeEye eye);
bool sideBySideProjectionForPackedTexture(
    const D3D12SceneRelayProjection& configured,
    std::uint64_t packed_width,
    std::uint32_t packed_height,
    D3D12SceneRelayProjection* output);

bool mapOpenXrFovToSceneRelayProjection(
    const D3D12SceneRelayProjection& source,
    const VrRuntimeEyeView& target,
    float target_u,
    float target_v,
    D3D12SceneRelayProjectionUv* output);

// Relays a captured flat-game frame into OpenXR eye targets. This is an
// intentionally separate diagnostic rung: it proves scene transport only and
// does not claim per-eye Unreal camera rendering or head-tracked stereo.
class D3D12SceneRelayRenderer {
 public:
  D3D12SceneRelayRenderer();
  ~D3D12SceneRelayRenderer();

  D3D12SceneRelayRenderer(const D3D12SceneRelayRenderer&) = delete;
  D3D12SceneRelayRenderer& operator=(const D3D12SceneRelayRenderer&) = delete;

  bool initialize(ID3D12Device* device, ID3D12CommandQueue* queue);
  bool configureSideBySideProjection(
      const D3D12SceneRelayProjection& projection);
  VrRuntimeResult capture(
      ID3D12Resource* source,
      D3D12_RESOURCE_STATES source_state = D3D12_RESOURCE_STATE_PRESENT);
  VrRuntimeResult captureSideBySideStereo(
      ID3D12Resource* source,
      D3D12_RESOURCE_STATES source_state = D3D12_RESOURCE_STATE_PRESENT);
  VrRuntimeResult recordInlineCapture(
      ID3D12GraphicsCommandList* producer_command_list,
      ID3D12Resource* source,
      D3D12_RESOURCE_STATES source_state);
  bool publishInlineCapture(ID3D12CommandList* executed_command_list);
  VrRuntimeResult render(
      const VrRuntimeFrameData& frame,
      const VrRuntimeRenderTarget* targets,
      uint32_t target_count);
  VrRuntimeResult readbackCapturedRgba8(
      std::vector<std::uint8_t>* pixels,
      std::uint32_t* width,
      std::uint32_t* height);

 private:
  VrRuntimeResult captureWithLayout(
      ID3D12Resource* source,
      D3D12_RESOURCE_STATES source_state,
      D3D12SceneRelaySourceLayout layout);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace vrclient::adapters::unreal

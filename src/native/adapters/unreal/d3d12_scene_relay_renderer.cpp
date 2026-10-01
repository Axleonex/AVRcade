#include "adapters/unreal/d3d12_scene_relay_renderer.h"

#include "adapters/unreal/d3d12_render_util.h"
#include "adapters/unreal/unreal_openxr_bridge.h"

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cfloat>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace vrclient::adapters::unreal {
namespace {

using Microsoft::WRL::ComPtr;

constexpr char kRelayShader[] = R"hlsl(
Texture2D<float4> SceneTexture : register(t0);
SamplerState SceneSampler : register(s0);
cbuffer RelayParameters : register(b0) {
  float UvScaleU;
  float UvBiasU;
  float SourceHalfTexelU;
  float SourceHalfTexelV;
  float ProjectionWarpEnabled;
  float SourceTanHalfX;
  float SourceTanHalfY;
  float TargetTanLeft;
  float TargetTanRight;
  float TargetTanUp;
  float TargetTanDown;
};

struct VertexOutput {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

VertexOutput VSMain(uint vertex_id : SV_VertexID) {
  VertexOutput output;
  float2 uv = float2((vertex_id << 1) & 2, vertex_id & 2);
  output.position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
  output.uv = uv;
  return output;
}

float4 PSMain(VertexOutput input) : SV_Target {
  float2 local_uv = input.uv;
  if (ProjectionWarpEnabled > 0.5) {
    float target_tan_x = lerp(TargetTanLeft, TargetTanRight, input.uv.x);
    float target_tan_y = lerp(TargetTanUp, TargetTanDown, input.uv.y);
    local_uv.x = (target_tan_x + SourceTanHalfX) / (2.0 * SourceTanHalfX);
    local_uv.y = (SourceTanHalfY - target_tan_y) / (2.0 * SourceTanHalfY);
  }
  float2 source_uv = float2(local_uv.x * UvScaleU + UvBiasU, local_uv.y);
  source_uv.x = clamp(
      source_uv.x,
      UvBiasU + SourceHalfTexelU,
      UvBiasU + UvScaleU - SourceHalfTexelU);
  source_uv.y = clamp(source_uv.y, SourceHalfTexelV, 1.0 - SourceHalfTexelV);
  return SceneTexture.Sample(SceneSampler, source_uv);
}
)hlsl";

constexpr DWORD kFenceTimeoutMs = 10'000;
// Enough in-flight submissions to cover one capture plus one stereo render
// without the CPU ever waiting on a healthy GPU.
constexpr std::size_t kSubmitSlots = 3;

bool sameTextureShape(
    const D3D12_RESOURCE_DESC& left,
    const D3D12_RESOURCE_DESC& right) {
  return left.Dimension == right.Dimension && left.Width == right.Width &&
      left.Height == right.Height &&
      left.DepthOrArraySize == right.DepthOrArraySize &&
      left.MipLevels == right.MipLevels && left.Format == right.Format &&
      left.SampleDesc.Count == right.SampleDesc.Count &&
      left.SampleDesc.Quality == right.SampleDesc.Quality;
}

D3D12_RESOURCE_BARRIER transition(
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after;
  return barrier;
}

}  // namespace

D3D12SceneRelayUvTransform sceneRelayUvTransform(
    D3D12SceneRelaySourceLayout layout,
    VrRuntimeEye eye) {
  if (layout != D3D12SceneRelaySourceLayout::SideBySideStereo) {
    return {};
  }
  return {
      0.5F,
      eye == VR_RUNTIME_EYE_RIGHT ? 0.5F : 0.0F,
  };
}

bool sideBySideProjectionForPackedTexture(
    const D3D12SceneRelayProjection& configured,
    std::uint64_t packed_width,
    std::uint32_t packed_height,
    D3D12SceneRelayProjection* output) {
  if (output == nullptr || packed_width < 2 || packed_width % 2 != 0 ||
      packed_height == 0 ||
      !std::isfinite(configured.vertical_fov_degrees) ||
      configured.vertical_fov_degrees <= 1.0F ||
      configured.vertical_fov_degrees >= 179.0F ||
      !std::isfinite(configured.eye_aspect_ratio) ||
      configured.eye_aspect_ratio <= 0.0F) {
    return false;
  }
  // Texture packing controls only the left/right UV regions. Unreal's fake
  // stereo device supplies its own angular projection, which is independent
  // of the packed eye's pixel aspect ratio.
  *output = configured;
  return true;
}

bool mapOpenXrFovToSceneRelayProjection(
    const D3D12SceneRelayProjection& source,
    const VrRuntimeEyeView& target,
    float target_u,
    float target_v,
    D3D12SceneRelayProjectionUv* output) {
  constexpr float kPi = 3.14159265358979323846F;
  if (output == nullptr ||
      !std::isfinite(source.vertical_fov_degrees) ||
      source.vertical_fov_degrees <= 1.0F ||
      source.vertical_fov_degrees >= 179.0F ||
      !std::isfinite(source.eye_aspect_ratio) ||
      source.eye_aspect_ratio <= 0.0F || !std::isfinite(target_u) ||
      !std::isfinite(target_v) || target_u < 0.0F || target_u > 1.0F ||
      target_v < 0.0F || target_v > 1.0F ||
      !std::isfinite(target.fov_angle_left) ||
      !std::isfinite(target.fov_angle_right) ||
      !std::isfinite(target.fov_angle_up) ||
      !std::isfinite(target.fov_angle_down) ||
      target.fov_angle_left >= target.fov_angle_right ||
      target.fov_angle_down >= target.fov_angle_up) {
    return false;
  }
  const float source_tan_y = std::tan(
      source.vertical_fov_degrees * kPi / 360.0F);
  const float source_tan_x = source_tan_y * source.eye_aspect_ratio;
  const float tan_left = std::tan(target.fov_angle_left);
  const float tan_right = std::tan(target.fov_angle_right);
  const float tan_up = std::tan(target.fov_angle_up);
  const float tan_down = std::tan(target.fov_angle_down);
  if (!std::isfinite(source_tan_x) || !std::isfinite(source_tan_y) ||
      source_tan_x <= 0.0F || source_tan_y <= 0.0F ||
      !std::isfinite(tan_left) || !std::isfinite(tan_right) ||
      !std::isfinite(tan_up) || !std::isfinite(tan_down)) {
    return false;
  }
  const float target_tan_x =
      tan_left + (tan_right - tan_left) * target_u;
  const float target_tan_y =
      tan_up + (tan_down - tan_up) * target_v;
  const D3D12SceneRelayProjectionUv mapped{
      (target_tan_x + source_tan_x) / (2.0F * source_tan_x),
      (source_tan_y - target_tan_y) / (2.0F * source_tan_y),
  };
  if (!std::isfinite(mapped.u) || !std::isfinite(mapped.v) ||
      mapped.u < 0.0F || mapped.u > 1.0F || mapped.v < 0.0F ||
      mapped.v > 1.0F) {
    return false;
  }
  *output = mapped;
  return true;
}

struct D3D12SceneRelayRenderer::Impl {
  struct SubmitSlot {
    ComPtr<ID3D12CommandAllocator> allocator;
    UINT64 fence_value = 0;
  };

  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  SubmitSlot slots[kSubmitSlots];
  std::size_t next_slot = 0;
  ComPtr<ID3D12GraphicsCommandList> command_list;
  ComPtr<ID3D12DescriptorHeap> srv_heap;
  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  ComPtr<ID3D12RootSignature> root_signature;
  ComPtr<ID3DBlob> vertex_shader;
  ComPtr<ID3DBlob> pixel_shader;
  ComPtr<ID3D12PipelineState> pipeline;
  ComPtr<ID3D12PipelineState> flat_pipeline;
  ComPtr<ID3D12Fence> fence;
  ComPtr<ID3D12Resource> snapshot;
  HANDLE fence_event = nullptr;
  UINT rtv_descriptor_size = 0;
  UINT64 fence_value = 0;
  DXGI_FORMAT pipeline_format = DXGI_FORMAT_UNKNOWN;
  DXGI_FORMAT flat_pipeline_format = DXGI_FORMAT_UNKNOWN;
  D3D12_RESOURCE_STATES snapshot_state = D3D12_RESOURCE_STATE_COPY_DEST;
  std::atomic<ID3D12CommandList*> pending_inline_command_list{nullptr};
  bool captured = false;
  D3D12SceneRelaySourceLayout source_layout =
      D3D12SceneRelaySourceLayout::Monoscopic;
  D3D12SceneRelayProjection stereo_projection{};
  // Latched after a fence timeout or device removal: the GPU state is no
  // longer trustworthy, so no further allocator resets or submissions are
  // attempted on this renderer.
  bool wedged = false;
  std::mutex mutex;

  ~Impl() {
    if (fence && fence_event != nullptr && !wedged) {
      waitForFence(fence.Get(), fence_value, fence_event, 2'000);
    }
    if (fence_event != nullptr) {
      CloseHandle(fence_event);
    }
  }

  bool deviceHealthy() {
    if (device && FAILED(device->GetDeviceRemovedReason())) {
      wedged = true;
      return false;
    }
    return true;
  }

  // Blocks only when the GPU is more than kSubmitSlots submissions behind.
  bool acquireSlotAndReset() {
    SubmitSlot& slot = slots[next_slot];
    if (!waitForFence(
            fence.Get(), slot.fence_value, fence_event, kFenceTimeoutMs)) {
      wedged = true;
      return false;
    }
    if (FAILED(slot.allocator->Reset()) ||
        FAILED(command_list->Reset(slot.allocator.Get(), nullptr))) {
      deviceHealthy();
      return false;
    }
    return true;
  }

  // Submits the recorded list without a CPU wait; the direct queue serializes
  // relay work against the game's own submissions.
  bool submitCurrentSlot() {
    if (!submitAndSignal(
            queue.Get(), command_list.Get(), fence.Get(), &fence_value)) {
      deviceHealthy();
      return false;
    }
    slots[next_slot].fence_value = fence_value;
    next_slot = (next_slot + 1) % kSubmitSlots;
    return true;
  }

  // Full drain; used only before releasing GPU resources (snapshot rebuild).
  bool drain() {
    if (!waitForFence(fence.Get(), fence_value, fence_event, kFenceTimeoutMs)) {
      wedged = true;
      return false;
    }
    return true;
  }

  bool ensurePipeline(DXGI_FORMAT format, bool flat_output = false) {
    auto& selected = flat_output ? flat_pipeline : pipeline;
    auto& selected_format =
        flat_output ? flat_pipeline_format : pipeline_format;
    if (selected && selected_format == format) {
      return true;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = root_signature.Get();
    desc.VS = {vertex_shader->GetBufferPointer(), vertex_shader->GetBufferSize()};
    desc.PS = {pixel_shader->GetBufferPointer(), pixel_shader->GetBufferSize()};
    desc.BlendState.AlphaToCoverageEnable = FALSE;
    desc.BlendState.IndependentBlendEnable = FALSE;
    auto& blend = desc.BlendState.RenderTarget[0];
    blend.BlendEnable = FALSE;
    blend.LogicOpEnable = FALSE;
    blend.SrcBlend = D3D12_BLEND_ONE;
    blend.DestBlend = D3D12_BLEND_ZERO;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.LogicOp = D3D12_LOGIC_OP_NOOP;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.FrontCounterClockwise = FALSE;
    desc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    desc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    desc.RasterizerState.SlopeScaledDepthBias =
        D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.DepthStencilState.DepthEnable = FALSE;
    desc.DepthStencilState.StencilEnable = FALSE;
    desc.InputLayout = {nullptr, 0};
    desc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = format;
    desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.NodeMask = 0;
    desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    ComPtr<ID3D12PipelineState> created;
    if (FAILED(device->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&created)))) {
      return false;
    }
    selected = std::move(created);
    selected_format = format;
    return true;
  }
};

D3D12SceneRelayRenderer::D3D12SceneRelayRenderer()
    : impl_(std::make_unique<Impl>()) {}

D3D12SceneRelayRenderer::~D3D12SceneRelayRenderer() = default;

bool D3D12SceneRelayRenderer::initialize(
    ID3D12Device* device,
    ID3D12CommandQueue* queue) {
  if (device == nullptr || queue == nullptr ||
      queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
    return false;
  }
  ComPtr<ID3D12Device> queue_device;
  if (FAILED(queue->GetDevice(IID_PPV_ARGS(&queue_device))) ||
      !sameComIdentity(device, queue_device.Get())) {
    return false;
  }

  Impl::SubmitSlot slots[kSubmitSlots];
  for (auto& slot : slots) {
    if (FAILED(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&slot.allocator)))) {
      return false;
    }
  }
  ComPtr<ID3D12GraphicsCommandList> command_list;
  if (FAILED(device->CreateCommandList(
          0, D3D12_COMMAND_LIST_TYPE_DIRECT, slots[0].allocator.Get(), nullptr,
          IID_PPV_ARGS(&command_list))) ||
      FAILED(command_list->Close())) {
    return false;
  }

  D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc{};
  srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  srv_heap_desc.NumDescriptors = 1;
  srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ComPtr<ID3D12DescriptorHeap> srv_heap;
  if (FAILED(device->CreateDescriptorHeap(
          &srv_heap_desc, IID_PPV_ARGS(&srv_heap)))) {
    return false;
  }
  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
  rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_heap_desc.NumDescriptors = 2;
  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  if (FAILED(device->CreateDescriptorHeap(
          &rtv_heap_desc, IID_PPV_ARGS(&rtv_heap)))) {
    return false;
  }

  D3D12_DESCRIPTOR_RANGE range{};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  range.NumDescriptors = 1;
  range.BaseShaderRegister = 0;
  range.RegisterSpace = 0;
  range.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
  D3D12_ROOT_PARAMETER parameters[2]{};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &range;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants.ShaderRegister = 0;
  parameters[1].Constants.RegisterSpace = 0;
  parameters[1].Constants.Num32BitValues = 11;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_STATIC_SAMPLER_DESC sampler{};
  sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.MipLODBias = 0.0F;
  sampler.MaxAnisotropy = 1;
  sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
  sampler.MinLOD = 0.0F;
  sampler.MaxLOD = FLT_MAX;
  sampler.ShaderRegister = 0;
  sampler.RegisterSpace = 0;
  sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC root_desc{};
  root_desc.NumParameters = 2;
  root_desc.pParameters = parameters;
  root_desc.NumStaticSamplers = 1;
  root_desc.pStaticSamplers = &sampler;
  root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  ComPtr<ID3DBlob> serialized;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3D12SerializeRootSignature(
          &root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
          &serialized, &errors))) {
    return false;
  }
  ComPtr<ID3D12RootSignature> root_signature;
  if (FAILED(device->CreateRootSignature(
          0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
          IID_PPV_ARGS(&root_signature)))) {
    return false;
  }
  ComPtr<ID3DBlob> vertex_shader;
  ComPtr<ID3DBlob> pixel_shader;
  if (FAILED(D3DCompile(
          kRelayShader, sizeof(kRelayShader) - 1, "scene-relay", nullptr,
          nullptr, "VSMain", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
          &vertex_shader, &errors)) ||
      FAILED(D3DCompile(
          kRelayShader, sizeof(kRelayShader) - 1, "scene-relay", nullptr,
          nullptr, "PSMain", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
          &pixel_shader, &errors))) {
    return false;
  }
  ComPtr<ID3D12Fence> fence;
  if (FAILED(device->CreateFence(
          0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
    return false;
  }
  HANDLE fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (fence_event == nullptr) {
    return false;
  }

  std::scoped_lock lock(impl_->mutex);
  if (impl_->fence_event != nullptr) {
    CloseHandle(impl_->fence_event);
  }
  impl_->device = device;
  impl_->queue = queue;
  for (std::size_t index = 0; index < kSubmitSlots; ++index) {
    impl_->slots[index] = std::move(slots[index]);
  }
  impl_->next_slot = 0;
  impl_->command_list = std::move(command_list);
  impl_->srv_heap = std::move(srv_heap);
  impl_->rtv_heap = std::move(rtv_heap);
  impl_->root_signature = std::move(root_signature);
  impl_->vertex_shader = std::move(vertex_shader);
  impl_->pixel_shader = std::move(pixel_shader);
  impl_->pipeline.Reset();
  impl_->flat_pipeline.Reset();
  impl_->fence = std::move(fence);
  impl_->snapshot.Reset();
  impl_->fence_event = fence_event;
  impl_->rtv_descriptor_size = device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  impl_->fence_value = 0;
  impl_->pipeline_format = DXGI_FORMAT_UNKNOWN;
  impl_->flat_pipeline_format = DXGI_FORMAT_UNKNOWN;
  impl_->snapshot_state = D3D12_RESOURCE_STATE_COPY_DEST;
  impl_->captured = false;
  impl_->source_layout = D3D12SceneRelaySourceLayout::Monoscopic;
  impl_->wedged = false;
  return true;
}

bool D3D12SceneRelayRenderer::configureSideBySideProjection(
    const D3D12SceneRelayProjection& projection) {
  VrRuntimeEyeView widest_target{};
  widest_target.fov_angle_left = -0.01F;
  widest_target.fov_angle_right = 0.01F;
  widest_target.fov_angle_up = 0.01F;
  widest_target.fov_angle_down = -0.01F;
  D3D12SceneRelayProjectionUv ignored{};
  if (!mapOpenXrFovToSceneRelayProjection(
          projection, widest_target, 0.5F, 0.5F, &ignored)) {
    return false;
  }
  std::scoped_lock lock(impl_->mutex);
  impl_->stereo_projection = projection;
  return true;
}

VrRuntimeResult D3D12SceneRelayRenderer::capture(
    ID3D12Resource* source,
    D3D12_RESOURCE_STATES source_state) {
  return captureWithLayout(
      source, source_state, D3D12SceneRelaySourceLayout::Monoscopic);
}

VrRuntimeResult D3D12SceneRelayRenderer::captureSideBySideStereo(
    ID3D12Resource* source,
    D3D12_RESOURCE_STATES source_state) {
  return captureWithLayout(
      source, source_state, D3D12SceneRelaySourceLayout::SideBySideStereo);
}

VrRuntimeResult D3D12SceneRelayRenderer::recordInlineCapture(
    ID3D12GraphicsCommandList* producer_command_list,
    ID3D12Resource* source,
    D3D12_RESOURCE_STATES source_state) {
  if (producer_command_list == nullptr || source == nullptr ||
      source_state == 0 ||
      producer_command_list->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }
  std::scoped_lock lock(impl_->mutex);
  if (!impl_->device || !impl_->queue || !impl_->fence ||
      impl_->fence_event == nullptr || impl_->wedged ||
      impl_->pending_inline_command_list.load(std::memory_order_acquire) !=
          nullptr) {
    return VR_RUNTIME_ERROR_STATE;
  }
  ComPtr<ID3D12Device> source_device;
  if (FAILED(source->GetDevice(IID_PPV_ARGS(&source_device))) ||
      !sameComIdentity(impl_->device.Get(), source_device.Get())) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }
  const D3D12_RESOURCE_DESC source_desc = source->GetDesc();
  if (source_desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
      source_desc.Width == 0 || source_desc.Height == 0 ||
      source_desc.DepthOrArraySize != 1 || source_desc.MipLevels != 1 ||
      source_desc.SampleDesc.Count != 1 ||
      source_desc.Format == DXGI_FORMAT_UNKNOWN) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }
  if (!impl_->snapshot ||
      !sameTextureShape(source_desc, impl_->snapshot->GetDesc())) {
    if (impl_->snapshot && !impl_->drain()) {
      impl_->captured = false;
      return VR_RUNTIME_ERROR_GRAPHICS;
    }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC snapshot_desc = source_desc;
    snapshot_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    ComPtr<ID3D12Resource> snapshot;
    if (FAILED(impl_->device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &snapshot_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&snapshot)))) {
      impl_->captured = false;
      return VR_RUNTIME_ERROR_GRAPHICS;
    }
    impl_->snapshot = std::move(snapshot);
    impl_->snapshot_state = D3D12_RESOURCE_STATE_COPY_DEST;
    impl_->captured = false;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = source_desc.Format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MostDetailedMip = 0;
    srv.Texture2D.MipLevels = 1;
    impl_->device->CreateShaderResourceView(
        impl_->snapshot.Get(), &srv,
        impl_->srv_heap->GetCPUDescriptorHandleForHeapStart());
  }
  if (impl_->snapshot_state != D3D12_RESOURCE_STATE_COPY_DEST) {
    const auto snapshot_to_copy = transition(
        impl_->snapshot.Get(), impl_->snapshot_state,
        D3D12_RESOURCE_STATE_COPY_DEST);
    producer_command_list->ResourceBarrier(1, &snapshot_to_copy);
  }
  if (source_state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
    const auto source_to_copy = transition(
        source, source_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    producer_command_list->ResourceBarrier(1, &source_to_copy);
  }
  producer_command_list->CopyResource(impl_->snapshot.Get(), source);
  if (source_state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
    const auto source_restore = transition(
        source, D3D12_RESOURCE_STATE_COPY_SOURCE, source_state);
    producer_command_list->ResourceBarrier(1, &source_restore);
  }
  const auto snapshot_to_read = transition(
      impl_->snapshot.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  producer_command_list->ResourceBarrier(1, &snapshot_to_read);
  impl_->snapshot_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  impl_->pending_inline_command_list.store(
      producer_command_list, std::memory_order_release);
  return VR_RUNTIME_OK;
}

bool D3D12SceneRelayRenderer::publishInlineCapture(
    ID3D12CommandList* executed_command_list) {
  if (executed_command_list == nullptr) {
    return false;
  }
  if (impl_->pending_inline_command_list.load(std::memory_order_acquire) !=
      executed_command_list) {
    return false;
  }
  std::scoped_lock lock(impl_->mutex);
  if (impl_->pending_inline_command_list.load(std::memory_order_relaxed) !=
      executed_command_list) {
    return false;
  }
  impl_->pending_inline_command_list.store(nullptr, std::memory_order_release);
  impl_->source_layout = D3D12SceneRelaySourceLayout::Monoscopic;
  impl_->captured = true;
  return true;
}

VrRuntimeResult D3D12SceneRelayRenderer::captureWithLayout(
    ID3D12Resource* source,
    D3D12_RESOURCE_STATES source_state,
    D3D12SceneRelaySourceLayout layout) {
  if (source == nullptr) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }
  std::scoped_lock lock(impl_->mutex);
  if (!impl_->device || !impl_->queue || !impl_->command_list ||
      !impl_->fence || impl_->fence_event == nullptr) {
    return VR_RUNTIME_ERROR_STATE;
  }
  if (impl_->wedged) {
    return VR_RUNTIME_ERROR_GRAPHICS;
  }
  ComPtr<ID3D12Device> source_device;
  if (FAILED(source->GetDevice(IID_PPV_ARGS(&source_device))) ||
      !sameComIdentity(impl_->device.Get(), source_device.Get())) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }
  const D3D12_RESOURCE_DESC source_desc = source->GetDesc();
  if (source_desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
      source_desc.Width == 0 || source_desc.Height == 0 ||
      source_desc.DepthOrArraySize != 1 || source_desc.MipLevels != 1 ||
      source_desc.SampleDesc.Count != 1 ||
      source_desc.Format == DXGI_FORMAT_UNKNOWN) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }
  if (layout == D3D12SceneRelaySourceLayout::SideBySideStereo &&
      !sideBySideProjectionForPackedTexture(
          impl_->stereo_projection, source_desc.Width, source_desc.Height,
          &impl_->stereo_projection)) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }

  if (!impl_->snapshot ||
      !sameTextureShape(source_desc, impl_->snapshot->GetDesc())) {
    // In-flight submissions may still reference the old snapshot and its SRV
    // descriptor; drain before releasing or redescribing either.
    if (impl_->snapshot && !impl_->drain()) {
      impl_->captured = false;
      return VR_RUNTIME_ERROR_GRAPHICS;
    }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC snapshot_desc = source_desc;
    snapshot_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    ComPtr<ID3D12Resource> snapshot;
    if (FAILED(impl_->device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &snapshot_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&snapshot)))) {
      impl_->captured = false;
      return VR_RUNTIME_ERROR_GRAPHICS;
    }
    impl_->snapshot = std::move(snapshot);
    impl_->snapshot_state = D3D12_RESOURCE_STATE_COPY_DEST;
    impl_->captured = false;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = source_desc.Format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MostDetailedMip = 0;
    srv.Texture2D.MipLevels = 1;
    srv.Texture2D.PlaneSlice = 0;
    srv.Texture2D.ResourceMinLODClamp = 0.0F;
    impl_->device->CreateShaderResourceView(
        impl_->snapshot.Get(), &srv,
        impl_->srv_heap->GetCPUDescriptorHandleForHeapStart());
  }

  if (!impl_->acquireSlotAndReset()) {
    impl_->captured = false;
    return VR_RUNTIME_ERROR_GRAPHICS;
  }
  if (impl_->snapshot_state != D3D12_RESOURCE_STATE_COPY_DEST) {
    const auto barrier = transition(
        impl_->snapshot.Get(), impl_->snapshot_state,
        D3D12_RESOURCE_STATE_COPY_DEST);
    impl_->command_list->ResourceBarrier(1, &barrier);
  }
  if (source_state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
    const auto barrier = transition(
        source, source_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    impl_->command_list->ResourceBarrier(1, &barrier);
  }
  impl_->command_list->CopyResource(impl_->snapshot.Get(), source);
  if (source_state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
    const auto barrier = transition(
        source, D3D12_RESOURCE_STATE_COPY_SOURCE, source_state);
    impl_->command_list->ResourceBarrier(1, &barrier);
  }
  const auto snapshot_barrier = transition(
      impl_->snapshot.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  impl_->command_list->ResourceBarrier(1, &snapshot_barrier);
  if (layout == D3D12SceneRelaySourceLayout::SideBySideStereo) {
    if (!impl_->ensurePipeline(source_desc.Format, true)) {
      impl_->captured = false;
      return VR_RUNTIME_ERROR_GRAPHICS;
    }
    if (source_state != D3D12_RESOURCE_STATE_RENDER_TARGET) {
      const auto barrier = transition(
          source, source_state, D3D12_RESOURCE_STATE_RENDER_TARGET);
      impl_->command_list->ResourceBarrier(1, &barrier);
    }
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc{};
    rtv_desc.Format = source_desc.Format;
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtv_desc.Texture2D.MipSlice = 0;
    rtv_desc.Texture2D.PlaneSlice = 0;
    const auto rtv = impl_->rtv_heap->GetCPUDescriptorHandleForHeapStart();
    impl_->device->CreateRenderTargetView(source, &rtv_desc, rtv);
    impl_->command_list->SetPipelineState(impl_->flat_pipeline.Get());
    ID3D12DescriptorHeap* heaps[] = {impl_->srv_heap.Get()};
    impl_->command_list->SetDescriptorHeaps(1, heaps);
    impl_->command_list->SetGraphicsRootSignature(impl_->root_signature.Get());
    impl_->command_list->SetGraphicsRootDescriptorTable(
        0, impl_->srv_heap->GetGPUDescriptorHandleForHeapStart());
    const auto left_uv = sceneRelayUvTransform(
        layout, VR_RUNTIME_EYE_LEFT);
    const float uv_constants[11]{
        left_uv.scale_u,
        left_uv.bias_u,
        0.5F / static_cast<float>(source_desc.Width),
        0.5F / static_cast<float>(source_desc.Height),
        0.0F,
        1.0F,
        1.0F,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
    };
    impl_->command_list->SetGraphicsRoot32BitConstants(
        1, 11, uv_constants, 0);
    impl_->command_list->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    impl_->command_list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(source_desc.Width);
    viewport.Height = static_cast<float>(source_desc.Height);
    viewport.MinDepth = 0.0F;
    viewport.MaxDepth = 1.0F;
    const D3D12_RECT scissor{
        0, 0, static_cast<LONG>(source_desc.Width),
        static_cast<LONG>(source_desc.Height)};
    impl_->command_list->RSSetViewports(1, &viewport);
    impl_->command_list->RSSetScissorRects(1, &scissor);
    impl_->command_list->DrawInstanced(3, 1, 0, 0);
    if (source_state != D3D12_RESOURCE_STATE_RENDER_TARGET) {
      const auto barrier = transition(
          source, D3D12_RESOURCE_STATE_RENDER_TARGET, source_state);
      impl_->command_list->ResourceBarrier(1, &barrier);
    }
  }
  // Tracked state advances only after a successful submission; a dropped
  // command list leaves the GPU (and the tracker) at the previous state.
  if (!impl_->submitCurrentSlot()) {
    impl_->captured = false;
    return VR_RUNTIME_ERROR_GRAPHICS;
  }
  impl_->snapshot_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  impl_->source_layout = layout;
  impl_->captured = true;
  return VR_RUNTIME_OK;
}

VrRuntimeResult D3D12SceneRelayRenderer::render(
    const VrRuntimeFrameData& frame,
    const VrRuntimeRenderTarget* targets,
    uint32_t target_count) {
  std::scoped_lock lock(impl_->mutex);
  if (!impl_->device || !impl_->queue || !impl_->command_list ||
      !impl_->root_signature || !impl_->srv_heap || !impl_->rtv_heap ||
      !impl_->fence || impl_->fence_event == nullptr) {
    return VR_RUNTIME_ERROR_STATE;
  }
  if (impl_->wedged) {
    return VR_RUNTIME_ERROR_GRAPHICS;
  }
  if (!impl_->captured || !impl_->snapshot) {
    return VR_RUNTIME_SKIPPED;
  }
  UnrealFrameSubmission submission{};
  if (makeUnrealFrameSubmission(frame, targets, target_count, &submission) !=
      UnrealOpenXrBridgeResult::Ready) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }
  const auto target_format =
      static_cast<DXGI_FORMAT>(submission.eyes[0].color_format);
  if (submission.eyes[1].color_format != submission.eyes[0].color_format ||
      !impl_->ensurePipeline(target_format)) {
    return VR_RUNTIME_ERROR_GRAPHICS;
  }
  if (impl_->source_layout ==
      D3D12SceneRelaySourceLayout::SideBySideStereo) {
    for (std::size_t eye = 0; eye < 2; ++eye) {
      D3D12SceneRelayProjectionUv corner{};
      if (!mapOpenXrFovToSceneRelayProjection(
              impl_->stereo_projection, frame.eyes[eye], 0.0F, 0.0F,
              &corner) ||
          !mapOpenXrFovToSceneRelayProjection(
              impl_->stereo_projection, frame.eyes[eye], 1.0F, 1.0F,
              &corner)) {
        return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
      }
    }
  }
  if (!impl_->acquireSlotAndReset()) {
    return VR_RUNTIME_ERROR_GRAPHICS;
  }
  impl_->command_list->SetPipelineState(impl_->pipeline.Get());
  ID3D12DescriptorHeap* heaps[] = {impl_->srv_heap.Get()};
  impl_->command_list->SetDescriptorHeaps(1, heaps);
  impl_->command_list->SetGraphicsRootSignature(impl_->root_signature.Get());
  impl_->command_list->SetGraphicsRootDescriptorTable(
      0, impl_->srv_heap->GetGPUDescriptorHandleForHeapStart());
  impl_->command_list->IASetPrimitiveTopology(
      D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  D3D12_CPU_DESCRIPTOR_HANDLE rtv =
      impl_->rtv_heap->GetCPUDescriptorHandleForHeapStart();
  for (uint32_t eye = 0; eye < 2; ++eye) {
    auto* texture =
        static_cast<ID3D12Resource*>(submission.eyes[eye].color_target);
    if (submission.eyes[eye].color_array_index >=
        texture->GetDesc().DepthOrArraySize) {
      return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
    }
    const auto rtv_desc = makeSliceRtvDesc(
        texture, target_format, submission.eyes[eye].color_array_index);
    impl_->device->CreateRenderTargetView(texture, &rtv_desc, rtv);
    impl_->command_list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(submission.eyes[eye].width);
    viewport.Height = static_cast<float>(submission.eyes[eye].height);
    viewport.MinDepth = 0.0F;
    viewport.MaxDepth = 1.0F;
    D3D12_RECT scissor{
        0, 0,
        static_cast<LONG>(submission.eyes[eye].width),
        static_cast<LONG>(submission.eyes[eye].height)};
    impl_->command_list->RSSetViewports(1, &viewport);
    impl_->command_list->RSSetScissorRects(1, &scissor);
    const auto uv = sceneRelayUvTransform(
        impl_->source_layout, static_cast<VrRuntimeEye>(eye));
    constexpr float kPi = 3.14159265358979323846F;
    const float source_tan_y = std::tan(
        impl_->stereo_projection.vertical_fov_degrees * kPi / 360.0F);
    const float source_tan_x =
        source_tan_y * impl_->stereo_projection.eye_aspect_ratio;
    const bool warp = impl_->source_layout ==
        D3D12SceneRelaySourceLayout::SideBySideStereo;
    const float uv_constants[11]{
        uv.scale_u,
        uv.bias_u,
        0.5F / static_cast<float>(impl_->snapshot->GetDesc().Width),
        0.5F / static_cast<float>(impl_->snapshot->GetDesc().Height),
        warp ? 1.0F : 0.0F,
        warp ? source_tan_x : 1.0F,
        warp ? source_tan_y : 1.0F,
        warp ? std::tan(frame.eyes[eye].fov_angle_left) : 0.0F,
        warp ? std::tan(frame.eyes[eye].fov_angle_right) : 0.0F,
        warp ? std::tan(frame.eyes[eye].fov_angle_up) : 0.0F,
        warp ? std::tan(frame.eyes[eye].fov_angle_down) : 0.0F,
    };
    impl_->command_list->SetGraphicsRoot32BitConstants(
        1, 11, uv_constants, 0);
    impl_->command_list->DrawInstanced(3, 1, 0, 0);
    rtv.ptr += impl_->rtv_descriptor_size;
  }
  // OpenXR's D3D12 binding requires eye-target work to be submitted on the
  // bound queue before the swapchain image is released, not CPU-complete.
  return impl_->submitCurrentSlot() ? VR_RUNTIME_OK
                                    : VR_RUNTIME_ERROR_GRAPHICS;
}

VrRuntimeResult D3D12SceneRelayRenderer::readbackCapturedRgba8(
    std::vector<std::uint8_t>* pixels,
    std::uint32_t* width,
    std::uint32_t* height) {
  if (pixels == nullptr || width == nullptr || height == nullptr) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }
  pixels->clear();
  *width = 0;
  *height = 0;
  std::scoped_lock lock(impl_->mutex);
  if (!impl_->captured || !impl_->snapshot || !impl_->device ||
      !impl_->command_list || !impl_->fence || impl_->fence_event == nullptr) {
    return VR_RUNTIME_ERROR_STATE;
  }
  if (impl_->wedged) {
    return VR_RUNTIME_ERROR_GRAPHICS;
  }
  const D3D12_RESOURCE_DESC description = impl_->snapshot->GetDesc();
  if ((description.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
       description.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) ||
      description.Width > UINT32_MAX || description.Height == 0) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT rows = 0;
  UINT64 row_size = 0;
  UINT64 total_size = 0;
  impl_->device->GetCopyableFootprints(
      &description, 0, 1, 0, &footprint, &rows, &row_size, &total_size);
  if (rows != description.Height || row_size < description.Width * 4 ||
      total_size == 0) {
    return VR_RUNTIME_ERROR_GRAPHICS;
  }
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC buffer{};
  buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer.Width = total_size;
  buffer.Height = 1;
  buffer.DepthOrArraySize = 1;
  buffer.MipLevels = 1;
  buffer.SampleDesc.Count = 1;
  buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> readback;
  if (FAILED(impl_->device->CreateCommittedResource(
          &heap, D3D12_HEAP_FLAG_NONE, &buffer,
          D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
          IID_PPV_ARGS(&readback))) ||
      !impl_->acquireSlotAndReset()) {
    return VR_RUNTIME_ERROR_GRAPHICS;
  }
  const D3D12_RESOURCE_STATES original_state = impl_->snapshot_state;
  if (original_state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
    const auto barrier = transition(
        impl_->snapshot.Get(), original_state,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    impl_->command_list->ResourceBarrier(1, &barrier);
  }
  D3D12_TEXTURE_COPY_LOCATION destination{};
  destination.pResource = readback.Get();
  destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  destination.PlacedFootprint = footprint;
  D3D12_TEXTURE_COPY_LOCATION source{};
  source.pResource = impl_->snapshot.Get();
  source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  source.SubresourceIndex = 0;
  impl_->command_list->CopyTextureRegion(
      &destination, 0, 0, 0, &source, nullptr);
  if (original_state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
    const auto barrier = transition(
        impl_->snapshot.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
        original_state);
    impl_->command_list->ResourceBarrier(1, &barrier);
  }
  if (!impl_->submitCurrentSlot() ||
      !waitForFence(
          impl_->fence.Get(), impl_->fence_value, impl_->fence_event,
          kFenceTimeoutMs)) {
    impl_->wedged = true;
    return VR_RUNTIME_ERROR_GRAPHICS;
  }
  const D3D12_RANGE read_range{0, static_cast<SIZE_T>(total_size)};
  void* mapped = nullptr;
  if (FAILED(readback->Map(0, &read_range, &mapped)) || mapped == nullptr) {
    return VR_RUNTIME_ERROR_GRAPHICS;
  }
  const std::size_t output_row = static_cast<std::size_t>(description.Width) * 4;
  pixels->resize(output_row * description.Height);
  for (std::uint32_t row = 0; row < description.Height; ++row) {
    std::memcpy(
        pixels->data() + static_cast<std::size_t>(row) * output_row,
        static_cast<const std::uint8_t*>(mapped) +
            static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
        output_row);
  }
  const D3D12_RANGE written_range{0, 0};
  readback->Unmap(0, &written_range);
  *width = static_cast<std::uint32_t>(description.Width);
  *height = description.Height;
  return VR_RUNTIME_OK;
}

}  // namespace vrclient::adapters::unreal

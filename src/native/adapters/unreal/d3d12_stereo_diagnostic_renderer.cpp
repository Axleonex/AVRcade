#include "adapters/unreal/d3d12_stereo_diagnostic_renderer.h"

#include "adapters/unreal/d3d12_render_util.h"
#include "adapters/unreal/unreal_openxr_bridge.h"

#include <array>

namespace vrclient::adapters::unreal {
namespace {

constexpr std::array<std::array<float, 4>, 2> kEyeColors{{
    {0.8F, 0.1F, 0.1F, 1.0F},
    {0.1F, 0.2F, 0.8F, 1.0F},
}};

constexpr DWORD kFenceTimeoutMs = 10'000;

}  // namespace

D3D12StereoDiagnosticRenderer::~D3D12StereoDiagnosticRenderer() {
  if (fence_event_ != nullptr) {
    CloseHandle(fence_event_);
  }
}

bool D3D12StereoDiagnosticRenderer::initialize(
    ID3D12Device* device,
    ID3D12CommandQueue* queue) {
  if (device == nullptr || queue == nullptr ||
      queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
    return false;
  }
  Microsoft::WRL::ComPtr<ID3D12Device> queue_device;
  if (FAILED(queue->GetDevice(IID_PPV_ARGS(&queue_device))) ||
      !sameComIdentity(device, queue_device.Get())) {
    return false;
  }

  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
  if (FAILED(device->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_DIRECT,
          IID_PPV_ARGS(&allocator)))) {
    return false;
  }
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list;
  if (FAILED(device->CreateCommandList(
          0,
          D3D12_COMMAND_LIST_TYPE_DIRECT,
          allocator.Get(),
          nullptr,
          IID_PPV_ARGS(&command_list))) ||
      FAILED(command_list->Close())) {
    return false;
  }

  D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  heap_desc.NumDescriptors = 2;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap;
  if (FAILED(device->CreateDescriptorHeap(
          &heap_desc,
          IID_PPV_ARGS(&rtv_heap)))) {
    return false;
  }
  Microsoft::WRL::ComPtr<ID3D12Fence> fence;
  if (FAILED(device->CreateFence(
          0,
          D3D12_FENCE_FLAG_NONE,
          IID_PPV_ARGS(&fence)))) {
    return false;
  }
  HANDLE fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (fence_event == nullptr) {
    return false;
  }

  if (fence_event_ != nullptr) {
    CloseHandle(fence_event_);
  }
  device_ = device;
  queue_ = queue;
  allocator_ = std::move(allocator);
  command_list_ = std::move(command_list);
  rtv_heap_ = std::move(rtv_heap);
  fence_ = std::move(fence);
  fence_event_ = fence_event;
  rtv_descriptor_size_ =
      device->GetDescriptorHandleIncrementSize(
          D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  fence_value_ = 0;
  return true;
}

VrRuntimeResult D3D12StereoDiagnosticRenderer::render(
    const VrRuntimeFrameData& frame,
    const VrRuntimeRenderTarget* targets,
    uint32_t target_count) {
  if (!device_ || !queue_ || !allocator_ || !command_list_ || !rtv_heap_ ||
      !fence_ || fence_event_ == nullptr) {
    return VR_RUNTIME_ERROR_STATE;
  }
  UnrealFrameSubmission submission{};
  if (makeUnrealFrameSubmission(
          frame,
          targets,
          target_count,
          &submission) != UnrealOpenXrBridgeResult::Ready) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }

  if (FAILED(allocator_->Reset()) ||
      FAILED(command_list_->Reset(allocator_.Get(), nullptr))) {
    return VR_RUNTIME_ERROR_GRAPHICS;
  }

  D3D12_CPU_DESCRIPTOR_HANDLE rtv =
      rtv_heap_->GetCPUDescriptorHandleForHeapStart();
  for (uint32_t eye = 0; eye < 2; ++eye) {
    auto* texture =
        static_cast<ID3D12Resource*>(submission.eyes[eye].color_target);
    const D3D12_RESOURCE_DESC texture_desc = texture->GetDesc();
    if (submission.eyes[eye].color_array_index >=
        texture_desc.DepthOrArraySize) {
      return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
    }
    const auto rtv_desc = makeSliceRtvDesc(
        texture,
        static_cast<DXGI_FORMAT>(submission.eyes[eye].color_format),
        submission.eyes[eye].color_array_index);
    device_->CreateRenderTargetView(texture, &rtv_desc, rtv);
    // XR_KHR_D3D12_enable guarantees a waited color swapchain image is already
    // in RENDER_TARGET state and requires that same state at release.
    command_list_->ClearRenderTargetView(
        rtv,
        kEyeColors[eye].data(),
        0,
        nullptr);
    rtv.ptr += rtv_descriptor_size_;
  }

  if (!submitAndSignal(
          queue_.Get(), command_list_.Get(), fence_.Get(), &fence_value_) ||
      !waitForFence(fence_.Get(), fence_value_, fence_event_, kFenceTimeoutMs)) {
    return VR_RUNTIME_ERROR_GRAPHICS;
  }
  return VR_RUNTIME_OK;
}

}  // namespace vrclient::adapters::unreal

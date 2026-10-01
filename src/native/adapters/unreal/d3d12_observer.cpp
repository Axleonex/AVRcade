#include "adapters/unreal/d3d12_observer.h"

#include "adapters/unreal/d3d12_render_util.h"

namespace vrclient::adapters::unreal {
namespace {

bool supportedColorFormat(DXGI_FORMAT format) {
  switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
      return true;
    default:
      return false;
  }
}

}  // namespace

D3D12ObservationResult D3D12Observer::observeDeviceQueue(
    ID3D12Device* device,
    ID3D12CommandQueue* queue) {
  if (device == nullptr || queue == nullptr) {
    return D3D12ObservationResult::InvalidArgument;
  }
  if (queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
    return D3D12ObservationResult::QueueNotDirect;
  }

  Microsoft::WRL::ComPtr<ID3D12Device> queue_device;
  if (FAILED(queue->GetDevice(IID_PPV_ARGS(&queue_device))) ||
      !sameComIdentity(device, queue_device.Get())) {
    return D3D12ObservationResult::QueueDeviceMismatch;
  }

  std::lock_guard lock(mutex_);
  device_ = device;
  queue_ = queue;
  swapchain_.Reset();
  swapchain_desc_ = {};
  present_count_ = 0;
  resize_count_ = 0;
  return D3D12ObservationResult::Pending;
}

D3D12ObservationResult D3D12Observer::observeSwapchain(
    IDXGISwapChain3* swapchain) {
  if (swapchain == nullptr) {
    return D3D12ObservationResult::InvalidArgument;
  }

  DXGI_SWAP_CHAIN_DESC1 desc{};
  if (FAILED(swapchain->GetDesc1(&desc)) || desc.BufferCount < 2 ||
      desc.Width == 0 || desc.Height == 0 || !supportedColorFormat(desc.Format)) {
    return D3D12ObservationResult::UnsupportedSwapchain;
  }

  Microsoft::WRL::ComPtr<ID3D12Device> swapchain_device;
  if (FAILED(swapchain->GetDevice(IID_PPV_ARGS(&swapchain_device)))) {
    return D3D12ObservationResult::SwapchainDeviceMismatch;
  }

  std::lock_guard lock(mutex_);
  if (!device_ || !sameComIdentity(device_.Get(), swapchain_device.Get())) {
    return D3D12ObservationResult::SwapchainDeviceMismatch;
  }
  swapchain_ = swapchain;
  swapchain_desc_ = desc;
  return D3D12ObservationResult::Ready;
}

void D3D12Observer::recordPresent() {
  std::lock_guard lock(mutex_);
  if (swapchain_) {
    ++present_count_;
  }
}

void D3D12Observer::recordResize() {
  std::lock_guard lock(mutex_);
  if (swapchain_) {
    ++resize_count_;
  }
}

D3D12ObservationSnapshot D3D12Observer::snapshot() const {
  std::lock_guard lock(mutex_);
  D3D12ObservationSnapshot out;
  out.device = device_.Get();
  out.queue = queue_.Get();
  out.swapchain = swapchain_.Get();
  out.adapter_luid = device_ ? device_->GetAdapterLuid() : LUID{};
  out.format = swapchain_desc_.Format;
  out.width = swapchain_desc_.Width;
  out.height = swapchain_desc_.Height;
  out.buffer_count = swapchain_desc_.BufferCount;
  out.present_count = present_count_;
  out.resize_count = resize_count_;
  out.ready = device_ && queue_ && swapchain_;
  return out;
}

vrclient::runtime::openxr::D3D12BorrowedBinding
D3D12Observer::borrowedBinding() const {
  std::lock_guard lock(mutex_);
  return {device_.Get(), queue_.Get()};
}

void D3D12Observer::reset() {
  std::lock_guard lock(mutex_);
  swapchain_.Reset();
  queue_.Reset();
  device_.Reset();
  swapchain_desc_ = {};
  present_count_ = 0;
  resize_count_ = 0;
}

}  // namespace vrclient::adapters::unreal

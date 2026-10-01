#pragma once

#include "openxr/d3d12_binding.h"

#include <cstdint>
#include <mutex>

namespace vrclient::adapters::unreal {

enum class D3D12ObservationResult {
  Ready,
  Pending,
  InvalidArgument,
  QueueNotDirect,
  QueueDeviceMismatch,
  SwapchainDeviceMismatch,
  UnsupportedSwapchain,
};

struct D3D12ObservationSnapshot {
  ID3D12Device* device = nullptr;
  ID3D12CommandQueue* queue = nullptr;
  IDXGISwapChain3* swapchain = nullptr;
  LUID adapter_luid{};
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t buffer_count = 0;
  std::uint64_t present_count = 0;
  std::uint64_t resize_count = 0;
  bool ready = false;
};

class D3D12Observer {
 public:
  D3D12ObservationResult observeDeviceQueue(
      ID3D12Device* device,
      ID3D12CommandQueue* queue);
  D3D12ObservationResult observeSwapchain(IDXGISwapChain3* swapchain);
  void recordPresent();
  void recordResize();
  D3D12ObservationSnapshot snapshot() const;
  vrclient::runtime::openxr::D3D12BorrowedBinding borrowedBinding() const;
  void reset();

 private:
  mutable std::mutex mutex_;
  Microsoft::WRL::ComPtr<ID3D12Device> device_;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
  Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain_;
  DXGI_SWAP_CHAIN_DESC1 swapchain_desc_{};
  std::uint64_t present_count_ = 0;
  std::uint64_t resize_count_ = 0;
};

}  // namespace vrclient::adapters::unreal

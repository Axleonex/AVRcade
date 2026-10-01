#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace vrclient::runtime::openxr {

struct D3D12BorrowedBinding {
  ID3D12Device* device = nullptr;
  ID3D12CommandQueue* queue = nullptr;
};

struct D3D12DeviceContext {
  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
  Microsoft::WRL::ComPtr<ID3D12Device> device;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
  bool borrowed = false;

  void destroy() {
    queue.Reset();
    device.Reset();
    adapter.Reset();
    borrowed = false;
  }
};

enum class D3D12BindingAdoptResult {
  Ready,
  InvalidArgument,
  QueueNotDirect,
  QueueDeviceMismatch,
  AdapterMismatch,
  FeatureLevelUnsupported,
};

D3D12BindingAdoptResult adoptBorrowedD3D12Binding(
    const LUID& required_adapter_luid,
    D3D_FEATURE_LEVEL minimum_feature_level,
    const D3D12BorrowedBinding& binding,
    D3D12DeviceContext* out);

}  // namespace vrclient::runtime::openxr

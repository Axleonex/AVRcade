#pragma once

#include "public/vr_runtime_api.h"

#include <d3d12.h>
#include <wrl/client.h>

namespace vrclient::adapters::unreal {

class D3D12StereoDiagnosticRenderer {
 public:
  D3D12StereoDiagnosticRenderer() = default;
  ~D3D12StereoDiagnosticRenderer();

  D3D12StereoDiagnosticRenderer(const D3D12StereoDiagnosticRenderer&) = delete;
  D3D12StereoDiagnosticRenderer& operator=(
      const D3D12StereoDiagnosticRenderer&) = delete;

  bool initialize(ID3D12Device* device, ID3D12CommandQueue* queue);
  VrRuntimeResult render(
      const VrRuntimeFrameData& frame,
      const VrRuntimeRenderTarget* targets,
      uint32_t target_count);

 private:
  Microsoft::WRL::ComPtr<ID3D12Device> device_;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator_;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
  HANDLE fence_event_ = nullptr;
  UINT rtv_descriptor_size_ = 0;
  UINT64 fence_value_ = 0;
};

}  // namespace vrclient::adapters::unreal

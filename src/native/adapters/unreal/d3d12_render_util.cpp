#include "adapters/unreal/d3d12_render_util.h"

namespace vrclient::adapters::unreal {

bool sameComIdentity(IUnknown* left, IUnknown* right) {
  if (left == nullptr || right == nullptr) {
    return false;
  }
  Microsoft::WRL::ComPtr<IUnknown> left_identity;
  Microsoft::WRL::ComPtr<IUnknown> right_identity;
  return SUCCEEDED(left->QueryInterface(IID_PPV_ARGS(&left_identity))) &&
      SUCCEEDED(right->QueryInterface(IID_PPV_ARGS(&right_identity))) &&
      left_identity.Get() == right_identity.Get();
}

D3D12_RENDER_TARGET_VIEW_DESC makeSliceRtvDesc(
    ID3D12Resource* texture,
    DXGI_FORMAT format,
    std::uint32_t array_slice) {
  const D3D12_RESOURCE_DESC texture_desc = texture->GetDesc();
  D3D12_RENDER_TARGET_VIEW_DESC out{};
  out.Format = format;
  if (texture_desc.DepthOrArraySize > 1) {
    if (texture_desc.SampleDesc.Count > 1) {
      out.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
      out.Texture2DMSArray.FirstArraySlice = array_slice;
      out.Texture2DMSArray.ArraySize = 1;
    } else {
      out.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
      out.Texture2DArray.MipSlice = 0;
      out.Texture2DArray.FirstArraySlice = array_slice;
      out.Texture2DArray.ArraySize = 1;
      out.Texture2DArray.PlaneSlice = 0;
    }
  } else if (texture_desc.SampleDesc.Count > 1) {
    out.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
  } else {
    out.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    out.Texture2D.MipSlice = 0;
    out.Texture2D.PlaneSlice = 0;
  }
  return out;
}

bool submitAndSignal(
    ID3D12CommandQueue* queue,
    ID3D12GraphicsCommandList* list,
    ID3D12Fence* fence,
    UINT64* fence_value) {
  if (FAILED(list->Close())) {
    return false;
  }
  ID3D12CommandList* lists[] = {list};
  queue->ExecuteCommandLists(1, lists);
  const UINT64 next = ++(*fence_value);
  return SUCCEEDED(queue->Signal(fence, next));
}

bool waitForFence(
    ID3D12Fence* fence,
    UINT64 value,
    HANDLE event,
    DWORD timeout_ms) {
  if (fence->GetCompletedValue() >= value) {
    return true;
  }
  if (FAILED(fence->SetEventOnCompletion(value, event))) {
    return false;
  }
  return WaitForSingleObject(event, timeout_ms) == WAIT_OBJECT_0;
}

}  // namespace vrclient::adapters::unreal

#include "openxr/d3d12_device.h"

#include <utility>

namespace vrclient::runtime::openxr {
namespace {

bool sameLuid(const LUID& left, const LUID& right) {
  return left.HighPart == right.HighPart && left.LowPart == right.LowPart;
}

Microsoft::WRL::ComPtr<IDXGIAdapter1> findAdapter(const LUID& required_luid) {
  Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    return {};
  }

  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
  for (UINT index = 0;
       factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND;
       ++index) {
    DXGI_ADAPTER_DESC1 desc{};
    if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
        sameLuid(desc.AdapterLuid, required_luid)) {
      return adapter;
    }
    adapter.Reset();
  }

  return {};
}

}  // namespace

bool createD3D12DeviceForOpenXr(
    XrInstance xr_instance,
    XrSystemId system_id,
    const D3D12BorrowedBinding* borrowed_binding,
    D3D12DeviceContext* out) {
  if (xr_instance == XR_NULL_HANDLE || system_id == XR_NULL_SYSTEM_ID ||
      out == nullptr) {
    return false;
  }

  out->destroy();

  PFN_xrGetD3D12GraphicsRequirementsKHR get_requirements = nullptr;
  XrResult xr_result = xrGetInstanceProcAddr(
      xr_instance,
      "xrGetD3D12GraphicsRequirementsKHR",
      reinterpret_cast<PFN_xrVoidFunction*>(&get_requirements));
  if (XR_FAILED(xr_result) || get_requirements == nullptr) {
    return false;
  }

  XrGraphicsRequirementsD3D12KHR requirements{};
  requirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR;
  xr_result = get_requirements(xr_instance, system_id, &requirements);
  if (XR_FAILED(xr_result)) {
    return false;
  }

  if (borrowed_binding != nullptr) {
    return adoptBorrowedD3D12Binding(
               requirements.adapterLuid,
               requirements.minFeatureLevel,
               *borrowed_binding,
               out) == D3D12BindingAdoptResult::Ready;
  }

  auto adapter = findAdapter(requirements.adapterLuid);
  if (!adapter) {
    return false;
  }

  Microsoft::WRL::ComPtr<ID3D12Device> device;
  if (FAILED(D3D12CreateDevice(
          adapter.Get(),
          requirements.minFeatureLevel,
          IID_PPV_ARGS(&device))) ||
      !device) {
    return false;
  }

  D3D12_COMMAND_QUEUE_DESC queue_desc{};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
  queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

  Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
  if (FAILED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))) ||
      !queue) {
    return false;
  }

  out->adapter = std::move(adapter);
  out->device = std::move(device);
  out->queue = std::move(queue);
  out->borrowed = false;
  return true;
}

}  // namespace vrclient::runtime::openxr

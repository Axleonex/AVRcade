#include "openxr/d3d12_binding.h"

#include <array>

namespace vrclient::runtime::openxr {
namespace {

bool sameLuid(const LUID& left, const LUID& right) {
  return left.HighPart == right.HighPart && left.LowPart == right.LowPart;
}

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

bool supportsFeatureLevel(
    ID3D12Device* device,
    D3D_FEATURE_LEVEL minimum_feature_level) {
  const std::array requested = {minimum_feature_level};
  D3D12_FEATURE_DATA_FEATURE_LEVELS levels{};
  levels.NumFeatureLevels = static_cast<UINT>(requested.size());
  levels.pFeatureLevelsRequested = requested.data();
  return SUCCEEDED(device->CheckFeatureSupport(
             D3D12_FEATURE_FEATURE_LEVELS,
             &levels,
             sizeof(levels))) &&
      levels.MaxSupportedFeatureLevel >= minimum_feature_level;
}

}  // namespace

D3D12BindingAdoptResult adoptBorrowedD3D12Binding(
    const LUID& required_adapter_luid,
    D3D_FEATURE_LEVEL minimum_feature_level,
    const D3D12BorrowedBinding& binding,
    D3D12DeviceContext* out) {
  if (binding.device == nullptr || binding.queue == nullptr || out == nullptr) {
    return D3D12BindingAdoptResult::InvalidArgument;
  }

  const D3D12_COMMAND_QUEUE_DESC queue_desc = binding.queue->GetDesc();
  if (queue_desc.Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
    return D3D12BindingAdoptResult::QueueNotDirect;
  }

  Microsoft::WRL::ComPtr<ID3D12Device> queue_device;
  if (FAILED(binding.queue->GetDevice(IID_PPV_ARGS(&queue_device))) ||
      !sameComIdentity(queue_device.Get(), binding.device)) {
    return D3D12BindingAdoptResult::QueueDeviceMismatch;
  }

  if (!sameLuid(binding.device->GetAdapterLuid(), required_adapter_luid)) {
    return D3D12BindingAdoptResult::AdapterMismatch;
  }
  if (!supportsFeatureLevel(binding.device, minimum_feature_level)) {
    return D3D12BindingAdoptResult::FeatureLevelUnsupported;
  }

  out->destroy();
  out->device = binding.device;
  out->queue = binding.queue;
  out->borrowed = true;
  return D3D12BindingAdoptResult::Ready;
}

}  // namespace vrclient::runtime::openxr

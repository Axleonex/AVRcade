#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>

namespace vrclient::adapters::unreal {

// COM identity comparison via the IUnknown identity rule; false for nulls.
bool sameComIdentity(IUnknown* left, IUnknown* right);

// RTV description addressing exactly one array slice of a 2D texture
// (single-slice view even for array textures, so per-eye array swapchains
// render only the requested slice). The caller must validate array_slice
// against the texture's DepthOrArraySize.
D3D12_RENDER_TARGET_VIEW_DESC makeSliceRtvDesc(
    ID3D12Resource* texture,
    DXGI_FORMAT format,
    std::uint32_t array_slice);

// Closes and submits the list on the queue, then signals fence with
// ++(*fence_value). Does not wait for GPU completion.
bool submitAndSignal(
    ID3D12CommandQueue* queue,
    ID3D12GraphicsCommandList* list,
    ID3D12Fence* fence,
    UINT64* fence_value);

// Bounded CPU wait until fence reaches value. True when already complete.
bool waitForFence(
    ID3D12Fence* fence,
    UINT64 value,
    HANDLE event,
    DWORD timeout_ms);

}  // namespace vrclient::adapters::unreal

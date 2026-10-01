#pragma once

#include "openxr/d3d12_binding.h"

#if defined(XR_USE_GRAPHICS_API_VULKAN)
#include <vulkan/vulkan.h>
#endif

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace vrclient::runtime::openxr {

bool createD3D12DeviceForOpenXr(
    XrInstance xr_instance,
    XrSystemId system_id,
    const D3D12BorrowedBinding* borrowed_binding,
    D3D12DeviceContext* out);

}  // namespace vrclient::runtime::openxr

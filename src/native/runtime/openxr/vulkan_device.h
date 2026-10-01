#pragma once

// XR_USE_PLATFORM_WIN32 makes <openxr/openxr_platform.h> reference Win32 COM types
// (IUnknown, used by the MSFT/holographic Win32 extension declarations), so
// <unknwn.h> must precede it. XR_USE_GRAPHICS_API_VULKAN makes the same header
// reference Vk* types. A dual-backend target also makes it reference D3D12
// types, so both graphics headers must precede openxr_platform.h.
#if defined(XR_USE_PLATFORM_WIN32)
#  include <unknwn.h>
#endif
#if defined(XR_USE_GRAPHICS_API_D3D12)
#  include <d3d12.h>
#endif
#include <vulkan/vulkan.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace vrclient::runtime::openxr {

// The Vulkan equivalent of the former D3D11DeviceContext. Holds the VkInstance,
// the VkPhysicalDevice OpenXR designates, the VkDevice, and the single graphics
// queue + its family index — exactly the handles XrGraphicsBindingVulkanKHR needs.
// All handles are owned by this struct EXCEPT swapchain VkImages, which OpenXR owns.
struct VulkanDeviceContext {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queue_family_index = 0;

  void destroy() {
    if (device != VK_NULL_HANDLE) {
      vkDestroyDevice(device, nullptr);
      device = VK_NULL_HANDLE;
    }
    if (instance != VK_NULL_HANDLE) {
      vkDestroyInstance(instance, nullptr);
      instance = VK_NULL_HANDLE;
    }
    physical_device = VK_NULL_HANDLE;
    queue = VK_NULL_HANDLE;
    queue_family_index = 0;
  }
};

// Creates the Vulkan instance/physical-device/device/queue that the OpenXR runtime
// designates for the given XrInstance + XrSystemId, using the XR_KHR_vulkan_enable2
// call sequence (xrGetVulkanGraphicsRequirements2KHR / xrCreateVulkanInstanceKHR /
// xrGetVulkanGraphicsDevice2KHR / xrCreateVulkanDeviceKHR). On success, fills *out
// and returns true. On any failure *out is left clean (handles destroyed) and the
// function returns false.
bool createVulkanDeviceForOpenXr(
    XrInstance xr_instance,
    XrSystemId system_id,
    VulkanDeviceContext* out);

}  // namespace vrclient::runtime::openxr

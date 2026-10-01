#include "openxr/vulkan_device.h"

#include <vector>

namespace vrclient::runtime::openxr {
namespace {

bool xrOk(XrResult result) {
  return result >= 0;
}

// Picks a graphics-capable queue family on the given physical device; falls back
// to any family with queueCount > 0. Mirrors the vulkan_probe selection.
bool pickGraphicsQueueFamily(VkPhysicalDevice phys, uint32_t* out_family) {
  uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(phys, &count, nullptr);
  if (count == 0) {
    return false;
  }
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(phys, &count, families.data());

  for (uint32_t i = 0; i < count; ++i) {
    if (families[i].queueCount > 0 &&
        (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
      *out_family = i;
      return true;
    }
  }
  for (uint32_t i = 0; i < count; ++i) {
    if (families[i].queueCount > 0) {
      *out_family = i;
      return true;
    }
  }
  return false;
}

}  // namespace

bool createVulkanDeviceForOpenXr(
    XrInstance xr_instance,
    XrSystemId system_id,
    VulkanDeviceContext* out) {
  if (out == nullptr || xr_instance == XR_NULL_HANDLE) {
    return false;
  }
  *out = VulkanDeviceContext{};

  // Resolve the vulkan_enable2 entry points (loader-side; no extra link target).
  PFN_xrGetVulkanGraphicsRequirements2KHR get_requirements = nullptr;
  PFN_xrCreateVulkanInstanceKHR create_vk_instance = nullptr;
  PFN_xrGetVulkanGraphicsDevice2KHR get_vk_device = nullptr;
  PFN_xrCreateVulkanDeviceKHR create_vk_device = nullptr;

  if (!xrOk(xrGetInstanceProcAddr(
          xr_instance, "xrGetVulkanGraphicsRequirements2KHR",
          reinterpret_cast<PFN_xrVoidFunction*>(&get_requirements))) ||
      get_requirements == nullptr) {
    return false;
  }
  if (!xrOk(xrGetInstanceProcAddr(
          xr_instance, "xrCreateVulkanInstanceKHR",
          reinterpret_cast<PFN_xrVoidFunction*>(&create_vk_instance))) ||
      create_vk_instance == nullptr) {
    return false;
  }
  if (!xrOk(xrGetInstanceProcAddr(
          xr_instance, "xrGetVulkanGraphicsDevice2KHR",
          reinterpret_cast<PFN_xrVoidFunction*>(&get_vk_device))) ||
      get_vk_device == nullptr) {
    return false;
  }
  if (!xrOk(xrGetInstanceProcAddr(
          xr_instance, "xrCreateVulkanDeviceKHR",
          reinterpret_cast<PFN_xrVoidFunction*>(&create_vk_device))) ||
      create_vk_device == nullptr) {
    return false;
  }

  // Graphics requirements -> pick an apiVersion inside the supported range.
  XrGraphicsRequirementsVulkanKHR requirements{};
  requirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR;
  if (!xrOk(get_requirements(xr_instance, system_id, &requirements))) {
    return false;
  }
  // enable2 generally wants >= 1.1; clamp to the runtime's [min, max] window.
  // Per XR_KHR_vulkan_enable2 the chosen Vulkan instance apiVersion MUST lie
  // within [minApiVersionSupported, maxApiVersionSupported].
  uint32_t api_version = VK_API_VERSION_1_1;
  const uint32_t min_major =
      static_cast<uint32_t>(XR_VERSION_MAJOR(requirements.minApiVersionSupported));
  const uint32_t min_minor =
      static_cast<uint32_t>(XR_VERSION_MINOR(requirements.minApiVersionSupported));
  const uint32_t min_api = VK_MAKE_API_VERSION(0, min_major, min_minor, 0);
  const uint32_t max_major =
      static_cast<uint32_t>(XR_VERSION_MAJOR(requirements.maxApiVersionSupported));
  const uint32_t max_minor =
      static_cast<uint32_t>(XR_VERSION_MINOR(requirements.maxApiVersionSupported));
  const uint32_t max_api = VK_MAKE_API_VERSION(0, max_major, max_minor, 0);
  if (min_api > api_version) {
    api_version = min_api;
  }
  // Clamp DOWN to the runtime-reported maximum (guarded: only when the runtime
  // reports a sane non-zero max that is below the current choice).
  if (max_api != 0 && max_api < api_version) {
    api_version = max_api;
  }

  // VkInstance via xrCreateVulkanInstanceKHR. OpenXR injects the instance
  // extensions the runtime requires; we add none of our own (compositor path).
  VkApplicationInfo app_info{};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = "VRClientRuntime";
  app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  app_info.pEngineName = "VRClientRuntime";
  app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
  app_info.apiVersion = api_version;

  VkInstanceCreateInfo vk_instance_ci{};
  vk_instance_ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  vk_instance_ci.pApplicationInfo = &app_info;

  XrVulkanInstanceCreateInfoKHR xr_instance_ci{};
  xr_instance_ci.type = XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR;
  xr_instance_ci.systemId = system_id;
  xr_instance_ci.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
  xr_instance_ci.vulkanCreateInfo = &vk_instance_ci;

  VkResult vk_result = VK_SUCCESS;
  if (!xrOk(create_vk_instance(
          xr_instance, &xr_instance_ci, &out->instance, &vk_result)) ||
      vk_result != VK_SUCCESS || out->instance == VK_NULL_HANDLE) {
    out->destroy();
    return false;
  }

  // The VkPhysicalDevice OpenXR mandates for this instance.
  XrVulkanGraphicsDeviceGetInfoKHR device_get_info{};
  device_get_info.type = XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR;
  device_get_info.systemId = system_id;
  device_get_info.vulkanInstance = out->instance;
  if (!xrOk(get_vk_device(
          xr_instance, &device_get_info, &out->physical_device)) ||
      out->physical_device == VK_NULL_HANDLE) {
    out->destroy();
    return false;
  }

  if (!pickGraphicsQueueFamily(out->physical_device, &out->queue_family_index)) {
    out->destroy();
    return false;
  }

  // VkDevice (one graphics queue) via xrCreateVulkanDeviceKHR. OpenXR injects the
  // required device extensions.
  const float priority = 1.0f;
  VkDeviceQueueCreateInfo queue_ci{};
  queue_ci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_ci.queueFamilyIndex = out->queue_family_index;
  queue_ci.queueCount = 1;
  queue_ci.pQueuePriorities = &priority;

  VkDeviceCreateInfo vk_device_ci{};
  vk_device_ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  vk_device_ci.queueCreateInfoCount = 1;
  vk_device_ci.pQueueCreateInfos = &queue_ci;

  XrVulkanDeviceCreateInfoKHR xr_device_ci{};
  xr_device_ci.type = XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR;
  xr_device_ci.systemId = system_id;
  xr_device_ci.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
  xr_device_ci.vulkanPhysicalDevice = out->physical_device;
  xr_device_ci.vulkanCreateInfo = &vk_device_ci;

  vk_result = VK_SUCCESS;
  if (!xrOk(create_vk_device(
          xr_instance, &xr_device_ci, &out->device, &vk_result)) ||
      vk_result != VK_SUCCESS || out->device == VK_NULL_HANDLE) {
    out->destroy();
    return false;
  }

  vkGetDeviceQueue(out->device, out->queue_family_index, 0, &out->queue);
  if (out->queue == VK_NULL_HANDLE) {
    out->destroy();
    return false;
  }

  return true;
}

}  // namespace vrclient::runtime::openxr

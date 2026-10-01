// vulkan_render_validate_main.cpp
//
// Headless validation of the Vulkan TEST-SCENE RENDER PATH (HARD CONSTRAINT #4),
// ADDITIVE + dev-only (OFF by default, guarded by VRCLIENT_BUILD_VULKAN_PROBE).
//
// It exercises the SAME VulkanTestSceneRenderer code the OpenXR runtime uses
// (initialize -> prepareEyeTargets -> renderEye), but over an OFFSCREEN VkImage
// this harness owns — NO XrSession, NO swapchain, NO headset. With
// VK_LAYER_KHRONOS_validation + VK_EXT_debug_utils active, a clean render frame
// must produce ZERO validation-typed messages.
//
// This proves the render path is separable from OpenXR present and is itself
// Vulkan-correct. The OpenXR session/swapchain wiring (present) needs a real
// runtime + headset and is compile/link-checked only — DEFERRED, not faked here.
//
// Exit codes (probe contract):
//   0  success: render frame completed clean AND (in --validate) zero validation msgs
//   2  a Vulkan API call failed / no usable physical device
//   3  --validate requested but VK_LAYER_KHRONOS_validation/debug-utils unavailable
//   4  --validate completed but the validation layer reported >= 1 finding
//   5  usage error
//
// Flags: --validate (require + install validation), --require-device.

#include "frame/test_scene_renderer.h"
#include "frame/stereo_math.h"
#include "public/vr_runtime_api.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

int g_validationCount = 0;

const char* severityName(VkDebugUtilsMessageSeverityFlagBitsEXT s) {
  switch (s) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: return "VERBOSE";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:    return "INFO";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: return "WARNING";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:   return "ERROR";
    default: return "UNKNOWN";
  }
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT types,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*user*/) {
  const bool errorOrWarn =
      (severity & (VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                   VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)) != 0;
  if (!errorOrWarn) return VK_FALSE;
  const bool isValidation =
      (types & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) != 0;
  if (isValidation) {
    ++g_validationCount;
    std::cerr << "[validation:" << severityName(severity) << "] "
              << (data && data->pMessageIdName ? data->pMessageIdName : "(no-id)")
              << ": " << (data && data->pMessage ? data->pMessage : "(no-message)")
              << "\n";
  } else {
    std::cerr << "[loader/general:" << severityName(severity) << "] "
              << (data && data->pMessage ? data->pMessage : "(no-message)")
              << " (not counted)\n";
  }
  return VK_FALSE;
}

bool layerAvailable(const char* wanted) {
  uint32_t count = 0;
  if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS || count == 0)
    return false;
  std::vector<VkLayerProperties> layers(count);
  if (vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS)
    return false;
  for (const auto& l : layers)
    if (std::strcmp(l.layerName, wanted) == 0) return true;
  return false;
}

bool extensionAvailable(const char* wanted) {
  uint32_t count = 0;
  if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS ||
      count == 0)
    return false;
  std::vector<VkExtensionProperties> exts(count);
  if (vkEnumerateInstanceExtensionProperties(nullptr, &count, exts.data()) != VK_SUCCESS)
    return false;
  for (const auto& e : exts)
    if (std::strcmp(e.extensionName, wanted) == 0) return true;
  return false;
}

bool findGraphicsFamily(VkPhysicalDevice phys, uint32_t* out) {
  uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(phys, &count, nullptr);
  if (count == 0) return false;
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(phys, &count, families.data());
  for (uint32_t i = 0; i < count; ++i)
    if (families[i].queueCount > 0 &&
        (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
      *out = i;
      return true;
    }
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  bool wantValidate = false;
  bool requireDevice = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--validate") wantValidate = true;
    else if (a == "--require-device") requireDevice = true;
    else if (a == "--help" || a == "-h") {
      std::cout << "usage: vrclient_vulkan_render_validate [--validate] [--require-device]\n";
      return 0;
    } else {
      std::cerr << "render_validate: unknown argument '" << a << "'\n";
      return 5;
    }
  }

  std::cout << "render_validate: headless render-path validation"
            << (wantValidate ? " [VALIDATION]" : "") << "\n";

  std::vector<const char*> layers;
  if (wantValidate) {
    const char* kValidation = "VK_LAYER_KHRONOS_validation";
    if (!layerAvailable(kValidation)) {
      std::cerr << "render_validate: " << kValidation
                << " unavailable (set VK_LAYER_PATH to external/vulkan/bin)\n";
      return 3;
    }
    layers.push_back(kValidation);
  }
  std::vector<const char*> exts;
  if (wantValidate) {
    if (!extensionAvailable(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
      std::cerr << "render_validate: " << VK_EXT_DEBUG_UTILS_EXTENSION_NAME
                << " unavailable\n";
      return 3;
    }
    exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  VkApplicationInfo app{};
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "vrclient_vulkan_render_validate";
  app.apiVersion = VK_API_VERSION_1_1;

  VkDebugUtilsMessengerCreateInfoEXT dbgCi{};
  dbgCi.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  dbgCi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  dbgCi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  dbgCi.pfnUserCallback = debugCallback;

  VkInstanceCreateInfo ici{};
  ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ici.pApplicationInfo = &app;
  ici.enabledLayerCount = static_cast<uint32_t>(layers.size());
  ici.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
  ici.enabledExtensionCount = static_cast<uint32_t>(exts.size());
  ici.ppEnabledExtensionNames = exts.empty() ? nullptr : exts.data();
  if (wantValidate) ici.pNext = &dbgCi;

  VkInstance instance = VK_NULL_HANDLE;
  if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS) {
    std::cerr << "render_validate: vkCreateInstance failed\n";
    return 2;
  }

  VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
  if (wantValidate) {
    auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (create) create(instance, &dbgCi, nullptr, &messenger);
  }

  int exitCode = 0;

  uint32_t devCount = 0;
  if (vkEnumeratePhysicalDevices(instance, &devCount, nullptr) != VK_SUCCESS) {
    std::cerr << "render_validate: enumerate devices failed\n";
    exitCode = 2;
  } else if (devCount == 0) {
    std::cout << "render_validate: NO physical device (no ICD). Environment note.\n";
    if (requireDevice) exitCode = 2;
  } else {
    std::vector<VkPhysicalDevice> devices(devCount);
    vkEnumeratePhysicalDevices(instance, &devCount, devices.data());
    VkPhysicalDevice phys = devices[0];

    uint32_t family = 0;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    if (!findGraphicsFamily(phys, &family)) {
      std::cerr << "render_validate: no graphics queue family\n";
      exitCode = 2;
    } else {
      const float prio = 1.0f;
      VkDeviceQueueCreateInfo qci{};
      qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
      qci.queueFamilyIndex = family;
      qci.queueCount = 1;
      qci.pQueuePriorities = &prio;
      VkDeviceCreateInfo dci{};
      dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
      dci.queueCreateInfoCount = 1;
      dci.pQueueCreateInfos = &qci;
      if (vkCreateDevice(phys, &dci, nullptr, &device) != VK_SUCCESS) {
        std::cerr << "render_validate: vkCreateDevice failed\n";
        exitCode = 2;
      } else {
        vkGetDeviceQueue(device, family, 0, &queue);

        // ---- Offscreen color target (the stand-in for an OpenXR swapchain image).
        const VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_SRGB;
        const uint32_t kWidth = 256;
        const uint32_t kHeight = 256;

        VkImage colorImage = VK_NULL_HANDLE;
        VkDeviceMemory colorMemory = VK_NULL_HANDLE;
        VkImageCreateInfo imgInfo{};
        imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType = VK_IMAGE_TYPE_2D;
        imgInfo.format = kColorFormat;
        imgInfo.extent = {kWidth, kHeight, 1};
        imgInfo.mipLevels = 1;
        imgInfo.arrayLayers = 1;
        imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool ok = (vkCreateImage(device, &imgInfo, nullptr, &colorImage) == VK_SUCCESS);

        if (ok) {
          VkMemoryRequirements req{};
          vkGetImageMemoryRequirements(device, colorImage, &req);
          VkPhysicalDeviceMemoryProperties memProps{};
          vkGetPhysicalDeviceMemoryProperties(phys, &memProps);
          uint32_t memType = UINT32_MAX;
          for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((req.memoryTypeBits & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags &
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
              memType = i;
              break;
            }
          }
          ok = (memType != UINT32_MAX);
          if (ok) {
            VkMemoryAllocateInfo alloc{};
            alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc.allocationSize = req.size;
            alloc.memoryTypeIndex = memType;
            ok = (vkAllocateMemory(device, &alloc, nullptr, &colorMemory) == VK_SUCCESS) &&
                 (vkBindImageMemory(device, colorImage, colorMemory, 0) == VK_SUCCESS);
          }
        }

        // ---- Drive the SAME renderer the OpenXR runtime uses.
        vrclient::runtime::frame::VulkanTestSceneRenderer renderer;
        if (ok) {
          ok = renderer.initialize(instance, phys, device, family, queue);
          if (!ok) std::cerr << "render_validate: renderer.initialize failed\n";
        }
        // Exercise the lightweight in-headset debug overlay (DIAG-03 partial) so
        // the validation pass covers its pipeline (depth-test OFF, alpha blend,
        // push-constant). setOverlayState(true) drives the error-RED branch.
        renderer.setOverlayEnabled(true);
        renderer.setOverlayState(true);
        if (ok) {
          const VkImage images[] = {colorImage};
          ok = renderer.prepareEyeTargets(
              VR_RUNTIME_EYE_LEFT, images, 1, kColorFormat, kWidth, kHeight);
          if (!ok) std::cerr << "render_validate: prepareEyeTargets failed\n";
        }
        if (ok) {
          // Regression guard (descriptor-pool re-use): re-prepare the SAME eye a
          // SECOND time, simulating a dynamic-resolution / swapchain re-create.
          // This drives destroyEye() -> vkFreeDescriptorSets() -> a fresh
          // vkAllocateDescriptorSets(). Before descriptor_pool_ gained
          // VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT (+ the free in
          // destroyEye), this second allocation exhausted the 2-set pool and
          // returned VK_ERROR_OUT_OF_POOL_MEMORY, so prepareEyeTargets()==false.
          const VkImage images[] = {colorImage};
          ok = renderer.prepareEyeTargets(
              VR_RUNTIME_EYE_LEFT, images, 1, kColorFormat, kWidth, kHeight);
          if (!ok)
            std::cerr << "render_validate: re-prepareEyeTargets (descriptor-pool "
                         "reuse) FAILED - pool likely exhausted\n";
          else
            std::cout << "render_validate: re-prepared LEFT eye "
                         "(descriptor-set free+realloc OK)\n";
        }
        if (ok) {
          VrRuntimeFrameData frame{};
          frame.eye_count = 2;
          // Drive a live runtime_state so the overlay status-bar color switch
          // exercises a non-default (RUNNING -> green) branch, not just STOPPED.
          frame.runtime_state = VR_RUNTIME_STATE_RUNNING;
          frame.eyes[0].eye = VR_RUNTIME_EYE_LEFT;
          frame.eyes[0].projection = vrclient::runtime::frame::identityMatrix();
          frame.eyes[0].view = vrclient::runtime::frame::identityMatrix();
          // Two frames over the single image so the per-image fence reuse path
          // (wait -> reset -> re-record -> submit) is also validated.
          renderer.renderEye(frame, VR_RUNTIME_EYE_LEFT, 0, colorImage);
          renderer.renderEye(frame, VR_RUNTIME_EYE_LEFT, 0, colorImage);
          renderer.waitIdle();
          std::cout << "render_validate: rendered 2 frames to offscreen target\n";
        }
        if (!ok && exitCode == 0) exitCode = 2;

        renderer.destroy();
        if (colorImage != VK_NULL_HANDLE) vkDestroyImage(device, colorImage, nullptr);
        if (colorMemory != VK_NULL_HANDLE) vkFreeMemory(device, colorMemory, nullptr);
        vkDestroyDevice(device, nullptr);
      }
    }
  }

  if (messenger != VK_NULL_HANDLE) {
    auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (destroy) destroy(instance, messenger, nullptr);
  }
  vkDestroyInstance(instance, nullptr);

  if (wantValidate) {
    std::cout << "render_validate: validation finding count = " << g_validationCount << "\n";
    if (exitCode == 0 && g_validationCount > 0) {
      std::cerr << "render_validate: FAIL - " << g_validationCount
                << " validation finding(s) during a clean render\n";
      return 4;
    }
  }
  if (exitCode == 0) std::cout << "render_validate: SUCCESS\n";
  return exitCode;
}

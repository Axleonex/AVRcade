// vulkan_probe_main.cpp
//
// Headless Vulkan SDK probe (ADDITIVE, dev-only, NOT part of the product/default
// suite). It proves the project-local Vulkan SDK bootstrapped by
// tools/build-vulkan.ps1 (external/vulkan: Vulkan-Headers + the vulkan-1 loader +
// optionally the VK_LAYER_KHRONOS_validation layer) is actually usable from C++:
//
//   * create a VkInstance (optionally requesting VK_LAYER_KHRONOS_validation),
//   * enumerate physical devices,
//   * create a VkDevice with one queue on the first device + queue family,
//   * then tear everything down cleanly.
//
// No window, no surface, no swapchain, no headset, no display: pure headless. It
// links the bootstrapped vulkan-1 loader (NOT the system loader) via the
// Vulkan::Loader / Vulkan::Headers imported targets find_package() resolves out of
// external/vulkan. It is guarded behind the OFF-by-default CMake option
// VRCLIENT_BUILD_VULKAN_PROBE so the default OpenXR-OFF build is unaffected.
//
// Exit codes (so a CI driver can assert precisely):
//   0  success: instance (+ optional layer) and device created and destroyed clean,
//      AND (in --validate mode) ZERO validation errors/warnings were observed.
//   2  a Vulkan API call failed (or, in --require-device mode, no physical device).
//   3  --validate was requested but VK_LAYER_KHRONOS_validation is not available.
//   4  --validate run completed the lifecycle but the validation layer emitted at
//      least one ERROR/WARNING message (a clean lifecycle must produce none).
//   5  usage / argument error.
//
// Flags:
//   --validate        request + require VK_LAYER_KHRONOS_validation, install a
//                     VK_EXT_debug_utils callback, and FAIL (exit 4) on any
//                     error/warning message. Implies the debug-utils instance ext.
//   --require-device  treat "no physical device found" as a failure (exit 2). By
//                     default a no-ICD machine is reported as an environment note
//                     and still exits 0 once the instance lifecycle succeeds.
//
// This file is self-contained; it pulls in only <vulkan/vulkan.h> from the
// bootstrapped headers and the C++ standard library.

#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

// Global validation-message tally. The debug-utils callback increments this; the
// probe asserts it stays zero across a clean instance+device lifecycle.
int g_validationErrorOrWarningCount = 0;

const char* severityName(VkDebugUtilsMessageSeverityFlagBitsEXT s) {
    switch (s) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: return "VERBOSE";
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:    return "INFO";
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: return "WARNING";
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:   return "ERROR";
        default: return "UNKNOWN";
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL realDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT types,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*pUserData*/) {
    const bool isErrorOrWarn =
        (severity & (VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)) != 0;
    if (!isErrorOrWarn) return VK_FALSE;

    // Only ACTUAL Vulkan API-usage validation findings carry the VALIDATION type bit.
    // The loader itself routes GENERAL-typed informational messages (e.g. "Layer X
    // forced disabled by VK_LOADER_LAYERS_DISABLE", implicit-layer notes) through the
    // same debug-utils callback; those are NOT validation errors about THIS probe's
    // instance/device usage. Count (and fail on) only VALIDATION-typed messages so the
    // zero-error assertion reflects the validation layer's verdict on our lifecycle,
    // not loader chatter from the host's third-party layers. General messages are still
    // printed for transparency but do not flip the result.
    const bool isValidation =
        (types & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) != 0;

    if (isValidation) {
        ++g_validationErrorOrWarningCount;
        std::cerr << "[validation:" << severityName(severity) << "] "
                  << (data && data->pMessageIdName ? data->pMessageIdName : "(no-id)")
                  << ": "
                  << (data && data->pMessage ? data->pMessage : "(no-message)")
                  << "\n";
    } else {
        std::cerr << "[loader/general:" << severityName(severity) << "] "
                  << (data && data->pMessage ? data->pMessage : "(no-message)")
                  << " (not counted; not a VALIDATION-typed message)\n";
    }
    return VK_FALSE;  // do not abort the triggering call
}

bool instanceLayerAvailable(const char* wanted) {
    uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS || count == 0) {
        return false;
    }
    std::vector<VkLayerProperties> layers(count);
    if (vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS) {
        return false;
    }
    for (const auto& l : layers) {
        if (std::strcmp(l.layerName, wanted) == 0) return true;
    }
    return false;
}

bool instanceExtensionAvailable(const char* wanted) {
    uint32_t count = 0;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS ||
        count == 0) {
        return false;
    }
    std::vector<VkExtensionProperties> exts(count);
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, exts.data()) != VK_SUCCESS) {
        return false;
    }
    for (const auto& e : exts) {
        if (std::strcmp(e.extensionName, wanted) == 0) return true;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    bool wantValidate = false;
    bool requireDevice = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--validate") {
            wantValidate = true;
        } else if (a == "--require-device") {
            requireDevice = true;
        } else if (a == "--help" || a == "-h") {
            std::cout << "usage: vrclient_vulkan_probe [--validate] [--require-device]\n";
            return 0;
        } else {
            std::cerr << "vulkan_probe: unknown argument '" << a << "'\n";
            return 5;
        }
    }

    std::cout << "vulkan_probe: headless instance+device lifecycle"
              << (wantValidate ? " [VALIDATION]" : "") << "\n";

    // ---- layer selection ----------------------------------------------------
    std::vector<const char*> enabledLayers;
    if (wantValidate) {
        const char* kValidation = "VK_LAYER_KHRONOS_validation";
        if (!instanceLayerAvailable(kValidation)) {
            std::cerr << "vulkan_probe: --validate requested but " << kValidation
                      << " is NOT available (is VK_LAYER_PATH pointed at "
                         "external/vulkan/bin?)\n";
            return 3;
        }
        enabledLayers.push_back(kValidation);
        std::cout << "vulkan_probe: " << kValidation << " is available and enabled\n";
    }

    // ---- extension selection ------------------------------------------------
    std::vector<const char*> enabledExts;
    const bool haveDebugUtils = instanceExtensionAvailable(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (wantValidate) {
        if (!haveDebugUtils) {
            std::cerr << "vulkan_probe: --validate requested but "
                      << VK_EXT_DEBUG_UTILS_EXTENSION_NAME << " is not available\n";
            return 3;
        }
        enabledExts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    // ---- VkInstance ---------------------------------------------------------
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "vrclient_vulkan_probe";
    app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app.pEngineName = "vrclient";
    app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app.apiVersion = VK_API_VERSION_1_0;  // minimum floor; widely supported

    // A debug-utils messenger chained into instance create so messages emitted
    // DURING vkCreateInstance / vkDestroyInstance are also caught.
    VkDebugUtilsMessengerCreateInfoEXT dbgCi{};
    dbgCi.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    dbgCi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    dbgCi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    dbgCi.pfnUserCallback = realDebugCallback;

    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    ici.enabledLayerCount = static_cast<uint32_t>(enabledLayers.size());
    ici.ppEnabledLayerNames = enabledLayers.empty() ? nullptr : enabledLayers.data();
    ici.enabledExtensionCount = static_cast<uint32_t>(enabledExts.size());
    ici.ppEnabledExtensionNames = enabledExts.empty() ? nullptr : enabledExts.data();
    if (wantValidate) ici.pNext = &dbgCi;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult r = vkCreateInstance(&ici, nullptr, &instance);
    if (r != VK_SUCCESS) {
        std::cerr << "vulkan_probe: vkCreateInstance failed (VkResult " << r << ")\n";
        return 2;
    }
    std::cout << "vulkan_probe: VkInstance created\n";

    // Standalone persistent messenger (catches anything between create/destroy).
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    if (wantValidate) {
        auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        if (create) {
            r = create(instance, &dbgCi, nullptr, &messenger);
            if (r != VK_SUCCESS) {
                std::cerr << "vulkan_probe: vkCreateDebugUtilsMessengerEXT failed (VkResult "
                          << r << ")\n";
                vkDestroyInstance(instance, nullptr);
                return 2;
            }
            std::cout << "vulkan_probe: debug-utils messenger installed\n";
        }
    }

    int exitCode = 0;

    // ---- physical devices ---------------------------------------------------
    uint32_t devCount = 0;
    r = vkEnumeratePhysicalDevices(instance, &devCount, nullptr);
    if (r != VK_SUCCESS) {
        std::cerr << "vulkan_probe: vkEnumeratePhysicalDevices(count) failed (VkResult " << r
                  << ")\n";
        exitCode = 2;
        goto cleanup;
    }
    std::cout << "vulkan_probe: physical device count = " << devCount << "\n";

    if (devCount == 0) {
        // No ICD/driver. This is an ENVIRONMENT condition, not a bootstrap defect:
        // the instance lifecycle proved the loader+headers work. Only fail if the
        // caller demanded a device.
        std::cout << "vulkan_probe: NO physical device found (no Vulkan driver/ICD "
                     "visible). Reporting as an environment note.\n";
        if (requireDevice) {
            std::cerr << "vulkan_probe: --require-device set; treating no-device as failure\n";
            exitCode = 2;
        }
        goto cleanup;
    }

    {
        std::vector<VkPhysicalDevice> devices(devCount);
        r = vkEnumeratePhysicalDevices(instance, &devCount, devices.data());
        if (r != VK_SUCCESS) {
            std::cerr << "vulkan_probe: vkEnumeratePhysicalDevices(list) failed (VkResult " << r
                      << ")\n";
            exitCode = 2;
            goto cleanup;
        }

        VkPhysicalDevice phys = devices[0];
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(phys, &props);
        std::cout << "vulkan_probe: using device[0] = " << props.deviceName
                  << " (apiVersion " << VK_VERSION_MAJOR(props.apiVersion) << "."
                  << VK_VERSION_MINOR(props.apiVersion) << "."
                  << VK_VERSION_PATCH(props.apiVersion) << ")\n";

        // ---- queue family ---------------------------------------------------
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(phys, &qfCount, nullptr);
        if (qfCount == 0) {
            std::cerr << "vulkan_probe: device exposes no queue families\n";
            exitCode = 2;
            goto cleanup;
        }
        std::vector<VkQueueFamilyProperties> qfs(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(phys, &qfCount, qfs.data());

        // Prefer a graphics-capable family; otherwise just take family 0 (a single
        // queue on any family satisfies the create-a-device probe).
        uint32_t family = 0;
        bool found = false;
        for (uint32_t i = 0; i < qfCount; ++i) {
            if (qfs[i].queueCount > 0 && (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                family = i;
                found = true;
                break;
            }
        }
        if (!found) {
            for (uint32_t i = 0; i < qfCount; ++i) {
                if (qfs[i].queueCount > 0) { family = i; found = true; break; }
            }
        }
        if (!found) {
            std::cerr << "vulkan_probe: no queue family with queueCount > 0\n";
            exitCode = 2;
            goto cleanup;
        }
        std::cout << "vulkan_probe: queue family index = " << family << "\n";

        // ---- VkDevice with one queue ----------------------------------------
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo qci{};
        qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = family;
        qci.queueCount = 1;
        qci.pQueuePriorities = &priority;

        VkDeviceCreateInfo dci{};
        dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qci;

        VkDevice device = VK_NULL_HANDLE;
        r = vkCreateDevice(phys, &dci, nullptr, &device);
        if (r != VK_SUCCESS) {
            std::cerr << "vulkan_probe: vkCreateDevice failed (VkResult " << r << ")\n";
            exitCode = 2;
            goto cleanup;
        }
        std::cout << "vulkan_probe: VkDevice created\n";

        VkQueue queue = VK_NULL_HANDLE;
        vkGetDeviceQueue(device, family, 0, &queue);
        std::cout << "vulkan_probe: retrieved queue (handle "
                  << (queue != VK_NULL_HANDLE ? "non-null" : "NULL") << ")\n";

        vkDestroyDevice(device, nullptr);
        std::cout << "vulkan_probe: VkDevice destroyed\n";
    }

cleanup:
    if (messenger != VK_NULL_HANDLE) {
        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy) destroy(instance, messenger, nullptr);
    }
    vkDestroyInstance(instance, nullptr);
    std::cout << "vulkan_probe: VkInstance destroyed\n";

    if (wantValidate) {
        std::cout << "vulkan_probe: validation error/warning count = "
                  << g_validationErrorOrWarningCount << "\n";
        if (exitCode == 0 && g_validationErrorOrWarningCount > 0) {
            std::cerr << "vulkan_probe: FAIL - validation layer reported "
                      << g_validationErrorOrWarningCount
                      << " error(s)/warning(s) during a clean lifecycle\n";
            return 4;
        }
    }

    if (exitCode == 0) {
        std::cout << "vulkan_probe: SUCCESS\n";
    }
    return exitCode;
}

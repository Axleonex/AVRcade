#include <windows.h>

#include <openxr/openxr.h>

#include <cstdio>
#include <string>

namespace {

using GetInstanceProcAddrFn = PFN_xrGetInstanceProcAddr;

template <typename Function>
bool LoadFunction(GetInstanceProcAddrFn getInstanceProcAddr, XrInstance instance,
                  const char* name, Function& function) {
  PFN_xrVoidFunction raw = nullptr;
  const XrResult result = getInstanceProcAddr(instance, name, &raw);
  if (result != XR_SUCCESS || raw == nullptr) {
    std::fprintf(stderr, "%s lookup failed result=%d\n", name, static_cast<int>(result));
    return false;
  }
  function = reinterpret_cast<Function>(raw);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: openxr_loader_probe <loader.dll> <runtime.json>\n");
    return 2;
  }

  const std::string runtimeManifest(argv[2]);
  const size_t slash = runtimeManifest.find_last_of("\\/");
  if (slash != std::string::npos)
    SetCurrentDirectoryA(runtimeManifest.substr(0, slash).c_str());
  if (!SetEnvironmentVariableA("XR_RUNTIME_JSON", argv[2])) {
    std::fprintf(stderr, "SetEnvironmentVariableA failed=%lu\n", GetLastError());
    return 3;
  }

  HMODULE loader = LoadLibraryA(argv[1]);
  if (loader == nullptr) {
    std::fprintf(stderr, "LoadLibraryA failed=%lu\n", GetLastError());
    return 4;
  }
  auto getInstanceProcAddr = reinterpret_cast<GetInstanceProcAddrFn>(
      GetProcAddress(loader, "xrGetInstanceProcAddr"));
  if (getInstanceProcAddr == nullptr) {
    std::fprintf(stderr, "xrGetInstanceProcAddr export missing\n");
    return 5;
  }

  PFN_xrEnumerateInstanceExtensionProperties enumerateExtensions = nullptr;
  if (!LoadFunction(getInstanceProcAddr, XR_NULL_HANDLE,
                    "xrEnumerateInstanceExtensionProperties",
                    enumerateExtensions))
    return 6;
  uint32_t extensionCount = 0;
  XrResult result = enumerateExtensions(nullptr, 0, &extensionCount, nullptr);
  std::printf("extension_enumeration result=%d count=%u\n", static_cast<int>(result),
              extensionCount);

  PFN_xrCreateInstance createInstance = nullptr;
  if (!LoadFunction(getInstanceProcAddr, XR_NULL_HANDLE, "xrCreateInstance", createInstance))
    return 7;
  XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
  createInfo.applicationInfo.applicationName[0] = 'V';
  createInfo.applicationInfo.applicationName[1] = 'R';
  createInfo.applicationInfo.applicationVersion = 1;
  createInfo.applicationInfo.engineName[0] = 'V';
  createInfo.applicationInfo.engineName[1] = 'R';
  createInfo.applicationInfo.engineVersion = 1;
  createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
  XrInstance instance = XR_NULL_HANDLE;
  result = createInstance(&createInfo, &instance);
  std::printf("create_instance result=%d handle=%llu\n", static_cast<int>(result),
              static_cast<unsigned long long>(instance));
  if (result != XR_SUCCESS) return 8;

  PFN_xrDestroyInstance destroyInstance = nullptr;
  if (!LoadFunction(getInstanceProcAddr, instance, "xrDestroyInstance", destroyInstance))
    return 9;
  result = destroyInstance(instance);
  std::printf("destroy_instance result=%d\n", static_cast<int>(result));
  return result == XR_SUCCESS ? 0 : 10;
}

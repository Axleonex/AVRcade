#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include <openxr/openxr_platform.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

struct XrInstance_T {
  XrSession session = XR_NULL_HANDLE;
  bool readyEventDelivered = false;
};

struct XrSession_T {
  XrInstance instance = XR_NULL_HANDLE;
  ID3D11Device* device = nullptr;
  bool running = false;
};

struct XrSpace_T {};

struct XrSwapchain_T {
  XrSession session = XR_NULL_HANDLE;
  std::vector<ID3D11Texture2D*> images;
  uint32_t nextImage = 0;
};

struct XrActionSet_T {};
struct XrAction_T {};

namespace {

HMODULE g_module = nullptr;
std::atomic<uint64_t> g_frameCount{0};
std::atomic<XrTime> g_displayTime{0};

void RuntimeLog(const char* format, ...) {
  char message[768]{};
  va_list args;
  va_start(args, format);
  _vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
  va_end(args);

  char modulePath[MAX_PATH]{};
  const DWORD length = GetModuleFileNameA(g_module, modulePath, sizeof(modulePath));
  if (length == 0 || length >= sizeof(modulePath)) return;
  std::string logPath(modulePath, length);
  const size_t slash = logPath.find_last_of("\\/");
  logPath = (slash == std::string::npos ? std::string{} : logPath.substr(0, slash + 1)) +
      "fake-openxr-runtime.log";

  HANDLE file = CreateFileA(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  std::string line = "[VRClient Fake OpenXR] ";
  line += message;
  line += "\r\n";
  DWORD written = 0;
  WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
  CloseHandle(file);
}

XrResult ValidatePointer(const void* pointer) {
  return pointer == nullptr ? XR_ERROR_VALIDATION_FAILURE : XR_SUCCESS;
}

void CopyName(char* destination, size_t capacity, const char* source) {
  if (capacity == 0) return;
  std::strncpy(destination, source, capacity - 1);
  destination[capacity - 1] = '\0';
}

template <typename Handle, typename Object>
Handle MakeHandle(Object* object) {
  return static_cast<Handle>(reinterpret_cast<uintptr_t>(object));
}

template <typename Object, typename Handle>
Object* FromHandle(Handle handle) {
  return reinterpret_cast<Object*>(static_cast<uintptr_t>(handle));
}

XrInstance_T* InstanceState(XrInstance handle) {
  return FromHandle<XrInstance_T>(handle);
}

XrSession_T* SessionState(XrSession handle) {
  return FromHandle<XrSession_T>(handle);
}

XrSpace_T* SpaceState(XrSpace handle) {
  return FromHandle<XrSpace_T>(handle);
}

XrSwapchain_T* SwapchainState(XrSwapchain handle) {
  return FromHandle<XrSwapchain_T>(handle);
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_module = module;
    DisableThreadLibraryCalls(module);
  }
  return TRUE;
}

#define FAKE_XR_EXPORT extern "C" __declspec(dllexport) XRAPI_ATTR

FAKE_XR_EXPORT XrResult XRAPI_CALL xrEnumerateInstanceExtensionProperties(
    const char* layerName, uint32_t capacityInput, uint32_t* countOutput,
    XrExtensionProperties* properties) {
  RuntimeLog("enumerate_instance_extension_properties");
  if (layerName != nullptr || countOutput == nullptr) return XR_ERROR_VALIDATION_FAILURE;
  *countOutput = 1;
  if (capacityInput == 0) return XR_SUCCESS;
  if (properties == nullptr) return XR_ERROR_VALIDATION_FAILURE;
  properties[0].type = XR_TYPE_EXTENSION_PROPERTIES;
  properties[0].next = nullptr;
  CopyName(properties[0].extensionName, XR_MAX_EXTENSION_NAME_SIZE,
           XR_KHR_D3D11_ENABLE_EXTENSION_NAME);
  properties[0].extensionVersion = 1;
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrCreateInstance(
    const XrInstanceCreateInfo* createInfo, XrInstance* instance) {
  if (ValidatePointer(createInfo) != XR_SUCCESS || instance == nullptr)
    return XR_ERROR_VALIDATION_FAILURE;
  auto* created = new XrInstance_T();
  *instance = MakeHandle<XrInstance>(created);
  RuntimeLog("create_instance api=%u.%u", XR_VERSION_MAJOR(createInfo->applicationInfo.apiVersion),
             XR_VERSION_MINOR(createInfo->applicationInfo.apiVersion));
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrDestroyInstance(XrInstance instance) {
  if (instance == XR_NULL_HANDLE) return XR_ERROR_HANDLE_INVALID;
  delete InstanceState(instance);
  RuntimeLog("destroy_instance");
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrGetSystem(
    XrInstance instance, const XrSystemGetInfo* getInfo, XrSystemId* systemId) {
  if (instance == XR_NULL_HANDLE || ValidatePointer(getInfo) != XR_SUCCESS || systemId == nullptr)
    return XR_ERROR_VALIDATION_FAILURE;
  if (getInfo->formFactor != XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY)
    return XR_ERROR_FORM_FACTOR_UNSUPPORTED;
  *systemId = 1;
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrPollEvent(XrInstance instance, XrEventDataBuffer* eventData) {
  if (instance == XR_NULL_HANDLE || eventData == nullptr) return XR_ERROR_VALIDATION_FAILURE;
  auto* instanceState = InstanceState(instance);
  if (!instanceState->readyEventDelivered && instanceState->session != XR_NULL_HANDLE) {
    auto* changed = reinterpret_cast<XrEventDataSessionStateChanged*>(eventData);
    changed->type = XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED;
    changed->next = nullptr;
    changed->session = instanceState->session;
    changed->state = XR_SESSION_STATE_READY;
    changed->time = g_displayTime.load();
    instanceState->readyEventDelivered = true;
    return XR_SUCCESS;
  }
  return XR_EVENT_UNAVAILABLE;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrCreateSession(
    XrInstance instance, const XrSessionCreateInfo* createInfo, XrSession* session) {
  if (instance == XR_NULL_HANDLE || createInfo == nullptr || session == nullptr)
    return XR_ERROR_VALIDATION_FAILURE;

  ID3D11Device* device = nullptr;
  for (auto* next = reinterpret_cast<const XrBaseInStructure*>(createInfo->next);
       next != nullptr; next = next->next) {
    if (next->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
      const auto* binding = reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(next);
      device = binding->device;
      break;
    }
  }
  if (device == nullptr) return XR_ERROR_GRAPHICS_DEVICE_INVALID;

  auto* created = new XrSession_T();
  created->instance = instance;
  created->device = device;
  created->device->AddRef();
  const XrSession sessionHandle = MakeHandle<XrSession>(created);
  InstanceState(instance)->session = sessionHandle;
  *session = sessionHandle;
  RuntimeLog("create_session");
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrDestroySession(XrSession session) {
  if (session == XR_NULL_HANDLE) return XR_ERROR_HANDLE_INVALID;
  auto* sessionState = SessionState(session);
  if (sessionState->device != nullptr) sessionState->device->Release();
  if (sessionState->instance != XR_NULL_HANDLE &&
      InstanceState(sessionState->instance)->session == session)
    InstanceState(sessionState->instance)->session = XR_NULL_HANDLE;
  delete sessionState;
  RuntimeLog("destroy_session");
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrBeginSession(
    XrSession session, const XrSessionBeginInfo* beginInfo) {
  if (session == XR_NULL_HANDLE || beginInfo == nullptr) return XR_ERROR_VALIDATION_FAILURE;
  SessionState(session)->running = true;
  RuntimeLog("begin_session view_configuration=%d", beginInfo->primaryViewConfigurationType);
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrEndSession(XrSession session) {
  if (session == XR_NULL_HANDLE) return XR_ERROR_HANDLE_INVALID;
  SessionState(session)->running = false;
  RuntimeLog("end_session");
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrCreateReferenceSpace(
    XrSession session, const XrReferenceSpaceCreateInfo* createInfo, XrSpace* space) {
  if (session == XR_NULL_HANDLE || createInfo == nullptr || space == nullptr)
    return XR_ERROR_VALIDATION_FAILURE;
  *space = MakeHandle<XrSpace>(new XrSpace_T());
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrCreateActionSpace(
    XrSession session, const XrActionSpaceCreateInfo* createInfo, XrSpace* space) {
  if (session == XR_NULL_HANDLE || createInfo == nullptr ||
      createInfo->action == XR_NULL_HANDLE || space == nullptr)
    return XR_ERROR_VALIDATION_FAILURE;
  *space = MakeHandle<XrSpace>(new XrSpace_T());
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrLocateSpace(
    XrSpace space, XrSpace baseSpace, XrTime, XrSpaceLocation* location) {
  if (space == XR_NULL_HANDLE || baseSpace == XR_NULL_HANDLE || location == nullptr)
    return XR_ERROR_VALIDATION_FAILURE;
  location->locationFlags = XR_SPACE_LOCATION_POSITION_VALID_BIT |
      XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
  location->pose.orientation = {0.0f,0.0f,0.0f,1.0f};
  location->pose.position = {0.0f,0.0f,0.0f};
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrDestroySpace(XrSpace space) {
  if (space == XR_NULL_HANDLE) return XR_ERROR_HANDLE_INVALID;
  delete SpaceState(space);
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrEnumerateViewConfigurationViews(
    XrInstance instance, XrSystemId systemId, XrViewConfigurationType viewConfigurationType,
    uint32_t capacityInput, uint32_t* countOutput, XrViewConfigurationView* views) {
  if (instance == XR_NULL_HANDLE || systemId != 1 ||
      viewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO || countOutput == nullptr)
    return XR_ERROR_VALIDATION_FAILURE;
  *countOutput = 2;
  if (capacityInput == 0) return XR_SUCCESS;
  if (capacityInput < 2 || views == nullptr) return XR_ERROR_SIZE_INSUFFICIENT;
  for (uint32_t eye = 0; eye < 2; ++eye) {
    views[eye].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    views[eye].next = nullptr;
    views[eye].recommendedImageRectWidth = 1024;
    views[eye].maxImageRectWidth = 2048;
    views[eye].recommendedImageRectHeight = 1024;
    views[eye].maxImageRectHeight = 2048;
    views[eye].recommendedSwapchainSampleCount = 1;
    views[eye].maxSwapchainSampleCount = 1;
  }
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrGetD3D11GraphicsRequirementsKHR(
    XrInstance instance, XrSystemId systemId, XrGraphicsRequirementsD3D11KHR* requirements) {
  if (instance == XR_NULL_HANDLE || systemId != 1 || requirements == nullptr)
    return XR_ERROR_VALIDATION_FAILURE;
  IDXGIFactory1* factory = nullptr;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return XR_ERROR_RUNTIME_FAILURE;
  IDXGIAdapter* adapter = nullptr;
  const HRESULT enumResult = factory->EnumAdapters(0, &adapter);
  if (FAILED(enumResult) || adapter == nullptr) {
    factory->Release();
    return XR_ERROR_RUNTIME_FAILURE;
  }
  DXGI_ADAPTER_DESC description{};
  const HRESULT descResult = adapter->GetDesc(&description);
  adapter->Release();
  factory->Release();
  if (FAILED(descResult)) return XR_ERROR_RUNTIME_FAILURE;
  requirements->adapterLuid = description.AdapterLuid;
  requirements->minFeatureLevel = D3D_FEATURE_LEVEL_11_0;
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrEnumerateSwapchainFormats(
    XrSession session, uint32_t capacityInput, uint32_t* countOutput, int64_t* formats) {
  if (session == XR_NULL_HANDLE || countOutput == nullptr) return XR_ERROR_VALIDATION_FAILURE;
  *countOutput = 1;
  if (capacityInput == 0) return XR_SUCCESS;
  if (capacityInput < 1 || formats == nullptr) return XR_ERROR_SIZE_INSUFFICIENT;
  formats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrCreateSwapchain(
    XrSession session, const XrSwapchainCreateInfo* createInfo, XrSwapchain* swapchain) {
  if (session == XR_NULL_HANDLE || createInfo == nullptr || swapchain == nullptr)
    return XR_ERROR_VALIDATION_FAILURE;
  if (createInfo->arraySize != 2 || createInfo->sampleCount != 1)
    return XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;

  auto* sessionState = SessionState(session);
  auto* created = new XrSwapchain_T();
  created->session = session;
  D3D11_TEXTURE2D_DESC description{};
  description.Width = createInfo->width;
  description.Height = createInfo->height;
  description.MipLevels = createInfo->mipCount;
  description.ArraySize = createInfo->arraySize;
  // Virtual Desktop exposes its negotiated R8G8B8A8_UNORM swapchain through
  // typeless D3D11 backing textures. Applications must create a typed UNORM
  // render-target view instead of copying this resource format into the view.
  description.Format = createInfo->format == DXGI_FORMAT_R8G8B8A8_UNORM
      ? DXGI_FORMAT_R8G8B8A8_TYPELESS
      : static_cast<DXGI_FORMAT>(createInfo->format);
  description.SampleDesc.Count = createInfo->sampleCount;
  description.Usage = D3D11_USAGE_DEFAULT;
  description.BindFlags = D3D11_BIND_RENDER_TARGET;
  for (uint32_t index = 0; index < 3; ++index) {
    ID3D11Texture2D* image = nullptr;
    if (FAILED(sessionState->device->CreateTexture2D(&description, nullptr, &image))) {
      for (auto* previous : created->images) previous->Release();
      delete created;
      return XR_ERROR_RUNTIME_FAILURE;
    }
    created->images.push_back(image);
  }
  *swapchain = MakeHandle<XrSwapchain>(created);
  RuntimeLog("create_swapchain %ux%u array=%u requested_format=%lld resource_format=%u",
             createInfo->width, createInfo->height, createInfo->arraySize,
             static_cast<long long>(createInfo->format),
             static_cast<unsigned>(description.Format));
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrDestroySwapchain(XrSwapchain swapchain) {
  if (swapchain == XR_NULL_HANDLE) return XR_ERROR_HANDLE_INVALID;
  auto* swapchainState = SwapchainState(swapchain);
  for (auto* image : swapchainState->images) image->Release();
  delete swapchainState;
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrEnumerateSwapchainImages(
    XrSwapchain swapchain, uint32_t capacityInput, uint32_t* countOutput,
    XrSwapchainImageBaseHeader* images) {
  if (swapchain == XR_NULL_HANDLE || countOutput == nullptr) return XR_ERROR_VALIDATION_FAILURE;
  auto* swapchainState = SwapchainState(swapchain);
  *countOutput = static_cast<uint32_t>(swapchainState->images.size());
  if (capacityInput == 0) return XR_SUCCESS;
  if (capacityInput < swapchainState->images.size() || images == nullptr)
    return XR_ERROR_SIZE_INSUFFICIENT;
  auto* d3d11Images = reinterpret_cast<XrSwapchainImageD3D11KHR*>(images);
  for (size_t index = 0; index < swapchainState->images.size(); ++index) {
    d3d11Images[index].type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
    d3d11Images[index].next = nullptr;
    d3d11Images[index].texture = swapchainState->images[index];
  }
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrAcquireSwapchainImage(
    XrSwapchain swapchain, const XrSwapchainImageAcquireInfo*, uint32_t* index) {
  if (swapchain == XR_NULL_HANDLE || index == nullptr) return XR_ERROR_VALIDATION_FAILURE;
  auto* swapchainState = SwapchainState(swapchain);
  *index = swapchainState->nextImage++ % static_cast<uint32_t>(swapchainState->images.size());
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrWaitSwapchainImage(
    XrSwapchain swapchain, const XrSwapchainImageWaitInfo*) {
  return swapchain == XR_NULL_HANDLE ? XR_ERROR_HANDLE_INVALID : XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrReleaseSwapchainImage(
    XrSwapchain swapchain, const XrSwapchainImageReleaseInfo*) {
  return swapchain == XR_NULL_HANDLE ? XR_ERROR_HANDLE_INVALID : XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrWaitFrame(
    XrSession session, const XrFrameWaitInfo*, XrFrameState* frameState) {
  if (session == XR_NULL_HANDLE || frameState == nullptr) return XR_ERROR_VALIDATION_FAILURE;
  const XrTime time = g_displayTime.fetch_add(11111111) + 11111111;
  frameState->type = XR_TYPE_FRAME_STATE;
  frameState->next = nullptr;
  frameState->predictedDisplayTime = time;
  frameState->predictedDisplayPeriod = 11111111;
  frameState->shouldRender = XR_TRUE;
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrBeginFrame(XrSession session, const XrFrameBeginInfo*) {
  return session == XR_NULL_HANDLE ? XR_ERROR_HANDLE_INVALID : XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo) {
  if (session == XR_NULL_HANDLE || frameEndInfo == nullptr) return XR_ERROR_VALIDATION_FAILURE;
  const uint64_t frame = ++g_frameCount;
  uint32_t stereoLayers = 0;
  for (uint32_t index = 0; index < frameEndInfo->layerCount; ++index) {
    const auto* layer = frameEndInfo->layers[index];
    if (layer != nullptr && layer->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
      const auto* projection = reinterpret_cast<const XrCompositionLayerProjection*>(layer);
      if (projection->viewCount == 2) ++stereoLayers;
    }
  }
  if (frame <= 3 || (frame % 60) == 0)
    RuntimeLog("end_frame frame=%llu layers=%u stereo_projection_layers=%u",
               static_cast<unsigned long long>(frame), frameEndInfo->layerCount, stereoLayers);
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrLocateViews(
    XrSession session, const XrViewLocateInfo*, XrViewState* viewState,
    uint32_t capacityInput, uint32_t* countOutput, XrView* views) {
  if (session == XR_NULL_HANDLE || viewState == nullptr || countOutput == nullptr)
    return XR_ERROR_VALIDATION_FAILURE;
  *countOutput = 2;
  if (capacityInput < 2 || views == nullptr) return XR_ERROR_SIZE_INSUFFICIENT;
  viewState->type = XR_TYPE_VIEW_STATE;
  viewState->next = nullptr;
  viewState->viewStateFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT |
      XR_VIEW_STATE_POSITION_VALID_BIT;
  for (uint32_t eye = 0; eye < 2; ++eye) {
    views[eye].type = XR_TYPE_VIEW;
    views[eye].next = nullptr;
    views[eye].pose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    views[eye].pose.position = {eye == 0 ? -0.032f : 0.032f, 0.0f, 0.0f};
    views[eye].fov = {-0.785398f, 0.785398f, 0.785398f, -0.785398f};
  }
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrStringToPath(
    XrInstance instance, const char* pathString, XrPath* path) {
  if (instance == XR_NULL_HANDLE || pathString == nullptr || path == nullptr)
    return XR_ERROR_VALIDATION_FAILURE;
  uint64_t hash = 1469598103934665603ull;
  for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(pathString);
       *cursor != 0; ++cursor) {
    hash ^= *cursor;
    hash *= 1099511628211ull;
  }
  *path = hash == XR_NULL_PATH ? 1 : hash;
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrCreateActionSet(
    XrInstance instance, const XrActionSetCreateInfo*, XrActionSet* actionSet) {
  if (instance == XR_NULL_HANDLE || actionSet == nullptr) return XR_ERROR_VALIDATION_FAILURE;
  *actionSet = MakeHandle<XrActionSet>(new XrActionSet_T());
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrDestroyActionSet(XrActionSet actionSet) {
  if (actionSet == XR_NULL_HANDLE) return XR_ERROR_HANDLE_INVALID;
  delete FromHandle<XrActionSet_T>(actionSet);
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrCreateAction(
    XrActionSet actionSet, const XrActionCreateInfo*, XrAction* action) {
  if (actionSet == XR_NULL_HANDLE || action == nullptr) return XR_ERROR_VALIDATION_FAILURE;
  *action = MakeHandle<XrAction>(new XrAction_T());
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrDestroyAction(XrAction action) {
  if (action == XR_NULL_HANDLE) return XR_ERROR_HANDLE_INVALID;
  delete FromHandle<XrAction_T>(action);
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrSuggestInteractionProfileBindings(
    XrInstance instance, const XrInteractionProfileSuggestedBinding*) {
  return instance == XR_NULL_HANDLE ? XR_ERROR_HANDLE_INVALID : XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrAttachSessionActionSets(
    XrSession session, const XrSessionActionSetsAttachInfo*) {
  return session == XR_NULL_HANDLE ? XR_ERROR_HANDLE_INVALID : XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrSyncActions(XrSession session, const XrActionsSyncInfo*) {
  return session == XR_NULL_HANDLE ? XR_ERROR_HANDLE_INVALID : XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrGetActionStateBoolean(
    XrSession session, const XrActionStateGetInfo*, XrActionStateBoolean* state) {
  if (session == XR_NULL_HANDLE || state == nullptr) return XR_ERROR_VALIDATION_FAILURE;
  state->isActive = XR_FALSE;
  state->currentState = XR_FALSE;
  state->changedSinceLastSync = XR_FALSE;
  state->lastChangeTime = 0;
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrGetActionStateFloat(
    XrSession session, const XrActionStateGetInfo*, XrActionStateFloat* state) {
  if (session == XR_NULL_HANDLE || state == nullptr) return XR_ERROR_VALIDATION_FAILURE;
  state->isActive = XR_FALSE;
  state->currentState = 0.0f;
  state->changedSinceLastSync = XR_FALSE;
  state->lastChangeTime = 0;
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrGetActionStateVector2f(
    XrSession session, const XrActionStateGetInfo*, XrActionStateVector2f* state) {
  if (session == XR_NULL_HANDLE || state == nullptr) return XR_ERROR_VALIDATION_FAILURE;
  state->isActive = XR_FALSE;
  state->currentState = {0.0f, 0.0f};
  state->changedSinceLastSync = XR_FALSE;
  state->lastChangeTime = 0;
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrGetActionStatePose(
    XrSession session, const XrActionStateGetInfo*, XrActionStatePose* state) {
  if (session == XR_NULL_HANDLE || state == nullptr) return XR_ERROR_VALIDATION_FAILURE;
  state->isActive = XR_FALSE;
  return XR_SUCCESS;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrGetInstanceProcAddr(
    XrInstance, const char* name, PFN_xrVoidFunction* function) {
  if (name == nullptr || function == nullptr) return XR_ERROR_VALIDATION_FAILURE;
  RuntimeLog("get_instance_proc_addr name=%s", name);
  *function = nullptr;
#define RETURN_FUNCTION(functionName) \
  if (std::strcmp(name, #functionName) == 0) { \
    *function = reinterpret_cast<PFN_xrVoidFunction>(&functionName); \
    return XR_SUCCESS; \
  }
  RETURN_FUNCTION(xrGetInstanceProcAddr)
  RETURN_FUNCTION(xrEnumerateInstanceExtensionProperties)
  RETURN_FUNCTION(xrCreateInstance)
  RETURN_FUNCTION(xrDestroyInstance)
  RETURN_FUNCTION(xrGetSystem)
  RETURN_FUNCTION(xrPollEvent)
  RETURN_FUNCTION(xrCreateSession)
  RETURN_FUNCTION(xrDestroySession)
  RETURN_FUNCTION(xrBeginSession)
  RETURN_FUNCTION(xrEndSession)
  RETURN_FUNCTION(xrCreateReferenceSpace)
  RETURN_FUNCTION(xrCreateActionSpace)
  RETURN_FUNCTION(xrDestroySpace)
  RETURN_FUNCTION(xrLocateSpace)
  RETURN_FUNCTION(xrEnumerateViewConfigurationViews)
  RETURN_FUNCTION(xrGetD3D11GraphicsRequirementsKHR)
  RETURN_FUNCTION(xrEnumerateSwapchainFormats)
  RETURN_FUNCTION(xrCreateSwapchain)
  RETURN_FUNCTION(xrDestroySwapchain)
  RETURN_FUNCTION(xrEnumerateSwapchainImages)
  RETURN_FUNCTION(xrAcquireSwapchainImage)
  RETURN_FUNCTION(xrWaitSwapchainImage)
  RETURN_FUNCTION(xrReleaseSwapchainImage)
  RETURN_FUNCTION(xrWaitFrame)
  RETURN_FUNCTION(xrBeginFrame)
  RETURN_FUNCTION(xrEndFrame)
  RETURN_FUNCTION(xrLocateViews)
  RETURN_FUNCTION(xrStringToPath)
  RETURN_FUNCTION(xrCreateActionSet)
  RETURN_FUNCTION(xrDestroyActionSet)
  RETURN_FUNCTION(xrCreateAction)
  RETURN_FUNCTION(xrDestroyAction)
  RETURN_FUNCTION(xrSuggestInteractionProfileBindings)
  RETURN_FUNCTION(xrAttachSessionActionSets)
  RETURN_FUNCTION(xrSyncActions)
  RETURN_FUNCTION(xrGetActionStateBoolean)
  RETURN_FUNCTION(xrGetActionStateFloat)
  RETURN_FUNCTION(xrGetActionStateVector2f)
  RETURN_FUNCTION(xrGetActionStatePose)
#undef RETURN_FUNCTION
  return XR_ERROR_FUNCTION_UNSUPPORTED;
}

FAKE_XR_EXPORT XrResult XRAPI_CALL xrNegotiateLoaderRuntimeInterface(
    const XrNegotiateLoaderInfo* loaderInfo, XrNegotiateRuntimeRequest* runtimeRequest) {
  if (loaderInfo == nullptr || runtimeRequest == nullptr ||
      loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
      runtimeRequest->structType != XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST ||
      loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION ||
      runtimeRequest->structVersion != XR_RUNTIME_INFO_STRUCT_VERSION ||
      loaderInfo->structSize < sizeof(XrNegotiateLoaderInfo) ||
      runtimeRequest->structSize < sizeof(XrNegotiateRuntimeRequest)) {
    return XR_ERROR_VALIDATION_FAILURE;
  }
  if (loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_RUNTIME_VERSION ||
      loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_RUNTIME_VERSION) {
    return XR_ERROR_INITIALIZATION_FAILED;
  }
  runtimeRequest->runtimeInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
  runtimeRequest->runtimeApiVersion = XR_CURRENT_API_VERSION;
  runtimeRequest->getInstanceProcAddr = &xrGetInstanceProcAddr;
  RuntimeLog("negotiate_loader_runtime_interface");
  return XR_SUCCESS;
}

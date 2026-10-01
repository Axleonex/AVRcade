#include "openxr/openxr_runtime.h"

#include "frame/stereo_math.h"

#include <openxr/openxr_reflection.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <iterator>
#include <thread>

namespace vrclient::runtime::openxr {
namespace {

constexpr float kNearZ = 0.05f;
constexpr float kFarZ = 100.0f;

bool xrOk(XrResult result) {
  return result >= 0;
}

bool instanceExtensionAvailable(const char* name) {
  if (name == nullptr) {
    return false;
  }
  uint32_t count = 0;
  if (!xrOk(xrEnumerateInstanceExtensionProperties(
          nullptr, 0, &count, nullptr))) {
    return false;
  }
  std::vector<XrExtensionProperties> properties(count);
  for (auto& property : properties) {
    property.type = XR_TYPE_EXTENSION_PROPERTIES;
  }
  if (!xrOk(xrEnumerateInstanceExtensionProperties(
          nullptr, count, &count, properties.data()))) {
    return false;
  }
  return std::any_of(
      properties.begin(), properties.end(),
      [name](const XrExtensionProperties& property) {
        return std::strcmp(property.extensionName, name) == 0;
      });
}

// Symbolic name for an XrResult, generated from the OpenXR SDK reflection macro
// (no hand-maintained table). xrResultToString needs a valid XrInstance, which
// we do not have on an instance-creation failure -- this works pre-instance, so
// a failure logs e.g. "XR_ERROR_API_VERSION_UNSUPPORTED" instead of a bare -4.
const char* xrResultName(XrResult result) {
  switch (result) {
#define VRCLIENT_XR_RESULT_CASE(name, value) \
  case value:                                \
    return #name;
    XR_LIST_ENUM_XrResult(VRCLIENT_XR_RESULT_CASE)
#undef VRCLIENT_XR_RESULT_CASE
    default:
      return "XR_UNKNOWN_RESULT";
  }
}

frame::EyeFov toEyeFov(const XrFovf& fov) {
  frame::EyeFov out;
  out.angle_left = fov.angleLeft;
  out.angle_right = fov.angleRight;
  out.angle_up = fov.angleUp;
  out.angle_down = fov.angleDown;
  return out;
}

VrRuntimePose toRuntimePose(const XrPosef& pose, XrSpaceLocationFlags flags) {
  VrRuntimePose out{};
  out.orientation.x = pose.orientation.x;
  out.orientation.y = pose.orientation.y;
  out.orientation.z = pose.orientation.z;
  out.orientation.w = pose.orientation.w;
  out.position.x = pose.position.x;
  out.position.y = pose.position.y;
  out.position.z = pose.position.z;
  out.orientation_valid =
      (flags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0 ? 1u : 0u;
  out.position_valid =
      (flags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 ? 1u : 0u;
  return out;
}

// True if the VkFormat is a depth/stencil format (which the runtime may also
// enumerate); such formats are never valid as a color attachment.
bool isDepthStencilFormat(VkFormat format) {
  switch (format) {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_S8_UINT:
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
      return true;
    default:
      return false;
  }
}

VkFormat chooseColorFormat(const std::vector<int64_t>& formats) {
  // Under a Vulkan binding, xrEnumerateSwapchainFormats returns VkFormat codes.
  // Prefer sRGB; fall back to UNORM. (sRGB first keeps gamma correct for the
  // compositor.)
  constexpr VkFormat preferred[] = {
      VK_FORMAT_R8G8B8A8_SRGB,
      VK_FORMAT_B8G8R8A8_SRGB,
      VK_FORMAT_R8G8B8A8_UNORM,
      VK_FORMAT_B8G8R8A8_UNORM,
  };

  for (VkFormat wanted : preferred) {
    const auto found = std::find(
        formats.begin(), formats.end(), static_cast<int64_t>(wanted));
    if (found != formats.end()) {
      return wanted;
    }
  }

  // Spec only guarantees the runtime offers >= 1 color format (not necessarily
  // one of the four above). Rather than fail on e.g. HDR-only runtimes
  // (A2B10G10R10 / R16G16B16A16_SFLOAT), fall back to the FIRST enumerated
  // non-depth/stencil format. The render pass is built from whatever format is
  // chosen, so any color format is renderable. (Runtimes enumerate in priority
  // order, so the first entry is the runtime's most-preferred color format.)
  for (int64_t code : formats) {
    const VkFormat candidate = static_cast<VkFormat>(code);
    if (candidate != VK_FORMAT_UNDEFINED && !isDepthStencilFormat(candidate)) {
      return candidate;
    }
  }

  return VK_FORMAT_UNDEFINED;
}

DXGI_FORMAT chooseD3D12ColorFormat(const std::vector<int64_t>& formats) {
  constexpr DXGI_FORMAT preferred[] = {
      DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
      DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
      DXGI_FORMAT_R8G8B8A8_UNORM,
      DXGI_FORMAT_B8G8R8A8_UNORM,
  };

  for (DXGI_FORMAT wanted : preferred) {
    const auto found = std::find(
        formats.begin(), formats.end(), static_cast<int64_t>(wanted));
    if (found != formats.end()) {
      return wanted;
    }
  }

  return DXGI_FORMAT_UNKNOWN;
}

frame::SessionSignal toSignal(XrSessionState state) {
  switch (state) {
    case XR_SESSION_STATE_READY:
      return frame::SessionSignal::Ready;
    case XR_SESSION_STATE_SYNCHRONIZED:
      return frame::SessionSignal::Synchronized;
    case XR_SESSION_STATE_VISIBLE:
      return frame::SessionSignal::Visible;
    case XR_SESSION_STATE_FOCUSED:
      return frame::SessionSignal::Focused;
    case XR_SESSION_STATE_STOPPING:
      return frame::SessionSignal::Stopping;
    case XR_SESSION_STATE_LOSS_PENDING:
      return frame::SessionSignal::LossPending;
    case XR_SESSION_STATE_EXITING:
      return frame::SessionSignal::Exiting;
    default:
      return frame::SessionSignal::Idle;
  }
}

}  // namespace

OpenXrRuntime::OpenXrRuntime(const VrRuntimeDesc& desc)
    : application_name_(desc.application_name != nullptr ? desc.application_name : "VR Conversion Manager"),
      profile_path_(desc.runtime_profile_path != nullptr ? desc.runtime_profile_path : ""),
      graphics_backend_(desc.preferred_graphics_backend),
      state_callback_(desc.state_callback),
      state_callback_user_data_(desc.state_callback_user_data) {
  if (desc.d3d12_binding != nullptr) {
    d3d12_borrowed_device_ =
        static_cast<ID3D12Device*>(desc.d3d12_binding->device);
    d3d12_borrowed_queue_ =
        static_cast<ID3D12CommandQueue*>(desc.d3d12_binding->queue);
    d3d12_borrowed_binding_.device =
        d3d12_borrowed_device_.Get();
    d3d12_borrowed_binding_.queue =
        d3d12_borrowed_queue_.Get();
    has_d3d12_borrowed_binding_ = true;
  }
  current_frame_.runtime_state = VR_RUNTIME_STATE_STOPPED;
  current_frame_.head_pose = {};
  current_frame_.eye_count = 2;
  current_frame_.eyes[0].eye = VR_RUNTIME_EYE_LEFT;
  current_frame_.eyes[1].eye = VR_RUNTIME_EYE_RIGHT;
  current_frame_.eyes[0].pose = {};
  current_frame_.eyes[1].pose = {};
  current_frame_.eyes[0].projection = frame::identityMatrix();
  current_frame_.eyes[1].projection = frame::identityMatrix();
  current_frame_.eyes[0].view = frame::identityMatrix();
  current_frame_.eyes[1].view = frame::identityMatrix();
  current_frame_.dynamic_resolution_scale = 1.0f;
  current_frame_.foveation_preset = VR_RUNTIME_FOVEATION_OFF;
  publishSnapshots();
  diagnostics_.initialize(application_name_, nullptr);
}

OpenXrRuntime::~OpenXrRuntime() {
  destroyOpenXrObjects();
  diagnostics_.shutdown();
}

VrRuntimeResult OpenXrRuntime::start() {
  diagnostics_.logRuntimeEvent(diagnostics::Severity::Info, "runtime_start_requested");

  // A prior stop()/EXITING latches exit_requested_; clear it so a restarted
  // session on the same handle can run frames again.
  exit_requested_ = false;
  last_result_ = VR_RUNTIME_OK;

  const auto profile_result = perf::loadRuntimeProfileFromFile(profile_path_.c_str());
  if (profile_result.result != VR_RUNTIME_OK) {
    diagnostics_.logger().log(diagnostics::Severity::Warning, "runtime_profile_load_failed", {
        {"profile_path", profile_path_},
        {"message", profile_result.message},
    });
    return fail(profile_result.result, 0);
  }

  profile_ = profile_result.profile;
  dynamic_resolution_.reset(profile_.dynamic_resolution);
  foveation_ = perf::makeFoveationSettings(profile_.foveation);
  pacing_.reset(profile_.frame_pacing);
  current_frame_.dynamic_resolution_scale = dynamic_resolution_.scale();
  current_frame_.foveation_preset = foveation_.preset;

  setState(VR_RUNTIME_STATE_INITIALIZING, 0);

  if (createInstance() != VR_RUNTIME_OK ||
      createSystem() != VR_RUNTIME_OK ||
      createGraphicsDevice() != VR_RUNTIME_OK) {
    return last_result_;
  }

  if (graphics_backend_ == VR_RUNTIME_GRAPHICS_BACKEND_VULKAN) {
    if (!test_renderer_.initialize(
            vulkan_graphics_.instance,
            vulkan_graphics_.physical_device,
            vulkan_graphics_.device,
            vulkan_graphics_.queue_family_index,
            vulkan_graphics_.queue)) {
      return fail(VR_RUNTIME_ERROR_GRAPHICS, 0);
    }

    // The built-in headset scene stays Vulkan-only. D3D12 callers provide a
    // callback that writes into the returned eye resources.
    test_renderer_.setOverlayEnabled(true);
  }

  if (createSession() != VR_RUNTIME_OK ||
      createSpaces() != VR_RUNTIME_OK ||
      createSwapchains() != VR_RUNTIME_OK) {
    return last_result_;
  }

  setState(VR_RUNTIME_STATE_READY, 0);
  diagnostics_.logRuntimeEvent(diagnostics::Severity::Info, "runtime_ready");
  return VR_RUNTIME_OK;
}

VrRuntimeResult OpenXrRuntime::pollEvents() {
  if (instance_ == XR_NULL_HANDLE) {
    return VR_RUNTIME_ERROR_STATE;
  }

  XrEventDataBuffer event_data{};
  event_data.type = XR_TYPE_EVENT_DATA_BUFFER;

  XrResult poll_result = XR_SUCCESS;
  while ((poll_result = xrPollEvent(instance_, &event_data)) == XR_SUCCESS) {
    switch (event_data.type) {
      case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
        const auto* changed =
            reinterpret_cast<const XrEventDataSessionStateChanged*>(&event_data);
        const frame::SessionSignal signal = toSignal(changed->state);
        const VrRuntimeState before = lifecycle_.state();
        const VrRuntimeState after = lifecycle_.apply(signal);
        if (after != before) {
          setState(after, static_cast<int32_t>(changed->state));
        }

        if (changed->state == XR_SESSION_STATE_READY) {
          const VrRuntimeResult begin_result = beginSession();
          if (begin_result != VR_RUNTIME_OK) {
            return begin_result;
          }
        } else if (changed->state == XR_SESSION_STATE_STOPPING) {
          const VrRuntimeResult end_result = endSession();
          if (end_result != VR_RUNTIME_OK) {
            return end_result;
          }
        } else if (changed->state == XR_SESSION_STATE_EXITING) {
          exit_requested_ = true;
        } else if (changed->state == XR_SESSION_STATE_LOSS_PENDING) {
          exit_requested_ = true;
        }
        break;
      }
      case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
        lifecycle_.apply(frame::SessionSignal::RuntimeRestarted);
        setState(VR_RUNTIME_STATE_LOSS_PENDING, 0);
        exit_requested_ = true;
        break;
      default:
        break;
    }

    event_data = {};
    event_data.type = XR_TYPE_EVENT_DATA_BUFFER;
  }

  if (poll_result == XR_EVENT_UNAVAILABLE) {
    return VR_RUNTIME_OK;
  }
  return fail(VR_RUNTIME_ERROR_VR_API, poll_result);
}

VrRuntimeResult OpenXrRuntime::runFrame(
    VrRuntimeRenderCallback callback,
    void* user_data) {
  if (exit_requested_ || lifecycle_.state() == VR_RUNTIME_STATE_EXITING) {
    return VR_RUNTIME_SKIPPED;
  }

  const VrRuntimeResult poll_result = pollEvents();
  if (poll_result != VR_RUNTIME_OK) {
    return poll_result;
  }

  if (!session_begun_) {
    return VR_RUNTIME_SKIPPED;
  }

  XrFrameWaitInfo wait_info{};
  wait_info.type = XR_TYPE_FRAME_WAIT_INFO;
  XrFrameState frame_state{};
  frame_state.type = XR_TYPE_FRAME_STATE;

  // HOT PATH BEGIN: OpenXR wait/begin/render/end frame loop.
  XrResult result = xrWaitFrame(session_, &wait_info, &frame_state);
  VrRuntimeResult frame_result = VR_RUNTIME_OK;
  int32_t frame_detail_code = 0;
  if (!xrOk(result)) {
    frame_result = VR_RUNTIME_ERROR_VR_API;
    frame_detail_code = result;
  }

  XrFrameBeginInfo begin_info{};
  begin_info.type = XR_TYPE_FRAME_BEGIN_INFO;
  if (frame_result == VR_RUNTIME_OK) {
    result = xrBeginFrame(session_, &begin_info);
    if (!xrOk(result)) {
      frame_result = VR_RUNTIME_ERROR_VR_API;
      frame_detail_code = result;
    }
  }

  VrRuntimeResult render_result = VR_RUNTIME_OK;
  if (frame_result == VR_RUNTIME_OK) {
    if (frame_state.shouldRender == XR_TRUE) {
      render_result = renderFrame(
          frame_state,
          callback,
          user_data,
          &frame_detail_code);
    } else {
      render_result = endEmptyFrame(
          frame_state.predictedDisplayTime,
          &frame_detail_code);
    }
  }
  // HOT PATH END.

  if (frame_result != VR_RUNTIME_OK) {
    return fail(frame_result, frame_detail_code);
  }
  // Bring-up robustness: a RECOVERABLE per-frame render hiccup (views not yet
  // locatable, or a transient acquire/wait-swapchain code) must NOT kill the
  // session. renderFrame/endEmptyFrame already submitted an empty frame to keep
  // the compositor's frame cycle balanced (the xrBeginFrame above was matched by
  // an xrEndFrame); we report SKIPPED and let the bounded loop continue,
  // mirroring the Khronos hello_xr reference (a frame whose views aren't
  // locatable is a no-op end-frame, not a fatal error). Only a HARD error trips
  // fail()/ERROR (a terminal state the harness loop treats as stop-and-exit).
  if (render_result == VR_RUNTIME_SKIPPED) {
    return VR_RUNTIME_SKIPPED;
  }
  if (render_result != VR_RUNTIME_OK) {
    return fail(render_result, frame_detail_code);
  }

  diagnostics_.updateFrameSnapshot(current_frame_, headset_state_);

  // Push the overlay error-indicator state for the NEXT frame, OUTSIDE the hot
  // path markers (we are past `// HOT PATH END.` above). renderEye reads only the
  // plain bool, never diagnostics_, so the recorded hot path stays clean. Using
  // the just-updated snapshot's warning count is fine for a status indicator.
  test_renderer_.setOverlayState(
      diagnostics_.overlay().snapshot().recent_warning_count > 0);
  return VR_RUNTIME_OK;
}

VrRuntimeResult OpenXrRuntime::sampleHeadPose(VrRuntimePose* pose) {
  if (pose == nullptr) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }
  *pose = {};
  if (!has_win32_time_conversion_ || convert_qpc_to_time_ == nullptr ||
      instance_ == XR_NULL_HANDLE || view_space_ == XR_NULL_HANDLE ||
      local_space_ == XR_NULL_HANDLE || headsetState().session_active == 0) {
    return VR_RUNTIME_SKIPPED;
  }

  LARGE_INTEGER counter{};
  if (!QueryPerformanceCounter(&counter)) {
    return VR_RUNTIME_ERROR_STATE;
  }
  XrTime sample_time = 0;
  XrResult result = convert_qpc_to_time_(instance_, &counter, &sample_time);
  if (!xrOk(result)) {
    return VR_RUNTIME_ERROR_VR_API;
  }

  XrSpaceLocation location{};
  location.type = XR_TYPE_SPACE_LOCATION;
  result = xrLocateSpace(view_space_, local_space_, sample_time, &location);
  if (!xrOk(result)) {
    return VR_RUNTIME_ERROR_VR_API;
  }
  constexpr XrSpaceLocationFlags kTrackedOrientation =
      XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
      XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
  if ((location.locationFlags & kTrackedOrientation) != kTrackedOrientation) {
    return VR_RUNTIME_SKIPPED;
  }
  *pose = toRuntimePose(location.pose, location.locationFlags);
  return VR_RUNTIME_OK;
}

VrRuntimeResult OpenXrRuntime::stop() {
  diagnostics_.logRuntimeEvent(diagnostics::Severity::Info, "runtime_stop_requested");
  exit_requested_ = true;

  // Graceful OpenXR session-exit handshake. Calling xrEndSession() directly here
  // (the old behaviour) returns XR_ERROR_SESSION_NOT_STOPPING (-29) because the
  // session is still FOCUSED/VISIBLE, not STOPPING. The spec-correct teardown is
  // xrRequestExitSession -> the runtime drives the session to STOPPING -> then
  // xrEndSession. pollEvents() already performs the (now-legal) xrEndSession when
  // it sees XR_SESSION_STATE_STOPPING and clears session_begun_, so here we just
  // request exit and pump events until the session winds down. The loop is bounded
  // by a wall-clock deadline so a non-cooperating runtime can never hang teardown;
  // if the deadline is hit before STOPPING arrives, destroyOpenXrObjects() below
  // still cleans up because xrDestroySession is legal in ANY session state (it
  // implicitly ends a running session) -- so we never call xrEndSession outside
  // STOPPING and never reproduce the -29.
  if (session_begun_ && session_ != XR_NULL_HANDLE) {
    const XrResult request_result = xrRequestExitSession(session_);
    if (!xrOk(request_result)) {
      diagnostics_.logger().log(diagnostics::Severity::Warning, "openxr_request_exit_failed", {
          {"xr_result", std::to_string(static_cast<int>(request_result))},
          {"xr_result_name", xrResultName(request_result)},
      });
    }

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (session_begun_ &&
           std::chrono::steady_clock::now() < deadline) {
      // pollEvents() ends the session on STOPPING and sets exit_requested_ on
      // EXITING. A non-OK result means a teardown-time XR call failed -- surface
      // it (don't swallow it) and stop draining; destroyOpenXrObjects() below is
      // still the cleanup backstop.
      const VrRuntimeResult drain_result = pollEvents();
      if (drain_result != VR_RUNTIME_OK) {
        diagnostics_.logger().log(diagnostics::Severity::Warning, "openxr_stop_drain_poll_failed", {
            {"result", std::to_string(static_cast<int>(drain_result))},
        });
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }

  destroyOpenXrObjects();
  lifecycle_.reset();
  setState(VR_RUNTIME_STATE_STOPPED, 0);
  return VR_RUNTIME_OK;
}

VrRuntimeResult OpenXrRuntime::createInstance() {
  const char* graphics_extension =
      graphics_backend_ == VR_RUNTIME_GRAPHICS_BACKEND_D3D12
          ? XR_KHR_D3D12_ENABLE_EXTENSION_NAME
          : XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME;
  std::vector<const char*> extensions = {graphics_extension};
  has_win32_time_conversion_ = instanceExtensionAvailable(
      XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME);
  if (has_win32_time_conversion_) {
    extensions.push_back(
        XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME);
  }

  XrInstanceCreateInfo create_info{};
  create_info.type = XR_TYPE_INSTANCE_CREATE_INFO;
  std::strncpy(
      create_info.applicationInfo.applicationName,
      application_name_.c_str(),
      XR_MAX_APPLICATION_NAME_SIZE - 1);
  create_info.applicationInfo.applicationVersion = 1;
  std::strncpy(
      create_info.applicationInfo.engineName,
      "VRClientRuntime",
      XR_MAX_ENGINE_NAME_SIZE - 1);
  create_info.applicationInfo.engineVersion = 1;
  // Request OpenXR 1.0, NOT XR_CURRENT_API_VERSION. Our SDK headers are OpenXR
  // 1.1, so XR_CURRENT_API_VERSION = 1.1.x -- but common runtimes (SteamVR,
  // Virtual Desktop's virtualdesktop-openxr, etc.) implement only 1.0 and reject
  // a 1.1 instance with XR_ERROR_API_VERSION_UNSUPPORTED (-4): the "chained
  // CreateInstance call failed" seen on a real headset. We use no 1.1-only
  // features (only a graphics KHR extension), and a 1.1 runtime
  // still accepts a 1.0 app, so 1.0 is the maximally-compatible request.
  create_info.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
  create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  create_info.enabledExtensionNames = extensions.data();

  const XrResult result = xrCreateInstance(&create_info, &instance_);
  if (!xrOk(result)) {
    diagnostics_.logger().log(diagnostics::Severity::Error, "openxr_call_failed", {
        {"xr_result", std::to_string(static_cast<int>(result))},
        {"xr_result_name", xrResultName(result)},
    });
    return fail(VR_RUNTIME_ERROR_RUNTIME_UNAVAILABLE, result);
  }

  if (has_win32_time_conversion_) {
    PFN_xrVoidFunction function = nullptr;
    const XrResult proc_result = xrGetInstanceProcAddr(
        instance_, "xrConvertWin32PerformanceCounterToTimeKHR", &function);
    if (!xrOk(proc_result) || function == nullptr) {
      return fail(VR_RUNTIME_ERROR_RUNTIME_UNAVAILABLE, proc_result);
    }
    convert_qpc_to_time_ =
        reinterpret_cast<PFN_xrConvertWin32PerformanceCounterToTimeKHR>(
            function);
  }

  // Identity evidence (lifecycle, NOT hot path): record the active runtime name
  // for CORE-01. Read-only; a failure here is non-fatal (the instance is valid).
  XrInstanceProperties instance_props{};
  instance_props.type = XR_TYPE_INSTANCE_PROPERTIES;
  if (xrOk(xrGetInstanceProperties(instance_, &instance_props))) {
    diagnostics_.logger().log(diagnostics::Severity::Info, "openxr_runtime_identity", {
        {"runtime_name", instance_props.runtimeName},
        {"runtime_version", std::to_string(static_cast<uint64_t>(instance_props.runtimeVersion))},
    });
  }

  return VR_RUNTIME_OK;
}

VrRuntimeResult OpenXrRuntime::createSystem() {
  XrSystemGetInfo system_info{};
  system_info.type = XR_TYPE_SYSTEM_GET_INFO;
  system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

  const XrResult result = xrGetSystem(instance_, &system_info, &system_id_);
  if (!xrOk(result)) {
    diagnostics_.logger().log(diagnostics::Severity::Error, "openxr_call_failed", {
        {"xr_result", std::to_string(static_cast<int>(result))},
        {"xr_result_name", xrResultName(result)},
    });
    return fail(VR_RUNTIME_ERROR_RUNTIME_UNAVAILABLE, result);
  }

  // Identity evidence (lifecycle, NOT hot path): record the HMD system name for
  // CORE-01. Read-only; a failure here is non-fatal (the system id is valid).
  XrSystemProperties system_props{};
  system_props.type = XR_TYPE_SYSTEM_PROPERTIES;
  if (xrOk(xrGetSystemProperties(instance_, system_id_, &system_props))) {
    diagnostics_.logger().log(diagnostics::Severity::Info, "openxr_system_identity", {
        {"system_name", system_props.systemName},
        {"vendor_id", std::to_string(static_cast<uint64_t>(system_props.vendorId))},
        {"max_layer_count", std::to_string(static_cast<uint64_t>(system_props.graphicsProperties.maxLayerCount))},
    });
  }

  return VR_RUNTIME_OK;
}

VrRuntimeResult OpenXrRuntime::createGraphicsDevice() {
  if (graphics_backend_ == VR_RUNTIME_GRAPHICS_BACKEND_D3D12) {
    if (!createD3D12DeviceForOpenXr(
            instance_,
            system_id_,
            has_d3d12_borrowed_binding_ ? &d3d12_borrowed_binding_ : nullptr,
            &d3d12_graphics_)) {
      return fail(VR_RUNTIME_ERROR_GRAPHICS, 0);
    }
  } else {
    if (!createVulkanDeviceForOpenXr(
            instance_, system_id_, &vulkan_graphics_)) {
      return fail(VR_RUNTIME_ERROR_GRAPHICS, 0);
    }
  }

  return VR_RUNTIME_OK;
}

VrRuntimeResult OpenXrRuntime::createSession() {
  XrSessionCreateInfo create_info{};
  create_info.type = XR_TYPE_SESSION_CREATE_INFO;
  create_info.systemId = system_id_;

  XrGraphicsBindingD3D12KHR d3d12_binding{};
  XrGraphicsBindingVulkanKHR vulkan_binding{};
  if (graphics_backend_ == VR_RUNTIME_GRAPHICS_BACKEND_D3D12) {
    d3d12_binding.type = XR_TYPE_GRAPHICS_BINDING_D3D12_KHR;
    d3d12_binding.device = d3d12_graphics_.device.Get();
    d3d12_binding.queue = d3d12_graphics_.queue.Get();
    create_info.next = &d3d12_binding;
  } else {
    vulkan_binding.type = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR;
    vulkan_binding.instance = vulkan_graphics_.instance;
    vulkan_binding.physicalDevice = vulkan_graphics_.physical_device;
    vulkan_binding.device = vulkan_graphics_.device;
    vulkan_binding.queueFamilyIndex = vulkan_graphics_.queue_family_index;
    vulkan_binding.queueIndex = 0;
    create_info.next = &vulkan_binding;
  }

  const XrResult result =
      xrCreateSession(instance_, &create_info, &session_);
  if (!xrOk(result)) {
    return fail(VR_RUNTIME_ERROR_VR_API, result);
  }

  return VR_RUNTIME_OK;
}

VrRuntimeResult OpenXrRuntime::createSpaces() {
  XrReferenceSpaceCreateInfo space_info{};
  space_info.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
  space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
  space_info.poseInReferenceSpace.orientation.w = 1.0f;

  XrResult result = xrCreateReferenceSpace(session_, &space_info, &local_space_);
  if (!xrOk(result)) {
    return fail(VR_RUNTIME_ERROR_VR_API, result);
  }

  space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
  result = xrCreateReferenceSpace(session_, &space_info, &view_space_);
  if (!xrOk(result)) {
    return fail(VR_RUNTIME_ERROR_VR_API, result);
  }

  return VR_RUNTIME_OK;
}

VrRuntimeResult OpenXrRuntime::createSwapchains() {
  uint32_t view_count = 0;
  XrResult result = xrEnumerateViewConfigurationViews(
      instance_,
      system_id_,
      XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
      0,
      &view_count,
      nullptr);
  if (!xrOk(result) || view_count < 2) {
    return fail(VR_RUNTIME_ERROR_VR_API, result);
  }

  for (XrViewConfigurationView& config : view_configurations_) {
    config = {};
    config.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
  }

  result = xrEnumerateViewConfigurationViews(
      instance_,
      system_id_,
      XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
      static_cast<uint32_t>(view_configurations_.size()),
      &view_count,
      view_configurations_.data());
  if (!xrOk(result)) {
    return fail(VR_RUNTIME_ERROR_VR_API, result);
  }

  uint32_t format_count = 0;
  result = xrEnumerateSwapchainFormats(session_, 0, &format_count, nullptr);
  if (!xrOk(result) || format_count == 0) {
    return fail(VR_RUNTIME_ERROR_VR_API, result);
  }

  std::vector<int64_t> formats(format_count);
  result = xrEnumerateSwapchainFormats(
      session_,
      format_count,
      &format_count,
      formats.data());
  if (!xrOk(result)) {
    return fail(VR_RUNTIME_ERROR_VR_API, result);
  }

  int64_t color_format = 0;
  if (graphics_backend_ == VR_RUNTIME_GRAPHICS_BACKEND_D3D12) {
    color_format =
        static_cast<int64_t>(chooseD3D12ColorFormat(formats));
  } else {
    color_format = static_cast<int64_t>(chooseColorFormat(formats));
  }

  if (color_format == 0) {
    return fail(VR_RUNTIME_ERROR_GRAPHICS, 0);
  }

  const float resolution_scale = dynamic_resolution_.scale();

  for (uint32_t eye = 0; eye < 2; ++eye) {
    EyeSwapchain& swapchain = swapchains_[eye];
    const XrViewConfigurationView& view_config = view_configurations_[eye];
    swapchain.width = std::max(
        1,
        static_cast<int32_t>(
            static_cast<float>(view_config.recommendedImageRectWidth) * resolution_scale));
    swapchain.height = std::max(
        1,
        static_cast<int32_t>(
            static_cast<float>(view_config.recommendedImageRectHeight) * resolution_scale));
    swapchain.format = color_format;

    XrSwapchainCreateInfo create_info{};
    create_info.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
    create_info.usageFlags =
        XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    create_info.format = color_format;
    create_info.sampleCount = view_config.recommendedSwapchainSampleCount;
    create_info.width = static_cast<uint32_t>(swapchain.width);
    create_info.height = static_cast<uint32_t>(swapchain.height);
    create_info.faceCount = 1;
    create_info.arraySize = 1;
    create_info.mipCount = 1;

    result = xrCreateSwapchain(session_, &create_info, &swapchain.handle);
    if (!xrOk(result)) {
      return fail(VR_RUNTIME_ERROR_VR_API, result);
    }

    uint32_t image_count = 0;
    result = xrEnumerateSwapchainImages(swapchain.handle, 0, &image_count, nullptr);
    if (!xrOk(result) || image_count == 0) {
      return fail(VR_RUNTIME_ERROR_VR_API, result);
    }

    if (graphics_backend_ == VR_RUNTIME_GRAPHICS_BACKEND_D3D12) {
      swapchain.d3d12_images.resize(image_count);
      for (XrSwapchainImageD3D12KHR& image : swapchain.d3d12_images) {
        image = {};
        image.type = XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR;
      }
      result = xrEnumerateSwapchainImages(
          swapchain.handle,
          image_count,
          &image_count,
          reinterpret_cast<XrSwapchainImageBaseHeader*>(
              swapchain.d3d12_images.data()));
    } else {
      swapchain.vulkan_images.resize(image_count);
      for (XrSwapchainImageVulkanKHR& image : swapchain.vulkan_images) {
        image = {};
        image.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
      }
      result = xrEnumerateSwapchainImages(
          swapchain.handle,
          image_count,
          &image_count,
          reinterpret_cast<XrSwapchainImageBaseHeader*>(
              swapchain.vulkan_images.data()));
    }
    if (!xrOk(result)) {
      return fail(VR_RUNTIME_ERROR_VR_API, result);
    }

    if (graphics_backend_ == VR_RUNTIME_GRAPHICS_BACKEND_VULKAN) {
      std::vector<VkImage> vk_images;
      vk_images.reserve(swapchain.vulkan_images.size());
      for (const XrSwapchainImageVulkanKHR& image :
           swapchain.vulkan_images) {
        vk_images.push_back(image.image);
      }

      if (!test_renderer_.prepareEyeTargets(
              static_cast<VrRuntimeEye>(eye),
              vk_images.data(),
              static_cast<uint32_t>(vk_images.size()),
              static_cast<VkFormat>(color_format),
              static_cast<uint32_t>(swapchain.width),
              static_cast<uint32_t>(swapchain.height))) {
        return fail(VR_RUNTIME_ERROR_GRAPHICS, 0);
      }
    }

    headset_state_.recommended_width[eye] = static_cast<uint32_t>(swapchain.width);
    headset_state_.recommended_height[eye] = static_cast<uint32_t>(swapchain.height);

    // CORE-02 evidence (lifecycle, NOT hot path): record per-eye swapchain
    // FORMAT + IMAGE COUNT + dimensions. These live only in the private
    // EyeSwapchain struct and are intentionally NOT on the graphics-agnostic
    // public ABI (public_api_contract.py forbids OpenXR/Vk detail in the
    // header), so the headset-smoke harness reads them from this session log.
    diagnostics_.logger().log(diagnostics::Severity::Info, "openxr_swapchain_created", {
        {"eye", std::to_string(eye)},
        {"backend", graphics_backend_ == VR_RUNTIME_GRAPHICS_BACKEND_D3D12
                        ? "d3d12"
                        : "vulkan"},
        {"graphics_format", std::to_string(color_format)},
        {"image_count", std::to_string(static_cast<uint64_t>(
                            graphics_backend_ == VR_RUNTIME_GRAPHICS_BACKEND_D3D12
                                ? swapchain.d3d12_images.size()
                                : swapchain.vulkan_images.size()))},
        {"width", std::to_string(swapchain.width)},
        {"height", std::to_string(swapchain.height)},
        {"sample_count", std::to_string(view_config.recommendedSwapchainSampleCount)},
    });
  }

  headset_state_.view_count = 2;
  headset_state_.current_refresh_hz = static_cast<uint32_t>(perf::refreshModeHz(profile_.target_refresh_mode));
  publishSnapshots();

  return VR_RUNTIME_OK;
}

VrRuntimeResult OpenXrRuntime::beginSession() {
  if (session_begun_) {
    return VR_RUNTIME_OK;
  }

  XrSessionBeginInfo begin_info{};
  begin_info.type = XR_TYPE_SESSION_BEGIN_INFO;
  begin_info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
  const XrResult result = xrBeginSession(session_, &begin_info);
  if (!xrOk(result)) {
    return fail(VR_RUNTIME_ERROR_VR_API, result);
  }

  session_begun_ = true;
  headset_state_.session_active = 1;
  publishSnapshots();
  diagnostics_.updateHeadsetState(headset_state_);
  diagnostics_.logRuntimeEvent(diagnostics::Severity::Info, "openxr_session_begun");
  return VR_RUNTIME_OK;
}

VrRuntimeResult OpenXrRuntime::endSession() {
  if (!session_begun_ || session_ == XR_NULL_HANDLE) {
    session_begun_ = false;
    headset_state_.session_active = 0;
    publishSnapshots();
    return VR_RUNTIME_OK;
  }

  const XrResult result = xrEndSession(session_);
  session_begun_ = false;
  headset_state_.session_active = 0;
  publishSnapshots();
  diagnostics_.updateHeadsetState(headset_state_);
  if (!xrOk(result)) {
    return fail(VR_RUNTIME_ERROR_VR_API, result);
  }

  diagnostics_.logRuntimeEvent(diagnostics::Severity::Info, "openxr_session_ended");
  return VR_RUNTIME_OK;
}

VrRuntimeResult OpenXrRuntime::renderFrame(
    const XrFrameState& frame_state,
    VrRuntimeRenderCallback callback,
    void* user_data,
    int32_t* out_detail_code) {
  uint32_t located_view_count = 0;
  VrRuntimeResult result = updateFrameData(
      frame_state.predictedDisplayTime,
      frame_state.predictedDisplayPeriod,
      &located_view_count,
      out_detail_code);
  if (result != VR_RUNTIME_OK) {
    const VrRuntimeResult end_result =
        endEmptyFrame(frame_state.predictedDisplayTime, out_detail_code);
    if (end_result != VR_RUNTIME_OK) {
      return end_result;
    }
    return result;
  }
  if (graphics_backend_ == VR_RUNTIME_GRAPHICS_BACKEND_D3D12 &&
      callback == nullptr) {
    const VrRuntimeResult end_result =
        endEmptyFrame(frame_state.predictedDisplayTime, out_detail_code);
    if (end_result != VR_RUNTIME_OK) {
      return end_result;
    }
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }

  std::array<uint32_t, 2> image_indices{};
  std::array<bool, 2> image_acquired{};
  std::array<bool, 2> image_waited{};
  std::array<VrRuntimeRenderTarget, 2> render_targets{};
  std::array<XrCompositionLayerProjectionView, 2> projection_views{};

  for (uint32_t eye = 0; eye < located_view_count; ++eye) {
    EyeSwapchain& swapchain = swapchains_[eye];

    XrSwapchainImageAcquireInfo acquire_info{};
    acquire_info.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
    XrResult xr_result = xrAcquireSwapchainImage(
        swapchain.handle,
        &acquire_info,
        &image_indices[eye]);
    if (!xrOk(xr_result)) {
      // Transient swapchain-image acquire hiccup: skip this frame (an empty
      // frame is submitted below) rather than killing the session.
      if (out_detail_code != nullptr) {
        *out_detail_code = xr_result;
      }
      result = VR_RUNTIME_SKIPPED;
      break;
    }
    image_acquired[eye] = true;

    XrSwapchainImageWaitInfo wait_info{};
    wait_info.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
    wait_info.timeout = XR_INFINITE_DURATION;
    xr_result = xrWaitSwapchainImage(swapchain.handle, &wait_info);
    if (!xrOk(xr_result)) {
      // An acquired image is not releasable until wait succeeds. Leave this
      // image owned by the session teardown path and fail the runtime so a
      // poisoned swapchain is never used by a later frame.
      if (out_detail_code != nullptr) {
        *out_detail_code = xr_result;
      }
      result = VR_RUNTIME_ERROR_VR_API;
      break;
    }
    image_waited[eye] = true;

    render_targets[eye].backend = graphics_backend_;
    render_targets[eye].eye = static_cast<VrRuntimeEye>(eye);
    if (graphics_backend_ == VR_RUNTIME_GRAPHICS_BACKEND_D3D12) {
      render_targets[eye].color_texture =
          swapchain.d3d12_images[image_indices[eye]].texture;
    } else {
      render_targets[eye].color_texture = reinterpret_cast<void*>(
          swapchain.vulkan_images[image_indices[eye]].image);
    }
    render_targets[eye].color_array_index = 0;
    render_targets[eye].width = static_cast<uint32_t>(swapchain.width);
    render_targets[eye].height = static_cast<uint32_t>(swapchain.height);
    render_targets[eye].viewport = {
        0,
        0,
        swapchain.width,
        swapchain.height,
    };
    render_targets[eye].color_format = swapchain.format;

    if (callback == nullptr) {
      test_renderer_.renderEye(
          current_frame_,
          static_cast<VrRuntimeEye>(eye),
          image_indices[eye],
          swapchain.vulkan_images[image_indices[eye]].image);
    }
  }

  if (result == VR_RUNTIME_OK && callback != nullptr) {
    result = callback(
        user_data,
        &current_frame_,
        render_targets.data(),
        located_view_count);
  }

  for (uint32_t eye = 0; eye < located_view_count; ++eye) {
    if (!image_acquired[eye] || !image_waited[eye]) {
      continue;
    }
    XrSwapchainImageReleaseInfo release_info{};
    release_info.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
    const XrResult xr_result =
        xrReleaseSwapchainImage(swapchains_[eye].handle, &release_info);
    if (!xrOk(xr_result)) {
      if (out_detail_code != nullptr) {
        *out_detail_code = xr_result;
      }
      // Release failure takes precedence over an earlier recoverable result:
      // call ordering is no longer known to be sound for this session.
      result = VR_RUNTIME_ERROR_VR_API;
    }
  }

  if (result != VR_RUNTIME_OK) {
    const VrRuntimeResult end_result =
        endEmptyFrame(frame_state.predictedDisplayTime, out_detail_code);
    if (end_result != VR_RUNTIME_OK) {
      return end_result;
    }
    return result;
  }

  for (uint32_t eye = 0; eye < located_view_count; ++eye) {
    XrCompositionLayerProjectionView& projection_view = projection_views[eye];
    projection_view = {};
    projection_view.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
    projection_view.pose = views_[eye].pose;
    projection_view.fov = views_[eye].fov;
    projection_view.subImage.swapchain = swapchains_[eye].handle;
    projection_view.subImage.imageRect.offset = {0, 0};
    projection_view.subImage.imageRect.extent = {
        swapchains_[eye].width,
        swapchains_[eye].height,
    };
    projection_view.subImage.imageArrayIndex = 0;
  }

  XrCompositionLayerProjection projection_layer{};
  projection_layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
  projection_layer.space = local_space_;
  projection_layer.viewCount = located_view_count;
  projection_layer.views = projection_views.data();

  const XrCompositionLayerBaseHeader* layers[] = {
      reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection_layer),
  };

  XrFrameEndInfo end_info{};
  end_info.type = XR_TYPE_FRAME_END_INFO;
  end_info.displayTime = frame_state.predictedDisplayTime;
  end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  end_info.layerCount = 1;
  end_info.layers = layers;

  const XrResult xr_result = xrEndFrame(session_, &end_info);
  if (!xrOk(xr_result)) {
    if (out_detail_code != nullptr) {
      *out_detail_code = xr_result;
    }
    return VR_RUNTIME_ERROR_VR_API;
  }

  ++frame_index_;
  return VR_RUNTIME_OK;
}

VrRuntimeResult OpenXrRuntime::updateFrameData(
    XrTime predicted_display_time,
    XrDuration predicted_display_period,
    uint32_t* out_view_count,
    int32_t* out_detail_code) {
  if (out_view_count == nullptr) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }

  for (XrView& view : views_) {
    view = {};
    view.type = XR_TYPE_VIEW;
  }

  XrViewState view_state{};
  view_state.type = XR_TYPE_VIEW_STATE;

  XrViewLocateInfo locate_info{};
  locate_info.type = XR_TYPE_VIEW_LOCATE_INFO;
  locate_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
  locate_info.displayTime = predicted_display_time;
  locate_info.space = local_space_;

  uint32_t view_count = 0;
  XrResult result = xrLocateViews(
      session_,
      &locate_info,
      &view_state,
      static_cast<uint32_t>(views_.size()),
      &view_count,
      views_.data());
  if (!xrOk(result)) {
    // Genuine API failure (negative XrResult): a real error -> fail()/ERROR.
    if (out_detail_code != nullptr) {
      *out_detail_code = result;
    }
    return VR_RUNTIME_ERROR_VR_API;
  }

  // RECOVERABLE (not a hard error): xrLocateViews succeeded but tracking is not
  // valid yet -- fewer than 2 views, or the view-state pose flags are not yet
  // valid (common during SYNCHRONIZED / early VISIBLE before head tracking
  // converges). Per hello_xr this is a no-op frame, NOT a session kill: signal
  // SKIPPED so runFrame submits an empty frame and the loop continues.
  constexpr XrViewStateFlags kPoseValid =
      XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
  if (view_count < 2 || (view_state.viewStateFlags & kPoseValid) != kPoseValid) {
    if (out_detail_code != nullptr) {
      *out_detail_code = result;
    }
    return VR_RUNTIME_SKIPPED;
  }

  XrSpaceLocation head_location{};
  head_location.type = XR_TYPE_SPACE_LOCATION;
  result = xrLocateSpace(
      view_space_,
      local_space_,
      predicted_display_time,
      &head_location);
  if (xrOk(result)) {
    current_frame_.head_pose =
        toRuntimePose(head_location.pose, head_location.locationFlags);
  } else {
    current_frame_.head_pose = {};
  }

  for (uint32_t eye = 0; eye < 2; ++eye) {
    current_frame_.eyes[eye].eye = static_cast<VrRuntimeEye>(eye);
    current_frame_.eyes[eye].pose =
        toRuntimePose(views_[eye].pose, XR_SPACE_LOCATION_POSITION_VALID_BIT |
                                         XR_SPACE_LOCATION_ORIENTATION_VALID_BIT);
    current_frame_.eyes[eye].projection =
        frame::makeProjectionMatrix(toEyeFov(views_[eye].fov), kNearZ, kFarZ);
    current_frame_.eyes[eye].view = frame::makeViewMatrix(current_frame_.eyes[eye].pose);
    // Raw located FOV angles for engines that rebuild their own projection
    // (the baked matrix above is diagnostics-only).
    current_frame_.eyes[eye].fov_angle_left = views_[eye].fov.angleLeft;
    current_frame_.eyes[eye].fov_angle_right = views_[eye].fov.angleRight;
    current_frame_.eyes[eye].fov_angle_up = views_[eye].fov.angleUp;
    current_frame_.eyes[eye].fov_angle_down = views_[eye].fov.angleDown;
  }

  current_frame_.runtime_state = lifecycle_.state();
  current_frame_.timing.predicted_display_time_ns = predicted_display_time;
  // B4 fix: derive the predicted display PERIOD from XrFrameState (nanoseconds)
  // instead of hard-zero, so the API's period field is accurate for frame-pacing
  // evidence (CORE-03/07). The headset-smoke harness still cross-checks with a
  // host-side steady_clock measurement.
  current_frame_.timing.predicted_display_period_seconds =
      static_cast<double>(predicted_display_period) * 1e-9;
  current_frame_.timing.frame_index = frame_index_;
  current_frame_.eye_count = 2;
  current_frame_.dynamic_resolution_scale = dynamic_resolution_.scale();
  current_frame_.foveation_preset = foveation_.preset;
  headset_state_.runtime_state = lifecycle_.state();
  headset_state_.session_active = session_begun_ ? 1u : 0u;
  publishSnapshots();
  *out_view_count = 2;
  return VR_RUNTIME_OK;
}

VrRuntimeResult OpenXrRuntime::endEmptyFrame(
    XrTime display_time,
    int32_t* out_detail_code) {
  XrFrameEndInfo end_info{};
  end_info.type = XR_TYPE_FRAME_END_INFO;
  end_info.displayTime = display_time;
  end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  end_info.layerCount = 0;
  const XrResult result = xrEndFrame(session_, &end_info);
  if (!xrOk(result)) {
    if (out_detail_code != nullptr) {
      *out_detail_code = result;
    }
    return VR_RUNTIME_ERROR_VR_API;
  }
  return VR_RUNTIME_OK;
}

void OpenXrRuntime::destroyOpenXrObjects() {
  // Tear down all renderer Vulkan objects (image views / framebuffers reference
  // the swapchain VkImages OpenXR owns) BEFORE destroying the swapchains, so no
  // live view outlives its image. destroy() waits for the device to idle first.
  if (vulkan_graphics_.device != VK_NULL_HANDLE) {
    test_renderer_.destroy();
  }

  for (EyeSwapchain& swapchain : swapchains_) {
    if (swapchain.handle != XR_NULL_HANDLE) {
      xrDestroySwapchain(swapchain.handle);
      swapchain.handle = XR_NULL_HANDLE;
    }
    swapchain.vulkan_images.clear();
    swapchain.d3d12_images.clear();
  }

  if (view_space_ != XR_NULL_HANDLE) {
    xrDestroySpace(view_space_);
    view_space_ = XR_NULL_HANDLE;
  }

  if (local_space_ != XR_NULL_HANDLE) {
    xrDestroySpace(local_space_);
    local_space_ = XR_NULL_HANDLE;
  }

  if (session_ != XR_NULL_HANDLE) {
    xrDestroySession(session_);
    session_ = XR_NULL_HANDLE;
  }

  if (instance_ != XR_NULL_HANDLE) {
    xrDestroyInstance(instance_);
    instance_ = XR_NULL_HANDLE;
  }

  // The Vulkan device/instance are created (via the OpenXR enable2 path) AFTER the
  // XR instance and consumed by the XR session; destroy them only after the XR
  // session + instance are gone. Backend contexts are safe to destroy repeatedly.
  vulkan_graphics_.destroy();
  d3d12_graphics_.destroy();

  session_begun_ = false;
  headset_state_.session_active = 0;
  current_frame_.head_pose = {};
  for (auto& eye : current_frame_.eyes) {
    eye.pose = {};
  }
  system_id_ = XR_NULL_SYSTEM_ID;
}

// The only writer of the published structs. Lock scope is the two struct
// copies -- never held across an XR or graphics call.
void OpenXrRuntime::publishSnapshots() {
  std::lock_guard<std::mutex> lock(data_mutex_);
  published_headset_state_ = headset_state_;
  published_frame_ = current_frame_;
}

void OpenXrRuntime::setState(VrRuntimeState state, int32_t detail_code) {
  const VrRuntimeState previous = headset_state_.runtime_state;
  headset_state_.runtime_state = state;
  current_frame_.runtime_state = state;
  publishSnapshots();

  if (state_callback_ != nullptr && previous != state) {
    state_callback_(state_callback_user_data_, previous, state, detail_code);
  }

  if (previous != state) {
    diagnostics_.updateHeadsetState(headset_state_);
    diagnostics_.logStateTransition(previous, state, detail_code);
  }
}

VrRuntimeResult OpenXrRuntime::fail(VrRuntimeResult result, int32_t detail_code) {
  last_result_ = result;
  diagnostics_.logRuntimeError("runtime_failure", result, detail_code);
  lifecycle_.apply(frame::SessionSignal::Failure);
  setState(VR_RUNTIME_STATE_ERROR, detail_code);
  return result;
}

}  // namespace vrclient::runtime::openxr

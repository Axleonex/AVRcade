#include "public/vr_runtime_api.h"

#include "openxr/openxr_runtime.h"

#include <new>

struct VrRuntime {
  explicit VrRuntime(const VrRuntimeDesc& desc) : impl(desc) {}
  vrclient::runtime::openxr::OpenXrRuntime impl;
};

extern "C" {

VrRuntimeResult vr_runtime_create(const VrRuntimeDesc* desc, VrRuntime** runtime) {
  if (runtime == nullptr) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }
  *runtime = nullptr;
  if (desc == nullptr) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }
  if (desc->size < sizeof(VrRuntimeDesc)) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }

  if (desc->preferred_graphics_backend != VR_RUNTIME_GRAPHICS_BACKEND_VULKAN &&
      desc->preferred_graphics_backend !=
          VR_RUNTIME_GRAPHICS_BACKEND_D3D12) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }
  if (desc->d3d12_binding != nullptr) {
    if (desc->preferred_graphics_backend != VR_RUNTIME_GRAPHICS_BACKEND_D3D12 ||
        desc->d3d12_binding->size < sizeof(VrRuntimeD3D12Binding) ||
        desc->d3d12_binding->device == nullptr ||
        desc->d3d12_binding->queue == nullptr) {
      return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
    }
  }

  VrRuntime* created = nullptr;
  try {
    created = new (std::nothrow) VrRuntime(*desc);
  } catch (const std::bad_alloc&) {
    return VR_RUNTIME_ERROR_RUNTIME_UNAVAILABLE;
  } catch (...) {
    return VR_RUNTIME_ERROR_RUNTIME_UNAVAILABLE;
  }
  if (created == nullptr) {
    return VR_RUNTIME_ERROR_RUNTIME_UNAVAILABLE;
  }

  *runtime = created;
  return VR_RUNTIME_OK;
}

void vr_runtime_destroy(VrRuntime* runtime) {
  delete runtime;
}

VrRuntimeResult vr_runtime_start(VrRuntime* runtime) {
  if (runtime == nullptr) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }
  return runtime->impl.start();
}

VrRuntimeResult vr_runtime_poll_events(VrRuntime* runtime) {
  if (runtime == nullptr) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }
  return runtime->impl.pollEvents();
}

VrRuntimeResult vr_runtime_run_frame(
    VrRuntime* runtime,
    VrRuntimeRenderCallback render_callback,
    void* render_user_data) {
  if (runtime == nullptr) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }
  return runtime->impl.runFrame(render_callback, render_user_data);
}

VrRuntimeResult vr_runtime_stop(VrRuntime* runtime) {
  if (runtime == nullptr) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }
  return runtime->impl.stop();
}

VrRuntimeState vr_runtime_get_state(const VrRuntime* runtime) {
  if (runtime == nullptr) {
    return VR_RUNTIME_STATE_ERROR;
  }
  return runtime->impl.state();
}

VrRuntimeResult vr_runtime_get_headset_state(
    const VrRuntime* runtime,
    VrRuntimeHeadsetState* state) {
  if (runtime == nullptr || state == nullptr) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }
  *state = runtime->impl.headsetState();
  return VR_RUNTIME_OK;
}

VrRuntimeResult vr_runtime_get_frame_data(
    const VrRuntime* runtime,
    VrRuntimeFrameData* frame) {
  if (runtime == nullptr || frame == nullptr) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }
  *frame = runtime->impl.frameData();
  return VR_RUNTIME_OK;
}

VrRuntimeResult vr_runtime_sample_head_pose(
    VrRuntime* runtime,
    VrRuntimePose* pose) {
  if (runtime == nullptr || pose == nullptr) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }
  return runtime->impl.sampleHeadPose(pose);
}

const char* vr_runtime_result_name(VrRuntimeResult result) {
  switch (result) {
    case VR_RUNTIME_OK:
      return "VR_RUNTIME_OK";
    case VR_RUNTIME_SKIPPED:
      return "VR_RUNTIME_SKIPPED";
    case VR_RUNTIME_ERROR_INVALID_ARGUMENT:
      return "VR_RUNTIME_ERROR_INVALID_ARGUMENT";
    case VR_RUNTIME_ERROR_PROFILE:
      return "VR_RUNTIME_ERROR_PROFILE";
    case VR_RUNTIME_ERROR_VR_API:
      return "VR_RUNTIME_ERROR_VR_API";
    case VR_RUNTIME_ERROR_GRAPHICS:
      return "VR_RUNTIME_ERROR_GRAPHICS";
    case VR_RUNTIME_ERROR_STATE:
      return "VR_RUNTIME_ERROR_STATE";
    case VR_RUNTIME_ERROR_RUNTIME_UNAVAILABLE:
      return "VR_RUNTIME_ERROR_RUNTIME_UNAVAILABLE";
  }
  return "VR_RUNTIME_ERROR_UNKNOWN";
}

}  // extern "C"

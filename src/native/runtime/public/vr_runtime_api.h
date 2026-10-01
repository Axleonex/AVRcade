#pragma once

#include <stdint.h>

#if defined(_WIN32) && defined(VR_RUNTIME_BUILD_SHARED)
#  if defined(VR_RUNTIME_EXPORTS)
#    define VR_RUNTIME_API __declspec(dllexport)
#  else
#    define VR_RUNTIME_API __declspec(dllimport)
#  endif
#else
#  define VR_RUNTIME_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VrRuntime VrRuntime;

typedef enum VrRuntimeResult {
  VR_RUNTIME_OK = 0,
  VR_RUNTIME_SKIPPED = 1,
  VR_RUNTIME_ERROR_INVALID_ARGUMENT = -1,
  VR_RUNTIME_ERROR_PROFILE = -2,
  VR_RUNTIME_ERROR_VR_API = -3,
  VR_RUNTIME_ERROR_GRAPHICS = -4,
  VR_RUNTIME_ERROR_STATE = -5,
  VR_RUNTIME_ERROR_RUNTIME_UNAVAILABLE = -6
} VrRuntimeResult;

typedef enum VrRuntimeState {
  VR_RUNTIME_STATE_STOPPED = 0,
  VR_RUNTIME_STATE_INITIALIZING = 1,
  VR_RUNTIME_STATE_READY = 2,
  VR_RUNTIME_STATE_RUNNING = 3,
  VR_RUNTIME_STATE_DEGRADED = 4,
  VR_RUNTIME_STATE_LOSS_PENDING = 5,
  VR_RUNTIME_STATE_EXITING = 6,
  VR_RUNTIME_STATE_ERROR = 7
} VrRuntimeState;

typedef enum VrRuntimeGraphicsBackend {
  /* Reserved (legacy): the runtime no longer builds a D3D11 backend. Kept so the
     public ABI does not shrink; not an accepted selector. */
  VR_RUNTIME_GRAPHICS_BACKEND_D3D11 = 1,
  /* Vulkan (XR_KHR_vulkan_enable2). */
  VR_RUNTIME_GRAPHICS_BACKEND_VULKAN = 2,
  /* Direct3D 12 (XR_KHR_D3D12_enable). */
  VR_RUNTIME_GRAPHICS_BACKEND_D3D12 = 3
} VrRuntimeGraphicsBackend;

typedef enum VrRuntimeEye {
  VR_RUNTIME_EYE_LEFT = 0,
  VR_RUNTIME_EYE_RIGHT = 1
} VrRuntimeEye;

typedef enum VrRuntimeFoveationPreset {
  VR_RUNTIME_FOVEATION_OFF = 0,
  VR_RUNTIME_FOVEATION_LOW = 1,
  VR_RUNTIME_FOVEATION_MEDIUM = 2,
  VR_RUNTIME_FOVEATION_HIGH = 3
} VrRuntimeFoveationPreset;

/* Coordinate conventions: all poses are right-handed, -Z forward, +Y up,
   positions in meters. Matrices use the column-vector convention
   (transformed = M * v). */
typedef struct VrRuntimeVec3 {
  float x;
  float y;
  float z;
} VrRuntimeVec3;

typedef struct VrRuntimeQuat {
  float x;
  float y;
  float z;
  float w;
} VrRuntimeQuat;

typedef struct VrRuntimePose {
  VrRuntimeQuat orientation;
  VrRuntimeVec3 position;
  uint32_t orientation_valid;
  uint32_t position_valid;
} VrRuntimePose;

typedef struct VrRuntimeMatrix4 {
  float m[16];
} VrRuntimeMatrix4;

typedef struct VrRuntimeRect {
  int32_t x;
  int32_t y;
  int32_t width;
  int32_t height;
} VrRuntimeRect;

typedef struct VrRuntimeFrameTiming {
  int64_t predicted_display_time_ns;
  double predicted_display_period_seconds;
  uint64_t frame_index;
} VrRuntimeFrameTiming;

typedef struct VrRuntimeEyeView {
  VrRuntimeEye eye;
  VrRuntimePose pose;
  /* Baked projection with GL-style [-1,1] clip depth and fixed near 0.05 /
     far 100 planes. Intended for the runtime's own diagnostics; game engines
     should rebuild their projection from the raw fov_angle_* fields below. */
  VrRuntimeMatrix4 projection;
  VrRuntimeMatrix4 view;
  /* Raw per-eye field-of-view half-angles in radians, signed (left/down are
     typically negative). */
  float fov_angle_left;
  float fov_angle_right;
  float fov_angle_up;
  float fov_angle_down;
} VrRuntimeEyeView;

typedef struct VrRuntimeHeadsetState {
  VrRuntimeState runtime_state;
  uint32_t session_active;
  uint32_t view_count;
  uint32_t recommended_width[2];
  uint32_t recommended_height[2];
  uint32_t current_refresh_hz;
} VrRuntimeHeadsetState;

/* Pose freshness: the frame data handed to the render callback (and read via
   vr_runtime_get_frame_data while inside it) is fresh for the CURRENT frame.
   Outside the callback it is the last rendered frame's pose -- one frame of
   latency when used to prime a game camera. */
typedef struct VrRuntimeFrameData {
  VrRuntimeState runtime_state;
  VrRuntimeFrameTiming timing;
  VrRuntimePose head_pose;
  VrRuntimeEyeView eyes[2];
  uint32_t eye_count;
  float dynamic_resolution_scale;
  VrRuntimeFoveationPreset foveation_preset;
} VrRuntimeFrameData;

typedef struct VrRuntimeRenderTarget {
  VrRuntimeGraphicsBackend backend;
  VrRuntimeEye eye;
  void* color_texture;
  uint32_t color_array_index;
  uint32_t width;
  uint32_t height;
  VrRuntimeRect viewport;
  int64_t color_format;
} VrRuntimeRenderTarget;

/* GPU submission contract: for the D3D12 backend the callback MUST have
   submitted (ExecuteCommandLists on the bound queue) all work referencing the
   provided color targets before returning; CPU completion of that work is NOT
   required. The runtime releases the swapchain image immediately after the
   callback returns. */
typedef VrRuntimeResult (*VrRuntimeRenderCallback)(
  void* user_data,
  const VrRuntimeFrameData* frame,
  const VrRuntimeRenderTarget* targets,
  uint32_t target_count);

typedef void (*VrRuntimeStateCallback)(
  void* user_data,
  VrRuntimeState previous_state,
  VrRuntimeState next_state,
  int32_t detail_code);

typedef struct VrRuntimeD3D12Binding {
  uint32_t size;
  void* device;
  void* queue;
} VrRuntimeD3D12Binding;

typedef struct VrRuntimeDesc {
  /* MUST be set to sizeof(VrRuntimeDesc) by the caller; vr_runtime_create
     rejects a smaller value. Allows appending fields without an ABI break. */
  uint32_t size;
  const char* application_name;
  const char* runtime_profile_path;
  VrRuntimeGraphicsBackend preferred_graphics_backend;
  VrRuntimeStateCallback state_callback;
  void* state_callback_user_data;
  /* Optional borrowed game graphics objects. Valid only for D3D12. The runtime
     retains both COM objects and rejects a non-direct queue, a queue from a
     different device, or an adapter/feature-level mismatch with the compositor. */
  const VrRuntimeD3D12Binding* d3d12_binding;
} VrRuntimeDesc;

/* Threading contract: vr_runtime_get_state, vr_runtime_get_headset_state and
   vr_runtime_get_frame_data are safe to call from any thread -- they copy a
   snapshot published under an internal lock. vr_runtime_sample_head_pose is
   safe to call concurrently with vr_runtime_run_frame after start succeeds;
   it must not overlap stop or destroy. All lifecycle and frame functions
   (create/destroy/start/stop/poll_events/run_frame) must otherwise be
   externally serialized on a single thread per runtime handle. */
VR_RUNTIME_API VrRuntimeResult vr_runtime_create(
  const VrRuntimeDesc* desc,
  VrRuntime** runtime);

VR_RUNTIME_API void vr_runtime_destroy(VrRuntime* runtime);

VR_RUNTIME_API VrRuntimeResult vr_runtime_start(VrRuntime* runtime);

VR_RUNTIME_API VrRuntimeResult vr_runtime_poll_events(VrRuntime* runtime);

VR_RUNTIME_API VrRuntimeResult vr_runtime_run_frame(
  VrRuntime* runtime,
  VrRuntimeRenderCallback render_callback,
  void* render_user_data);

VR_RUNTIME_API VrRuntimeResult vr_runtime_stop(VrRuntime* runtime);

VR_RUNTIME_API VrRuntimeState vr_runtime_get_state(const VrRuntime* runtime);

VR_RUNTIME_API VrRuntimeResult vr_runtime_get_headset_state(
  const VrRuntime* runtime,
  VrRuntimeHeadsetState* state);

VR_RUNTIME_API VrRuntimeResult vr_runtime_get_frame_data(
  const VrRuntime* runtime,
  VrRuntimeFrameData* frame);

/* Samples the current tracked HMD pose independently of scene rendering.
   Returns SKIPPED when the session, current-time conversion extension, or
   tracked orientation is unavailable. */
VR_RUNTIME_API VrRuntimeResult vr_runtime_sample_head_pose(
  VrRuntime* runtime,
  VrRuntimePose* pose);

VR_RUNTIME_API const char* vr_runtime_result_name(VrRuntimeResult result);

#ifdef __cplusplus
}
#endif

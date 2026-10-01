#pragma once

#include "plugins/sdk/igame_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VRCLIENT_REDENGINE_GRAPHICS_SERVICE_VERSION 1u
#define VRCLIENT_REDENGINE_VIEW_SERVICE_VERSION 1u
#define VRCLIENT_REDENGINE_GAME_STATE_SERVICE_VERSION 1u

typedef enum VrRedengineRendererApi {
  VRCLIENT_REDENGINE_RENDERER_UNKNOWN = 0,
  VRCLIENT_REDENGINE_RENDERER_D3D12 = 1
} VrRedengineRendererApi;

typedef struct VrRedengineGraphicsSnapshot {
  uint32_t size;
  uint32_t version;
  VrRedengineRendererApi renderer;
  void* device;
  void* queue;
  void* swapchain;
  uint32_t width;
  uint32_t height;
  uint32_t format;
  uint32_t buffer_count;
  uint64_t present_count;
  uint64_t resize_count;
  VrAdapterBool ready;
  uint32_t candidate_view_width;
  uint32_t candidate_view_height;
  uint64_t candidate_view_resource_count;
  /* Borrowed while the graphics observer is loaded. This remains provisional
     until live RTV/use correlation proves it is the VRCAM final-color target. */
  void* candidate_color_resource;
  uint32_t candidate_color_format;
  uint64_t candidate_color_resource_count;
  uint64_t candidate_color_rtv_count;
  uint64_t candidate_unorm_resource_count;
  uint64_t candidate_unorm_rtv_resource_count;
  uint64_t candidate_unorm_rtv_count;
  uint64_t candidate_unorm_bound_resource_count;
  uint64_t candidate_unorm_bind_count;
  uint32_t candidate_unorm_tracked_count;
  uint64_t candidate_unorm_bind_counts[32];
  uint64_t candidate_unorm_cleared_resource_count;
  uint64_t candidate_unorm_clear_count;
  uint64_t candidate_unorm_clear_counts[32];
  uint64_t candidate_unorm_shader_read_resource_count;
  uint64_t candidate_unorm_shader_read_transition_count;
  uint64_t candidate_unorm_shader_read_transition_counts[32];
  char candidate_unorm_names[32][64];
  uint64_t candidate_unorm_copy_source_resource_count;
  uint64_t candidate_unorm_copy_source_count;
  uint64_t candidate_unorm_copy_source_counts[32];
  void* vrcam_color_resource;
  uint32_t vrcam_color_format;
  uint32_t vrcam_candidate_index;
  uint64_t vrcam_bind_count;
  uint64_t vrcam_shader_read_transition_count;
  VrAdapterBool vrcam_ready;
  uint32_t vrcam_source_state;
  uint64_t vrcam_capture_count;
  uint64_t vrcam_capture_fail_count;
  VrAdapterBool vrcam_capture_ready;
  uint64_t candidate_unorm_primary_queue_execute_counts[32];
  uint64_t candidate_unorm_other_direct_queue_execute_counts[32];
  uint64_t candidate_unorm_compute_queue_execute_counts[32];
  uint64_t candidate_unorm_copy_queue_execute_counts[32];
} VrRedengineGraphicsSnapshot;

typedef VrAdapterResult (VRCLIENT_ADAPTER_CALL *VrRedengineGraphicsQueryFn)(
    void* user_data,
    VrRedengineGraphicsSnapshot* snapshot);

typedef struct VrRedengineGraphicsService {
  uint32_t size;
  uint32_t version;
  VrRedengineGraphicsQueryFn query;
  void* user_data;
} VrRedengineGraphicsService;

/* Normalized VRClient coordinates: right-handed meters, -Z forward, +Y up.
   The REDengine bridge owns conversion from engine-native transforms. */
typedef struct VrRedengineViewSnapshot {
  uint32_t size;
  uint32_t version;
  uint64_t frame_index;
  uint64_t view_key;
  uint64_t main_view_key;
  uint64_t stereo_view_key;
  float position_m[3];
  float orientation_xyzw[4];
  float fov_angle_left;
  float fov_angle_right;
  float fov_angle_up;
  float fov_angle_down;
  VrAdapterBool main_view_ready;
  VrAdapterBool stereo_view_ready;
} VrRedengineViewSnapshot;

typedef VrAdapterResult (VRCLIENT_ADAPTER_CALL *VrRedengineViewQueryFn)(
    void* user_data,
    VrRedengineViewSnapshot* snapshot);

typedef struct VrRedengineViewService {
  uint32_t size;
  uint32_t version;
  VrRedengineViewQueryFn query;
  void* user_data;
} VrRedengineViewService;

typedef enum VrRedengineGameMode {
  VRCLIENT_REDENGINE_GAME_MODE_UNKNOWN = 0,
  VRCLIENT_REDENGINE_GAME_MODE_LOADING = 1,
  VRCLIENT_REDENGINE_GAME_MODE_GAMEPLAY = 2,
  VRCLIENT_REDENGINE_GAME_MODE_MENU = 3,
  VRCLIENT_REDENGINE_GAME_MODE_CUTSCENE = 4,
  VRCLIENT_REDENGINE_GAME_MODE_VEHICLE = 5,
  VRCLIENT_REDENGINE_GAME_MODE_BRAINDANCE = 6,
  VRCLIENT_REDENGINE_GAME_MODE_PHOTO = 7
} VrRedengineGameMode;

typedef struct VrRedengineGameStateSnapshot {
  uint32_t size;
  uint32_t version;
  uint64_t sample_index;
  VrRedengineGameMode mode;
  VrAdapterBool player_available;
  VrAdapterBool camera_control_allowed;
  VrAdapterBool ready;
} VrRedengineGameStateSnapshot;

typedef VrAdapterResult (VRCLIENT_ADAPTER_CALL *VrRedengineGameStateQueryFn)(
    void* user_data,
    VrRedengineGameStateSnapshot* snapshot);

typedef struct VrRedengineGameStateService {
  uint32_t size;
  uint32_t version;
  VrRedengineGameStateQueryFn query;
  void* user_data;
} VrRedengineGameStateService;

#ifdef __cplusplus
}
#endif

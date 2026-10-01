#pragma once

#include "plugins/sdk/igame_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RAGE services are intentionally a narrow host/adapter contract.  The game
   bridge owns discovery of engine objects; the adapter never scans or patches
   arbitrary process memory. */
#define VRCLIENT_RAGE_GRAPHICS_SERVICE_VERSION 1u
#define VRCLIENT_RAGE_VIEW_SERVICE_VERSION 1u
#define VRCLIENT_RAGE_GAME_STATE_SERVICE_VERSION 1u
#define VRCLIENT_RAGE_INPUT_SERVICE_VERSION 1u

typedef enum VrRageRendererApi {
  VRCLIENT_RAGE_RENDERER_UNKNOWN = 0,
  VRCLIENT_RAGE_RENDERER_D3D12 = 1,
  VRCLIENT_RAGE_RENDERER_VULKAN = 2
} VrRageRendererApi;

typedef struct VrRageGraphicsSnapshot {
  uint32_t size;
  uint32_t version;
  VrRageRendererApi renderer;
  void* device;
  void* queue;
  void* swapchain;
  void* left_eye_target;
  void* right_eye_target;
  uint32_t width;
  uint32_t height;
  uint32_t format;
  uint64_t present_count;
  uint64_t resize_count;
  VrAdapterBool ready;
} VrRageGraphicsSnapshot;

typedef VrAdapterResult (VRCLIENT_ADAPTER_CALL *VrRageGraphicsQueryFn)(
    void* user_data, VrRageGraphicsSnapshot* snapshot);
typedef VrAdapterResult (VRCLIENT_ADAPTER_CALL *VrRageStereoSubmitFn)(
    void* user_data, const VrRageGraphicsSnapshot* snapshot,
    const float* head_position_m, const float* head_orientation_xyzw);

typedef struct VrRageGraphicsService {
  uint32_t size;
  uint32_t version;
  VrRageGraphicsQueryFn query;
  VrRageStereoSubmitFn submit_stereo;
  void* user_data;
} VrRageGraphicsService;

typedef struct VrRageViewSnapshot {
  uint32_t size;
  uint32_t version;
  uint64_t frame_index;
  uint64_t camera_id;
  float position_m[3];
  float orientation_xyzw[4];
  float fov_angle_left;
  float fov_angle_right;
  float fov_angle_up;
  float fov_angle_down;
  VrAdapterBool ready;
} VrRageViewSnapshot;

typedef VrAdapterResult (VRCLIENT_ADAPTER_CALL *VrRageViewQueryFn)(
    void* user_data, VrRageViewSnapshot* snapshot);

typedef struct VrRageViewService {
  uint32_t size;
  uint32_t version;
  VrRageViewQueryFn query;
  void* user_data;
} VrRageViewService;

typedef enum VrRageGameMode {
  VRCLIENT_RAGE_GAME_MODE_UNKNOWN = 0,
  VRCLIENT_RAGE_GAME_MODE_LOADING = 1,
  VRCLIENT_RAGE_GAME_MODE_GAMEPLAY = 2,
  VRCLIENT_RAGE_GAME_MODE_MENU = 3,
  VRCLIENT_RAGE_GAME_MODE_CUTSCENE = 4,
  VRCLIENT_RAGE_GAME_MODE_VEHICLE = 5
} VrRageGameMode;

typedef struct VrRageGameStateSnapshot {
  uint32_t size;
  uint32_t version;
  uint64_t sample_index;
  VrRageGameMode mode;
  VrAdapterBool player_available;
  VrAdapterBool camera_control_allowed;
  VrAdapterBool ready;
} VrRageGameStateSnapshot;

typedef VrAdapterResult (VRCLIENT_ADAPTER_CALL *VrRageGameStateQueryFn)(
    void* user_data, VrRageGameStateSnapshot* snapshot);

typedef struct VrRageGameStateService {
  uint32_t size;
  uint32_t version;
  VrRageGameStateQueryFn query;
  void* user_data;
} VrRageGameStateService;

typedef enum VrRageControlId {
  VRCLIENT_RAGE_CONTROL_LOOK_X = 1,
  VRCLIENT_RAGE_CONTROL_MOVE_X = 2,
  VRCLIENT_RAGE_CONTROL_MOVE_Y = 3,
  VRCLIENT_RAGE_CONTROL_AIM = 4,
  VRCLIENT_RAGE_CONTROL_FIRE = 5,
  VRCLIENT_RAGE_CONTROL_INTERACT = 6,
  VRCLIENT_RAGE_CONTROL_MENU = 7,
  VRCLIENT_RAGE_CONTROL_HORSE_FORWARD = 8,
  VRCLIENT_RAGE_CONTROL_HORSE_REIN = 9
} VrRageControlId;

typedef VrAdapterResult (VRCLIENT_ADAPTER_CALL *VrRageInputApplyFn)(
    void* user_data, VrRageControlId control, float value,
    VrAdapterBool active);

typedef struct VrRageInputService {
  uint32_t size;
  uint32_t version;
  VrRageInputApplyFn apply;
  void* user_data;
} VrRageInputService;

#ifdef __cplusplus
}
#endif

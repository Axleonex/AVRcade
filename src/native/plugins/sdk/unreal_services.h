#pragma once

#include "plugins/sdk/igame_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VRCLIENT_UNREAL_GRAPHICS_SERVICE_VERSION 1u
#define VRCLIENT_UNREAL_CAMERA_SERVICE_VERSION 1u

typedef enum VrUnrealRendererApi {
  VRCLIENT_UNREAL_RENDERER_UNKNOWN = 0,
  VRCLIENT_UNREAL_RENDERER_D3D11 = 1,
  VRCLIENT_UNREAL_RENDERER_D3D12 = 2,
  VRCLIENT_UNREAL_RENDERER_VULKAN = 3
} VrUnrealRendererApi;

typedef struct VrUnrealGraphicsSnapshot {
  uint32_t size;
  uint32_t version;
  VrUnrealRendererApi renderer;
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
} VrUnrealGraphicsSnapshot;

typedef VrAdapterResult (VRCLIENT_ADAPTER_CALL *VrUnrealGraphicsQueryFn)(
    void* user_data,
    VrUnrealGraphicsSnapshot* snapshot);

typedef struct VrUnrealGraphicsService {
  uint32_t size;
  uint32_t version;
  VrUnrealGraphicsQueryFn query;
  void* user_data;
} VrUnrealGraphicsService;

/* UE engine-space camera data: centimeters, X forward, Y right, Z up, with
   pitch/yaw/roll in degrees. The provider must identify one active camera and
   advance sample_index whenever it publishes a fresh sample. */
typedef struct VrUnrealCameraSnapshot {
  uint32_t size;
  uint32_t version;
  uint64_t camera_id;
  uint64_t sample_index;
  int64_t sample_time_ns;
  double location_uu[3];
  double rotation_degrees[3];
  float vertical_fov_degrees;
  float aspect_ratio;
  float near_clip_uu;
  float far_clip_uu;
  VrAdapterBool active;
  VrAdapterBool ready;
} VrUnrealCameraSnapshot;

typedef VrAdapterResult (VRCLIENT_ADAPTER_CALL *VrUnrealCameraQueryFn)(
    void* user_data,
    VrUnrealCameraSnapshot* snapshot);

typedef struct VrUnrealCameraService {
  uint32_t size;
  uint32_t version;
  VrUnrealCameraQueryFn query;
  void* user_data;
} VrUnrealCameraService;

#ifdef __cplusplus
}
#endif

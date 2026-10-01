#pragma once

#include "plugins/sdk/adapter_context.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VRCLIENT_SHARED_SERVICE_VERSION 1u
#define VRCLIENT_INPUT_MAX_ACTIONS 32u
#define VRCLIENT_HUD_MAX_ANCHORS 32u

typedef enum VrClientInputActionId {
  VRCLIENT_INPUT_ACTION_UNKNOWN = 0,
  VRCLIENT_INPUT_ACTION_MOVE_X = 1,
  VRCLIENT_INPUT_ACTION_MOVE_Y = 2,
  VRCLIENT_INPUT_ACTION_TURN_X = 3,
  VRCLIENT_INPUT_ACTION_INTERACT = 4,
  VRCLIENT_INPUT_ACTION_MENU = 5,
  VRCLIENT_INPUT_ACTION_RECENTER = 6,
  VRCLIENT_INPUT_ACTION_COMFORT_SNAP_TURN = 7,
  VRCLIENT_INPUT_ACTION_COMFORT_VIGNETTE_TOGGLE = 8,
  VRCLIENT_INPUT_ACTION_AIM = 9,
  VRCLIENT_INPUT_ACTION_FIRE = 10,
  VRCLIENT_INPUT_ACTION_HORSE_FORWARD = 11,
  VRCLIENT_INPUT_ACTION_HORSE_REIN = 12
} VrClientInputActionId;

typedef enum VrClientHudAnchorMode {
  VRCLIENT_HUD_ANCHOR_HEAD_LOCKED = 1,
  VRCLIENT_HUD_ANCHOR_WORLD_LOCKED = 2
} VrClientHudAnchorMode;

typedef enum VrClientHudVisibilityRule {
  VRCLIENT_HUD_VISIBILITY_ALWAYS = 1,
  VRCLIENT_HUD_VISIBILITY_WHEN_GAMEPLAY = 2,
  VRCLIENT_HUD_VISIBILITY_WHEN_MENU = 3
} VrClientHudVisibilityRule;

typedef struct VrClientInputActionState {
  uint32_t size;
  uint32_t action_id;
  VrAdapterBool active;
  float value;
  int64_t timestamp_ns;
} VrClientInputActionState;

typedef VrAdapterResult (VRCLIENT_ADAPTER_CALL *VrClientInputQueryActionFn)(
    void* user_data,
    uint32_t action_id,
    VrClientInputActionState* out_state);

typedef struct VrClientInputService {
  uint32_t size;
  uint32_t version;
  VrClientInputQueryActionFn query_action;
  void* user_data;
  uint32_t action_count;
} VrClientInputService;

typedef struct VrClientComfortSettings {
  uint32_t size;
  uint32_t version;
  VrAdapterBool snap_turn_enabled;
  VrAdapterBool smooth_turn_enabled;
  VrAdapterBool vignette_enabled;
  VrAdapterBool seated_mode;
  float snap_turn_degrees;
  float smooth_turn_degrees_per_second;
  float vignette_strength;
  float world_scale;
  float height_offset_m;
} VrClientComfortSettings;

typedef VrAdapterResult (VRCLIENT_ADAPTER_CALL *VrClientComfortSnapshotFn)(
    void* user_data,
    VrClientComfortSettings* out_settings);

typedef struct VrClientComfortService {
  uint32_t size;
  uint32_t version;
  VrClientComfortSnapshotFn snapshot;
  void* user_data;
} VrClientComfortService;

typedef struct VrClientHudAnchor {
  uint32_t size;
  uint32_t version;
  const char* element_id;
  uint32_t anchor_mode;
  uint32_t visibility_rule;
  VrAdapterBool template_data;
  VrAdapterBool visible;
  float depth_m;
  float scale;
  float offset_x_m;
  float offset_y_m;
  float offset_z_m;
  float curvature;
  float transform[16];
} VrClientHudAnchor;

typedef uint32_t (VRCLIENT_ADAPTER_CALL *VrClientHudAnchorCountFn)(void* user_data);

typedef VrAdapterResult (VRCLIENT_ADAPTER_CALL *VrClientHudQueryAnchorFn)(
    void* user_data,
    const char* element_id,
    VrClientHudAnchor* out_anchor);

typedef VrAdapterResult (VRCLIENT_ADAPTER_CALL *VrClientHudQueryAnchorByIndexFn)(
    void* user_data,
    uint32_t index,
    VrClientHudAnchor* out_anchor);

typedef struct VrClientHudService {
  uint32_t size;
  uint32_t version;
  VrClientHudAnchorCountFn anchor_count;
  VrClientHudQueryAnchorFn query_anchor;
  VrClientHudQueryAnchorByIndexFn query_anchor_by_index;
  void* user_data;
} VrClientHudService;

#ifdef __cplusplus
}
#endif

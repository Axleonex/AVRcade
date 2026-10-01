#pragma once

#include "plugins/sdk/igame_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VRCLIENT_ADAPTER_SERVICE_DIAGNOSTICS 1u
#define VRCLIENT_ADAPTER_SERVICE_POSE_TIMING 2u
#define VRCLIENT_ADAPTER_SERVICE_CONFIG 3u
#define VRCLIENT_ADAPTER_SERVICE_INPUT 100u
#define VRCLIENT_ADAPTER_SERVICE_COMFORT 101u
#define VRCLIENT_ADAPTER_SERVICE_HUD 102u
#define VRCLIENT_ADAPTER_SERVICE_UNREAL_GRAPHICS 200u
#define VRCLIENT_ADAPTER_SERVICE_UNREAL_CAMERA 201u
#define VRCLIENT_ADAPTER_SERVICE_REDENGINE_GRAPHICS 300u
#define VRCLIENT_ADAPTER_SERVICE_REDENGINE_VIEW 301u
#define VRCLIENT_ADAPTER_SERVICE_REDENGINE_GAME_STATE 302u
#define VRCLIENT_ADAPTER_SERVICE_RAGE_GRAPHICS 400u
#define VRCLIENT_ADAPTER_SERVICE_RAGE_VIEW 401u
#define VRCLIENT_ADAPTER_SERVICE_RAGE_GAME_STATE 402u
#define VRCLIENT_ADAPTER_SERVICE_RAGE_INPUT 403u

static inline const VrAdapterService* vr_adapter_find_service(
    const VrAdapterHostServices* services,
    uint32_t service_id) {
  if (services == 0 || services->services == 0) {
    return 0;
  }
  for (uint32_t index = 0; index < services->service_count; ++index) {
    const VrAdapterService* service = &services->services[index];
    if (service->service_id == service_id) {
      return service;
    }
  }
  return 0;
}

#ifdef __cplusplus
}
#endif

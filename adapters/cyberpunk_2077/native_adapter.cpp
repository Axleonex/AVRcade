#include "plugins/sdk/adapter_context.h"
#include "plugins/sdk/adapter_metadata.h"
#include "plugins/sdk/redengine_services.h"

#include <stdint.h>
#include <string.h>

namespace {

constexpr const char* kAdapterId = "vrclient-cyberpunk-2077-adapter";
constexpr const char* kSupportedGame = "cyberpunk-2077";
constexpr const char* kSupportedBuild = "steam-1091500-build-20383525";

struct CyberpunkAdapterState {
  uint64_t frame_count = 0;
  bool initialized = false;
};

CyberpunkAdapterState g_state;

const char* kSupportedGames[] = {kSupportedGame};

const VrAdapterBuildId kSupportedBuilds[] = {{
    sizeof(VrAdapterBuildId),
    kSupportedGame,
    kSupportedBuild,
}};

const VrAdapterMetadata kMetadata = {
    sizeof(VrAdapterMetadata),
    VRCLIENT_ADAPTER_ABI_VERSION,
    kAdapterId,
    "0.1.0",
    "VRClient Cyberpunk 2077 Adapter",
    kSupportedGames,
    1,
    kSupportedBuilds,
    1,
    {sizeof(VrAdapterApiVersion), 1, 0, 0},
    {sizeof(VrAdapterApiVersion), 1, 0, 0},
    VR_ADAPTER_CAP_FRAME_TICK | VR_ADAPTER_CAP_CONFIG_RELOAD |
        VR_ADAPTER_CAP_FAST_RESTART,
    0,
};

bool sameString(const char* left, const char* right) {
  return left != nullptr && right != nullptr && strcmp(left, right) == 0;
}

void logEvent(
    const VrAdapterContext* context,
    const char* event_name,
    const char* key,
    const char* value) {
  if (context == nullptr || context->host_services == nullptr) {
    return;
  }
  const VrAdapterService* entry = vr_adapter_find_service(
      context->host_services,
      VRCLIENT_ADAPTER_SERVICE_DIAGNOSTICS);
  if (entry == nullptr || entry->service == nullptr) {
    return;
  }
  const auto* diagnostics =
      static_cast<const VrAdapterDiagnosticsService*>(entry->service);
  if (diagnostics->size >= sizeof(VrAdapterDiagnosticsService) &&
      diagnostics->log_event != nullptr) {
    diagnostics->log_event(
        diagnostics->user_data,
        event_name,
        key,
        value);
  }
}

bool hasRedengineService(
    const VrAdapterContext* context,
    uint32_t service_id,
    uint32_t service_version) {
  if (context == nullptr || context->host_services == nullptr) {
    return false;
  }
  const VrAdapterService* entry =
      vr_adapter_find_service(context->host_services, service_id);
  return entry != nullptr && entry->service != nullptr &&
      entry->service_version == service_version;
}

VrAdapterResult VRCLIENT_ADAPTER_CALL validate(
    VrGameAdapter* adapter,
    const VrAdapterContext* context) {
  if (adapter == nullptr || context == nullptr ||
      context->target.game_id == nullptr ||
      context->target.build_id == nullptr) {
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  }
  if (!sameString(context->target.game_id, kSupportedGame) ||
      !sameString(context->target.build_id, kSupportedBuild)) {
    logEvent(
        context,
        "cyberpunk_adapter_build_mismatch",
        "required_gate",
        kSupportedBuild);
    return VR_ADAPTER_ERROR_UNSUPPORTED_TARGET;
  }
  if (!hasRedengineService(
          context,
          VRCLIENT_ADAPTER_SERVICE_REDENGINE_GRAPHICS,
          VRCLIENT_REDENGINE_GRAPHICS_SERVICE_VERSION) ||
      !hasRedengineService(
          context,
          VRCLIENT_ADAPTER_SERVICE_REDENGINE_VIEW,
          VRCLIENT_REDENGINE_VIEW_SERVICE_VERSION) ||
      !hasRedengineService(
          context,
          VRCLIENT_ADAPTER_SERVICE_REDENGINE_GAME_STATE,
          VRCLIENT_REDENGINE_GAME_STATE_SERVICE_VERSION)) {
    logEvent(
        context,
        "cyberpunk_adapter_bridge_unavailable",
        "required_gate",
        "redengine_graphics_view_and_game_state_services");
    return VR_ADAPTER_ERROR_UNSUPPORTED_API;
  }
  return VR_ADAPTER_OK;
}

VrAdapterResult VRCLIENT_ADAPTER_CALL init(
    VrGameAdapter* adapter,
    const VrAdapterContext* context) {
  const VrAdapterResult result = validate(adapter, context);
  if (result != VR_ADAPTER_OK) {
    return result;
  }
  g_state.frame_count = 0;
  g_state.initialized = true;
  logEvent(context, "cyberpunk_adapter_init", "adapter_id", kAdapterId);
  return VR_ADAPTER_OK;
}

VrAdapterResult VRCLIENT_ADAPTER_CALL tick(
    VrGameAdapter* adapter,
    const VrAdapterContext*,
    const VrAdapterFrameInfo* frame) {
  if (adapter == nullptr || frame == nullptr || !g_state.initialized) {
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  }
  g_state.frame_count = frame->frame_index;
  return VR_ADAPTER_OK;
}

VrAdapterResult VRCLIENT_ADAPTER_CALL simple(
    VrGameAdapter* adapter,
    const VrAdapterContext*) {
  return adapter == nullptr ? VR_ADAPTER_ERROR_INVALID_ARGUMENT : VR_ADAPTER_OK;
}

VrAdapterResult VRCLIENT_ADAPTER_CALL shutdown(
    VrGameAdapter* adapter,
    const VrAdapterContext* context) {
  if (adapter == nullptr) {
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  }
  logEvent(context, "cyberpunk_adapter_shutdown", "adapter_id", kAdapterId);
  g_state.initialized = false;
  return VR_ADAPTER_OK;
}

VrGameAdapter g_adapter = {
    sizeof(VrGameAdapter),
    &g_state,
    validate,
    init,
    tick,
    simple,
    simple,
    simple,
    shutdown,
};

}  // namespace

extern "C" VRCLIENT_ADAPTER_API uint32_t VRCLIENT_ADAPTER_CALL
vrclient_get_adapter_abi(void) {
  return VRCLIENT_ADAPTER_ABI_VERSION;
}

extern "C" VRCLIENT_ADAPTER_API const VrAdapterMetadata* VRCLIENT_ADAPTER_CALL
vrclient_get_adapter_metadata(void) {
  return &kMetadata;
}

extern "C" VRCLIENT_ADAPTER_API VrAdapterResult VRCLIENT_ADAPTER_CALL
vrclient_create_adapter(
    const VrAdapterHostServices* services,
    VrGameAdapter** adapter) {
  if (services == nullptr || adapter == nullptr ||
      services->size < sizeof(VrAdapterHostServices)) {
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  }
  *adapter = &g_adapter;
  return VR_ADAPTER_OK;
}

extern "C" VRCLIENT_ADAPTER_API void VRCLIENT_ADAPTER_CALL
vrclient_destroy_adapter(VrGameAdapter* adapter) {
  if (adapter == &g_adapter) {
    g_state.initialized = false;
  }
}

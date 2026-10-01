#include "plugins/sdk/adapter_context.h"
#include "plugins/sdk/adapter_metadata.h"
#include "plugins/sdk/shared_services.h"

#include <stdint.h>

namespace {

struct TemplateAdapterState {
  uint64_t frame_count = 0;
  int64_t last_predicted_display_time_ns = 0;
  bool initialized = false;
  bool input_service_seen = false;
  bool comfort_service_seen = false;
  bool hud_service_seen = false;
};

TemplateAdapterState g_state;

const char* kSupportedGames[] = {
    "vrclient-smoke-host",
};

const VrAdapterBuildId kSupportedBuilds[] = {
    {
        sizeof(VrAdapterBuildId),
        "vrclient-smoke-host",
        "smoke-2026-06-11",
    },
};

const VrAdapterMetadata kMetadata = {
    sizeof(VrAdapterMetadata),
    VRCLIENT_ADAPTER_ABI_VERSION,
    "vrclient-template-adapter",
    "0.1.0",
    "VRClient Template Adapter",
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

void logAdapterEvent(
    const VrAdapterContext* context,
    const char* event_name,
    const char* key,
    const char* value) {
  if (context == nullptr || context->host_services == nullptr) {
    return;
  }
  const VrAdapterService* service = vr_adapter_find_service(
      context->host_services,
      VRCLIENT_ADAPTER_SERVICE_DIAGNOSTICS);
  if (service == nullptr || service->service == nullptr) {
    return;
  }
  const auto* diagnostics =
      static_cast<const VrAdapterDiagnosticsService*>(service->service);
  if (diagnostics->log_event != nullptr) {
    diagnostics->log_event(
        diagnostics->user_data,
        event_name,
        key,
        value);
  }
}

void logLifecycle(const VrAdapterContext* context, const char* event_name) {
  logAdapterEvent(context, event_name, "adapter_id", kMetadata.adapter_id);
}

void readSharedServices(const VrAdapterContext* context) {
  if (context == nullptr || context->host_services == nullptr) {
    return;
  }

  if (const VrAdapterService* service = vr_adapter_find_service(
          context->host_services,
          VRCLIENT_ADAPTER_SERVICE_INPUT)) {
    const auto* input = static_cast<const VrClientInputService*>(service->service);
    if (input != nullptr && input->size >= sizeof(VrClientInputService) &&
        input->version == VRCLIENT_SHARED_SERVICE_VERSION &&
        input->query_action != nullptr) {
      VrClientInputActionState state{};
      state.size = sizeof(VrClientInputActionState);
      if (input->query_action(
              input->user_data,
              VRCLIENT_INPUT_ACTION_INTERACT,
              &state) == VR_ADAPTER_OK) {
        g_state.input_service_seen = true;
        logAdapterEvent(
            context,
            "template_adapter_input_service",
            "action",
            "interact");
      }
    }
  }

  if (const VrAdapterService* service = vr_adapter_find_service(
          context->host_services,
          VRCLIENT_ADAPTER_SERVICE_COMFORT)) {
    const auto* comfort = static_cast<const VrClientComfortService*>(service->service);
    if (comfort != nullptr && comfort->size >= sizeof(VrClientComfortService) &&
        comfort->version == VRCLIENT_SHARED_SERVICE_VERSION &&
        comfort->snapshot != nullptr) {
      VrClientComfortSettings settings{};
      settings.size = sizeof(VrClientComfortSettings);
      if (comfort->snapshot(comfort->user_data, &settings) == VR_ADAPTER_OK) {
        g_state.comfort_service_seen = true;
        logAdapterEvent(
            context,
            "template_adapter_comfort_service",
            "snap_turn",
            settings.snap_turn_enabled ? "enabled" : "disabled");
      }
    }
  }

  if (const VrAdapterService* service = vr_adapter_find_service(
          context->host_services,
          VRCLIENT_ADAPTER_SERVICE_HUD)) {
    const auto* hud = static_cast<const VrClientHudService*>(service->service);
    if (hud != nullptr && hud->size >= sizeof(VrClientHudService) &&
        hud->version == VRCLIENT_SHARED_SERVICE_VERSION &&
        hud->query_anchor_by_index != nullptr) {
      VrClientHudAnchor anchor{};
      anchor.size = sizeof(VrClientHudAnchor);
      if (hud->query_anchor_by_index(hud->user_data, 0, &anchor) == VR_ADAPTER_OK) {
        g_state.hud_service_seen = true;
        logAdapterEvent(
            context,
            "template_adapter_hud_service",
            "anchor",
            anchor.element_id == nullptr ? "unknown" : anchor.element_id);
      }
    }
  }
}

VrAdapterResult VRCLIENT_ADAPTER_CALL validate(
    VrGameAdapter* adapter,
    const VrAdapterContext* context) {
  if (adapter == nullptr || context == nullptr ||
      context->target.game_id == nullptr ||
      context->target.build_id == nullptr) {
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  }
  return VR_ADAPTER_OK;
}

VrAdapterResult VRCLIENT_ADAPTER_CALL init(
    VrGameAdapter* adapter,
    const VrAdapterContext* context) {
  if (adapter == nullptr) {
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  }
  g_state.initialized = true;
  g_state.frame_count = 0;
  g_state.last_predicted_display_time_ns = 0;
  logLifecycle(context, "template_adapter_init");
  readSharedServices(context);
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
  g_state.last_predicted_display_time_ns = frame->predicted_display_time_ns;
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
  logLifecycle(context, "template_adapter_shutdown");
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

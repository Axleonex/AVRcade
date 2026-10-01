#include "plugins/sdk/adapter_context.h"
#include "plugins/sdk/adapter_metadata.h"
#include "plugins/sdk/rage_services.h"
#include "plugins/sdk/shared_services.h"

#include <stdint.h>
#include <string.h>

namespace {

constexpr const char* kAdapterId = "vrclient-rdr2-adapter";
constexpr const char* kSupportedGame = "red-dead-redemption-2";
constexpr const char* kSupportedBuild = "steam-1174180-build-13773296";

struct Rdr2AdapterState {
  uint64_t frame_count = 0;
  uint64_t last_camera_id = 0;
  bool initialized = false;
};

Rdr2AdapterState g_state;
const char* kSupportedGames[] = {kSupportedGame};
const VrAdapterBuildId kSupportedBuilds[] = {{
    sizeof(VrAdapterBuildId), kSupportedGame, kSupportedBuild}};

const VrAdapterMetadata kMetadata = {
    sizeof(VrAdapterMetadata),
    VRCLIENT_ADAPTER_ABI_VERSION,
    kAdapterId,
    "0.1.0",
    "VRClient Red Dead Redemption 2 Adapter",
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

void logEvent(const VrAdapterContext* context, const char* event_name,
              const char* key, const char* value) {
  if (context == nullptr || context->host_services == nullptr) return;
  const VrAdapterService* entry = vr_adapter_find_service(
      context->host_services, VRCLIENT_ADAPTER_SERVICE_DIAGNOSTICS);
  if (entry == nullptr || entry->service == nullptr) return;
  const auto* diagnostics =
      static_cast<const VrAdapterDiagnosticsService*>(entry->service);
  if (diagnostics->size >= sizeof(VrAdapterDiagnosticsService) &&
      diagnostics->log_event != nullptr) {
    diagnostics->log_event(diagnostics->user_data, event_name, key, value);
  }
}

const VrAdapterService* findService(const VrAdapterContext* context,
                                    uint32_t service_id,
                                    uint32_t service_version) {
  if (context == nullptr || context->host_services == nullptr) return nullptr;
  const VrAdapterService* entry =
      vr_adapter_find_service(context->host_services, service_id);
  return entry != nullptr && entry->service != nullptr &&
                 entry->service_version == service_version
             ? entry
             : nullptr;
}

void forwardInput(const VrAdapterContext* context) {
  const VrAdapterService* inputEntry =
      findService(context, VRCLIENT_ADAPTER_SERVICE_RAGE_INPUT,
                  VRCLIENT_RAGE_INPUT_SERVICE_VERSION);
  const VrAdapterService* sharedEntry =
      vr_adapter_find_service(context->host_services,
                              VRCLIENT_ADAPTER_SERVICE_INPUT);
  if (inputEntry == nullptr || sharedEntry == nullptr ||
      inputEntry->service == nullptr || sharedEntry->service == nullptr)
    return;
  const auto* output =
      static_cast<const VrRageInputService*>(inputEntry->service);
  const auto* input =
      static_cast<const VrClientInputService*>(sharedEntry->service);
  if (output->size < sizeof(VrRageInputService) ||
      output->version != VRCLIENT_RAGE_INPUT_SERVICE_VERSION ||
      output->apply == nullptr || input->size < sizeof(VrClientInputService) ||
      input->query_action == nullptr)
    return;
  constexpr struct {
    uint32_t source;
    VrRageControlId target;
  } kMappings[] = {
      {VRCLIENT_INPUT_ACTION_TURN_X, VRCLIENT_RAGE_CONTROL_LOOK_X},
      {VRCLIENT_INPUT_ACTION_MOVE_X, VRCLIENT_RAGE_CONTROL_MOVE_X},
      {VRCLIENT_INPUT_ACTION_MOVE_Y, VRCLIENT_RAGE_CONTROL_MOVE_Y},
      {VRCLIENT_INPUT_ACTION_AIM, VRCLIENT_RAGE_CONTROL_AIM},
      {VRCLIENT_INPUT_ACTION_FIRE, VRCLIENT_RAGE_CONTROL_FIRE},
      {VRCLIENT_INPUT_ACTION_INTERACT, VRCLIENT_RAGE_CONTROL_INTERACT},
      {VRCLIENT_INPUT_ACTION_MENU, VRCLIENT_RAGE_CONTROL_MENU},
      {VRCLIENT_INPUT_ACTION_HORSE_FORWARD, VRCLIENT_RAGE_CONTROL_HORSE_FORWARD},
      {VRCLIENT_INPUT_ACTION_HORSE_REIN, VRCLIENT_RAGE_CONTROL_HORSE_REIN},
  };
  for (const auto& mapping : kMappings) {
    VrClientInputActionState state{};
    state.size = sizeof(state);
    if (input->query_action(input->user_data, mapping.source, &state) ==
        VR_ADAPTER_OK) {
      output->apply(output->user_data, mapping.target, state.value,
                    state.active);
    }
  }
}

VrAdapterResult VRCLIENT_ADAPTER_CALL validate(
    VrGameAdapter* adapter, const VrAdapterContext* context) {
  if (adapter == nullptr || context == nullptr ||
      context->target.game_id == nullptr || context->target.build_id == nullptr)
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  if (!sameString(context->target.game_id, kSupportedGame) ||
      !sameString(context->target.build_id, kSupportedBuild)) {
    logEvent(context, "rdr2_adapter_build_mismatch", "required_build",
             kSupportedBuild);
    return VR_ADAPTER_ERROR_UNSUPPORTED_TARGET;
  }
  if (findService(context, VRCLIENT_ADAPTER_SERVICE_RAGE_GRAPHICS,
                  VRCLIENT_RAGE_GRAPHICS_SERVICE_VERSION) == nullptr ||
      findService(context, VRCLIENT_ADAPTER_SERVICE_RAGE_VIEW,
                  VRCLIENT_RAGE_VIEW_SERVICE_VERSION) == nullptr ||
      findService(context, VRCLIENT_ADAPTER_SERVICE_RAGE_GAME_STATE,
                  VRCLIENT_RAGE_GAME_STATE_SERVICE_VERSION) == nullptr ||
      findService(context, VRCLIENT_ADAPTER_SERVICE_RAGE_INPUT,
                  VRCLIENT_RAGE_INPUT_SERVICE_VERSION) == nullptr) {
    logEvent(context, "rdr2_adapter_bridge_unavailable", "required_services",
             "rage_graphics_view_game_state_and_input");
    return VR_ADAPTER_ERROR_UNSUPPORTED_API;
  }
  return VR_ADAPTER_OK;
}

VrAdapterResult VRCLIENT_ADAPTER_CALL init(
    VrGameAdapter* adapter, const VrAdapterContext* context) {
  const VrAdapterResult result = validate(adapter, context);
  if (result != VR_ADAPTER_OK) return result;
  g_state = {};
  g_state.initialized = true;
  logEvent(context, "rdr2_adapter_init", "adapter_id", kAdapterId);
  return VR_ADAPTER_OK;
}

VrAdapterResult VRCLIENT_ADAPTER_CALL tick(
    VrGameAdapter* adapter, const VrAdapterContext* context,
    const VrAdapterFrameInfo* frame) {
  if (adapter == nullptr || context == nullptr || frame == nullptr ||
      !g_state.initialized)
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;

  const VrAdapterService* graphicsEntry = findService(
      context, VRCLIENT_ADAPTER_SERVICE_RAGE_GRAPHICS,
      VRCLIENT_RAGE_GRAPHICS_SERVICE_VERSION);
  const VrAdapterService* viewEntry = findService(
      context, VRCLIENT_ADAPTER_SERVICE_RAGE_VIEW,
      VRCLIENT_RAGE_VIEW_SERVICE_VERSION);
  const VrAdapterService* stateEntry = findService(
      context, VRCLIENT_ADAPTER_SERVICE_RAGE_GAME_STATE,
      VRCLIENT_RAGE_GAME_STATE_SERVICE_VERSION);
  if (graphicsEntry == nullptr || viewEntry == nullptr || stateEntry == nullptr ||
      graphicsEntry->service == nullptr || viewEntry->service == nullptr ||
      stateEntry->service == nullptr) {
    return VR_ADAPTER_ERROR_UNSUPPORTED_API;
  }
  const auto* graphics =
      static_cast<const VrRageGraphicsService*>(graphicsEntry->service);
  const auto* view = static_cast<const VrRageViewService*>(viewEntry->service);
  const auto* game_state =
      static_cast<const VrRageGameStateService*>(stateEntry->service);
  if (graphics->size < sizeof(VrRageGraphicsService) ||
      view->size < sizeof(VrRageViewService) ||
      game_state->size < sizeof(VrRageGameStateService) ||
      graphics->query == nullptr || graphics->submit_stereo == nullptr ||
      view->query == nullptr || game_state->query == nullptr)
    return VR_ADAPTER_ERROR_UNSUPPORTED_API;

  VrRageGraphicsSnapshot graphics_snapshot{};
  graphics_snapshot.size = sizeof(graphics_snapshot);
  VrRageViewSnapshot view_snapshot{};
  view_snapshot.size = sizeof(view_snapshot);
  VrRageGameStateSnapshot state_snapshot{};
  state_snapshot.size = sizeof(state_snapshot);
  if (graphics->query(graphics->user_data, &graphics_snapshot) != VR_ADAPTER_OK ||
      view->query(view->user_data, &view_snapshot) != VR_ADAPTER_OK ||
      game_state->query(game_state->user_data, &state_snapshot) != VR_ADAPTER_OK)
    return VR_ADAPTER_ERROR_RECOVERABLE;
  if (!graphics_snapshot.ready || !view_snapshot.ready || !state_snapshot.ready ||
      !state_snapshot.camera_control_allowed)
    return VR_ADAPTER_ERROR_RECOVERABLE;

  const VrAdapterResult submitted = graphics->submit_stereo(
      graphics->user_data, &graphics_snapshot, view_snapshot.position_m,
      view_snapshot.orientation_xyzw);
  if (submitted != VR_ADAPTER_OK) return submitted;
  forwardInput(context);
  g_state.frame_count = frame->frame_index;
  g_state.last_camera_id = view_snapshot.camera_id;
  return VR_ADAPTER_OK;
}

VrAdapterResult VRCLIENT_ADAPTER_CALL simple(
    VrGameAdapter* adapter, const VrAdapterContext*) {
  return adapter == nullptr ? VR_ADAPTER_ERROR_INVALID_ARGUMENT : VR_ADAPTER_OK;
}

VrAdapterResult VRCLIENT_ADAPTER_CALL shutdown(
    VrGameAdapter* adapter, const VrAdapterContext* context) {
  if (adapter == nullptr) return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  logEvent(context, "rdr2_adapter_shutdown", "adapter_id", kAdapterId);
  g_state = {};
  return VR_ADAPTER_OK;
}

VrGameAdapter g_adapter = {sizeof(VrGameAdapter), &g_state, validate, init, tick,
                           simple, simple, simple, shutdown};

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
vrclient_create_adapter(const VrAdapterHostServices* services,
                        VrGameAdapter** adapter) {
  if (services == nullptr || adapter == nullptr ||
      services->size < sizeof(VrAdapterHostServices))
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  *adapter = &g_adapter;
  return VR_ADAPTER_OK;
}

extern "C" VRCLIENT_ADAPTER_API void VRCLIENT_ADAPTER_CALL
vrclient_destroy_adapter(VrGameAdapter* adapter) {
  if (adapter == &g_adapter) g_state = {};
}

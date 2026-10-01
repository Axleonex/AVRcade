#include "plugins/sdk/adapter_context.h"
#include "plugins/sdk/adapter_metadata.h"
#include "plugins/sdk/unreal_services.h"

#include <cstdint>
#include <cmath>
#include <cstring>

namespace {

constexpr const char* kAdapterId = "vrclient-meccha-native-adapter";
constexpr const char* kSupportedGame = "meccha-chameleon";
constexpr const char* kSupportedBuild = "steam-4704690-build-24517175";
constexpr const char* kShippingExecutable = "PenguinHotel-Win64-Shipping.exe";

struct AdapterState {
  bool initialized = false;
  bool graphics_ready_logged = false;
  bool camera_pending_logged = false;
  bool camera_ready_logged = false;
  std::uint64_t frame_count = 0;
};

AdapterState g_state;
const char* kSupportedGames[] = {kSupportedGame};
const VrAdapterBuildId kSupportedBuilds[] = {
    {sizeof(VrAdapterBuildId), kSupportedGame, kSupportedBuild},
};
const VrAdapterMetadata kMetadata = {
    sizeof(VrAdapterMetadata),
    VRCLIENT_ADAPTER_ABI_VERSION,
    kAdapterId,
    "0.1.0",
    "VRClient MECCHA CHAMELEON Native Adapter",
    kSupportedGames,
    1,
    kSupportedBuilds,
    1,
    {sizeof(VrAdapterApiVersion), 1, 0, 0},
    {sizeof(VrAdapterApiVersion), 1, 0, 0},
    VR_ADAPTER_CAP_FRAME_TICK | VR_ADAPTER_CAP_FAST_RESTART,
    0,
};

bool sameString(const char* left, const char* right) {
  return left != nullptr && right != nullptr && std::strcmp(left, right) == 0;
}

const char* fileName(const char* path) {
  if (path == nullptr) {
    return nullptr;
  }
  const char* slash = std::strrchr(path, '/');
  const char* backslash = std::strrchr(path, '\\');
  const char* separator =
      slash == nullptr ? backslash
                       : (backslash == nullptr || slash > backslash ? slash : backslash);
  return separator == nullptr ? path : separator + 1;
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

bool queryGraphics(
    const VrAdapterContext* context,
    VrUnrealGraphicsSnapshot* snapshot) {
  if (context == nullptr || context->host_services == nullptr ||
      snapshot == nullptr) {
    return false;
  }
  const VrAdapterService* entry = vr_adapter_find_service(
      context->host_services,
      VRCLIENT_ADAPTER_SERVICE_UNREAL_GRAPHICS);
  if (entry == nullptr || entry->service == nullptr) {
    return false;
  }
  const auto* service = static_cast<const VrUnrealGraphicsService*>(entry->service);
  if (service->size < sizeof(VrUnrealGraphicsService) ||
      service->version != VRCLIENT_UNREAL_GRAPHICS_SERVICE_VERSION ||
      service->query == nullptr) {
    return false;
  }
  snapshot->size = sizeof(VrUnrealGraphicsSnapshot);
  snapshot->version = VRCLIENT_UNREAL_GRAPHICS_SERVICE_VERSION;
  return service->query(service->user_data, snapshot) == VR_ADAPTER_OK;
}

bool queryCamera(
    const VrAdapterContext* context,
    VrUnrealCameraSnapshot* snapshot) {
  if (context == nullptr || context->host_services == nullptr ||
      snapshot == nullptr) {
    return false;
  }
  const VrAdapterService* entry = vr_adapter_find_service(
      context->host_services,
      VRCLIENT_ADAPTER_SERVICE_UNREAL_CAMERA);
  if (entry == nullptr || entry->service == nullptr) {
    return false;
  }
  const auto* service = static_cast<const VrUnrealCameraService*>(entry->service);
  if (service->size < sizeof(VrUnrealCameraService) ||
      service->version != VRCLIENT_UNREAL_CAMERA_SERVICE_VERSION ||
      service->query == nullptr) {
    return false;
  }
  snapshot->size = sizeof(VrUnrealCameraSnapshot);
  snapshot->version = VRCLIENT_UNREAL_CAMERA_SERVICE_VERSION;
  return service->query(service->user_data, snapshot) == VR_ADAPTER_OK;
}

bool validCameraSnapshot(
    const VrUnrealCameraSnapshot& camera,
    std::int64_t predicted_display_time_ns) {
  constexpr std::int64_t kMaximumCameraAgeNs = 100'000'000;
  const auto finite3 = [](const double value[3]) {
    return std::isfinite(value[0]) && std::isfinite(value[1]) &&
        std::isfinite(value[2]);
  };
  return camera.size >= sizeof(VrUnrealCameraSnapshot) &&
      camera.version == VRCLIENT_UNREAL_CAMERA_SERVICE_VERSION &&
      camera.ready != 0 && camera.active != 0 && camera.camera_id != 0 &&
      camera.sample_time_ns > 0 && predicted_display_time_ns > 0 &&
      camera.sample_time_ns <= predicted_display_time_ns &&
      predicted_display_time_ns - camera.sample_time_ns <=
          kMaximumCameraAgeNs &&
      finite3(camera.location_uu) && finite3(camera.rotation_degrees) &&
      std::isfinite(camera.vertical_fov_degrees) &&
      camera.vertical_fov_degrees > 1.0F &&
      camera.vertical_fov_degrees < 179.0F &&
      std::isfinite(camera.aspect_ratio) && camera.aspect_ratio > 0.0F &&
      std::isfinite(camera.near_clip_uu) && camera.near_clip_uu > 0.0F &&
      std::isfinite(camera.far_clip_uu) &&
      camera.far_clip_uu > camera.near_clip_uu;
}

VrAdapterResult VRCLIENT_ADAPTER_CALL validate(
    VrGameAdapter* adapter,
    const VrAdapterContext* context) {
  if (adapter == nullptr || context == nullptr ||
      context->target.game_id == nullptr ||
      context->target.build_id == nullptr ||
      context->target.executable_path == nullptr) {
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  }
  if (!sameString(context->target.game_id, kSupportedGame) ||
      !sameString(context->target.build_id, kSupportedBuild) ||
      !sameString(fileName(context->target.executable_path), kShippingExecutable)) {
    return VR_ADAPTER_ERROR_UNSUPPORTED_TARGET;
  }
  return VR_ADAPTER_OK;
}

VrAdapterResult VRCLIENT_ADAPTER_CALL init(
    VrGameAdapter* adapter,
    const VrAdapterContext* context) {
  const VrAdapterResult validation = validate(adapter, context);
  if (validation != VR_ADAPTER_OK) {
    return validation;
  }
  g_state = {};
  g_state.initialized = true;
  logEvent(context, "meccha_native_adapter_init", "adapter_id", kAdapterId);

  VrUnrealGraphicsSnapshot graphics{};
  if (!queryGraphics(context, &graphics) || !graphics.ready) {
    logEvent(
        context,
        "meccha_native_graphics_pending",
        "reason",
        "awaiting_d3d12_device_queue_swapchain");
  }
  return VR_ADAPTER_OK;
}

VrAdapterResult VRCLIENT_ADAPTER_CALL tick(
    VrGameAdapter* adapter,
    const VrAdapterContext* context,
    const VrAdapterFrameInfo* frame) {
  if (adapter == nullptr || frame == nullptr || !g_state.initialized) {
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  }
  g_state.frame_count = frame->frame_index;

  VrUnrealGraphicsSnapshot graphics{};
  if (!queryGraphics(context, &graphics) || !graphics.ready) {
    return VR_ADAPTER_SKIPPED;
  }
  if (graphics.renderer != VRCLIENT_UNREAL_RENDERER_D3D12 ||
      graphics.device == nullptr || graphics.queue == nullptr ||
      graphics.swapchain == nullptr) {
    return VR_ADAPTER_ERROR_UNSUPPORTED_API;
  }
  if (!g_state.graphics_ready_logged) {
    logEvent(
        context,
        "meccha_native_graphics_ready",
        "renderer",
        "d3d12");
    g_state.graphics_ready_logged = true;
  }
  VrUnrealCameraSnapshot camera{};
  if (!queryCamera(context, &camera) ||
      !validCameraSnapshot(camera, frame->predicted_display_time_ns)) {
    if (!g_state.camera_pending_logged) {
      logEvent(
          context,
          "meccha_native_camera_pending",
          "reason",
          "awaiting_unique_fresh_active_camera");
      g_state.camera_pending_logged = true;
    }
    return VR_ADAPTER_SKIPPED;
  }
  if (!g_state.camera_ready_logged) {
    logEvent(
        context,
        "meccha_native_camera_ready",
        "source",
        "unreal_camera_service");
    g_state.camera_ready_logged = true;
  }
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
  logEvent(context, "meccha_native_adapter_shutdown", "adapter_id", kAdapterId);
  g_state = {};
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
    g_state = {};
  }
}

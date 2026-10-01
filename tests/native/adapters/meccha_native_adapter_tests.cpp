#include "plugins/host/plugin_host.h"
#include "plugins/host/plugin_library.h"
#include "plugins/sdk/unreal_services.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct TestState {
  bool graphics_ready = false;
  bool camera_ready = false;
  int init_events = 0;
  int pending_events = 0;
  int ready_events = 0;
  int camera_pending_events = 0;
  int camera_ready_events = 0;
  int shutdown_events = 0;
};

void VRCLIENT_ADAPTER_CALL logDiagnostic(
    void* user_data,
    const char* event_name,
    const char*,
    const char*) {
  auto* state = static_cast<TestState*>(user_data);
  if (state == nullptr || event_name == nullptr) {
    return;
  }
  const std::string event = event_name;
  if (event == "meccha_native_adapter_init") ++state->init_events;
  if (event == "meccha_native_graphics_pending") ++state->pending_events;
  if (event == "meccha_native_graphics_ready") ++state->ready_events;
  if (event == "meccha_native_camera_pending") ++state->camera_pending_events;
  if (event == "meccha_native_camera_ready") ++state->camera_ready_events;
  if (event == "meccha_native_adapter_shutdown") ++state->shutdown_events;
}

VrAdapterResult VRCLIENT_ADAPTER_CALL queryGraphics(
    void* user_data,
    VrUnrealGraphicsSnapshot* snapshot) {
  auto* state = static_cast<TestState*>(user_data);
  if (state == nullptr || snapshot == nullptr ||
      snapshot->size < sizeof(VrUnrealGraphicsSnapshot)) {
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  }
  snapshot->renderer = VRCLIENT_UNREAL_RENDERER_D3D12;
  snapshot->device = state->graphics_ready ? reinterpret_cast<void*>(1) : nullptr;
  snapshot->queue = state->graphics_ready ? reinterpret_cast<void*>(2) : nullptr;
  snapshot->swapchain = state->graphics_ready ? reinterpret_cast<void*>(3) : nullptr;
  snapshot->ready = state->graphics_ready ? 1u : 0u;
  return VR_ADAPTER_OK;
}

VrAdapterResult VRCLIENT_ADAPTER_CALL queryCamera(
    void* user_data,
    VrUnrealCameraSnapshot* snapshot) {
  auto* state = static_cast<TestState*>(user_data);
  if (state == nullptr || snapshot == nullptr ||
      snapshot->size < sizeof(VrUnrealCameraSnapshot)) {
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  }
  snapshot->camera_id = state->camera_ready ? 7 : 0;
  snapshot->sample_index = state->camera_ready ? 1 : 0;
  snapshot->sample_time_ns = state->camera_ready ? 1000 : 0;
  snapshot->active = state->camera_ready ? 1u : 0u;
  snapshot->ready = state->camera_ready ? 1u : 0u;
  snapshot->vertical_fov_degrees = 80.0F;
  snapshot->aspect_ratio = 16.0F / 9.0F;
  snapshot->near_clip_uu = 10.0F;
  snapshot->far_clip_uu = 100'000.0F;
  return VR_ADAPTER_OK;
}

VrAdapterContext makeContext(const VrAdapterHostServices& services) {
  VrAdapterContext context{};
  context.size = sizeof(context);
  context.host_services = &services;
  context.target.size = sizeof(context.target);
  context.target.game_id = "meccha-chameleon";
  context.target.build_id = "steam-4704690-build-24517175";
  context.target.executable_path =
      "G:\\SteamLibrary\\steamapps\\common\\MECCHA CHAMELEON\\Chameleon\\"
      "Binaries\\Win64\\PenguinHotel-Win64-Shipping.exe";
  context.target.source = "meccha_native_adapter_test";
  return context;
}

VrAdapterFrameInfo makeFrame(std::uint64_t index) {
  VrAdapterFrameInfo frame{};
  frame.size = sizeof(frame);
  frame.frame_index = index;
  frame.predicted_display_time_ns = 1000;
  frame.predicted_display_period_seconds = 0.011;
  frame.delta_seconds = 0.011;
  frame.callback_flags = VR_ADAPTER_CALLBACK_HOT_PATH;
  return frame;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    expect(argc >= 2, "usage: meccha_native_adapter_tests <adapter-dll>");

    TestState state;
    VrAdapterDiagnosticsService diagnostics{
        sizeof(VrAdapterDiagnosticsService),
        1,
        &logDiagnostic,
        &state,
    };
    VrUnrealGraphicsService graphics{
        sizeof(VrUnrealGraphicsService),
        VRCLIENT_UNREAL_GRAPHICS_SERVICE_VERSION,
        &queryGraphics,
        &state,
    };
    VrUnrealCameraService camera{
        sizeof(VrUnrealCameraService),
        VRCLIENT_UNREAL_CAMERA_SERVICE_VERSION,
        &queryCamera,
        &state,
    };
    std::vector<VrAdapterService> services = {
        {sizeof(VrAdapterService), VRCLIENT_ADAPTER_SERVICE_DIAGNOSTICS, 1, &diagnostics},
        {sizeof(VrAdapterService),
         VRCLIENT_ADAPTER_SERVICE_UNREAL_GRAPHICS,
         VRCLIENT_UNREAL_GRAPHICS_SERVICE_VERSION,
         &graphics},
        {sizeof(VrAdapterService),
         VRCLIENT_ADAPTER_SERVICE_UNREAL_CAMERA,
         VRCLIENT_UNREAL_CAMERA_SERVICE_VERSION,
         &camera},
    };

    vrclient::plugins::host::PluginLoadRequest stale;
    stale.plugin_path = std::filesystem::path(argv[1]);
    stale.target_game_id = "meccha-chameleon";
    stale.target_build_id = "steam-4704690-build-stale";
    stale.services = services;
    auto stale_load = vrclient::plugins::host::loadPluginFromLibrary(stale);
    expect(
        stale_load.status == vrclient::plugins::host::PluginHostStatus::WrongTarget,
        "stale Meccha build must be refused");

    vrclient::plugins::host::PluginLoadRequest request;
    request.plugin_path = std::filesystem::path(argv[1]);
    request.target_game_id = "meccha-chameleon";
    request.target_build_id = "steam-4704690-build-24517175";
    request.services = services;
    auto loaded = vrclient::plugins::host::loadPluginFromLibrary(request);
    expect(
        loaded.status == vrclient::plugins::host::PluginHostStatus::Loaded,
        "Meccha adapter should load for exact metadata target");

    auto context = makeContext(loaded.adapter.host_services);
    auto validate = vrclient::plugins::host::dispatchLifecycle(
        loaded.adapter,
        vrclient::plugins::host::AdapterLifecycleCall::Validate,
        context);
    expect(validate.adapter_result == VR_ADAPTER_OK, "exact target validate failed");

    auto wrong_exe = context;
    wrong_exe.target.executable_path = "PenguinHotel.exe";
    auto refused = vrclient::plugins::host::dispatchLifecycle(
        loaded.adapter,
        vrclient::plugins::host::AdapterLifecycleCall::Validate,
        wrong_exe);
    expect(
        refused.adapter_result == VR_ADAPTER_ERROR_UNSUPPORTED_TARGET,
        "launcher stub must be refused");

    auto init = vrclient::plugins::host::dispatchLifecycle(
        loaded.adapter,
        vrclient::plugins::host::AdapterLifecycleCall::Init,
        context);
    expect(init.adapter_result == VR_ADAPTER_OK, "adapter init failed");
    expect(
        state.init_events == 1 && state.pending_events == 1,
        "init must report pending graphics");

    auto pending = vrclient::plugins::host::dispatchFrameTick(
        loaded.adapter,
        context,
        makeFrame(1));
    expect(
        pending.adapter_result == VR_ADAPTER_SKIPPED &&
            !pending.adapter_disabled,
        "pending graphics tick must skip without disabling");

    state.graphics_ready = true;
    auto camera_pending = vrclient::plugins::host::dispatchFrameTick(
        loaded.adapter,
        context,
        makeFrame(2));
    expect(
        camera_pending.adapter_result == VR_ADAPTER_SKIPPED &&
            !camera_pending.adapter_disabled,
        "missing camera sample must skip without disabling");
    expect(state.camera_pending_events == 1, "camera pending event missing");

    state.camera_ready = true;
    auto ready = vrclient::plugins::host::dispatchFrameTick(
        loaded.adapter,
        context,
        makeFrame(3));
    expect(ready.adapter_result == VR_ADAPTER_OK, "ready graphics tick failed");
    expect(state.ready_events == 1, "graphics ready event should log once");
    expect(state.camera_ready_events == 1, "camera ready event should log once");

    auto shutdown = vrclient::plugins::host::dispatchLifecycle(
        loaded.adapter,
        vrclient::plugins::host::AdapterLifecycleCall::Shutdown,
        context);
    expect(shutdown.adapter_result == VR_ADAPTER_OK, "shutdown failed");
    expect(state.shutdown_events == 1, "shutdown event missing");
    vrclient::plugins::host::unloadPlugin(loaded.adapter);

    std::cout << "Meccha native adapter lifecycle test passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}

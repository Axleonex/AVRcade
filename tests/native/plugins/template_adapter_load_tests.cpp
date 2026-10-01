#include "plugins/host/plugin_library.h"
#include "shared/comfort/comfort_system.h"
#include "shared/game_profile.h"
#include "shared/hud/hud_system.h"
#include "shared/input/input_system.h"

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

VrAdapterContext context(const VrAdapterHostServices& services) {
  VrAdapterContext out{};
  out.size = sizeof(VrAdapterContext);
  out.host_services = &services;
  out.target.size = sizeof(VrAdapterTargetIdentity);
  out.target.game_id = "vrclient-smoke-host";
  out.target.build_id = "smoke-2026-06-11";
  out.target.executable_path = "C:/VRClientSmoke/vrclient_smoke_host.exe";
  out.target.source = "template_adapter_load_test";
  return out;
}

VrAdapterFrameInfo frame() {
  VrAdapterFrameInfo out{};
  out.size = sizeof(VrAdapterFrameInfo);
  out.frame_index = 1;
  out.predicted_display_time_ns = 1234;
  out.predicted_display_period_seconds = 0.011;
  out.delta_seconds = 0.011;
  out.callback_flags = VR_ADAPTER_CALLBACK_HOT_PATH;
  return out;
}

void expectLoadedCall(
    const vrclient::plugins::host::PluginCallResult& call,
    const std::string& message) {
  expect(call.status == vrclient::plugins::host::PluginHostStatus::Loaded, message);
  expect(!call.adapter_disabled, message + " disabled adapter");
}

struct DiagnosticsCounts {
  int input = 0;
  int comfort = 0;
  int hud = 0;
};

void VRCLIENT_ADAPTER_CALL logDiagnostic(
    void* user_data,
    const char* event_name,
    const char*,
    const char*) {
  if (user_data == nullptr || event_name == nullptr) {
    return;
  }
  auto* counts = static_cast<DiagnosticsCounts*>(user_data);
  const std::string event = event_name;
  if (event == "template_adapter_input_service") {
    counts->input += 1;
  } else if (event == "template_adapter_comfort_service") {
    counts->comfort += 1;
  } else if (event == "template_adapter_hud_service") {
    counts->hud += 1;
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    expect(argc >= 2, "usage: vr_template_adapter_load_tests <template-adapter-dll>");

    const auto profile = vrclient::shared::defaultGameProfile();
    vrclient::shared::input::InputServiceRuntime input(profile.input);
    input.setActionState(VRCLIENT_INPUT_ACTION_INTERACT, true, 1.0f, 456);
    vrclient::shared::comfort::ComfortServiceRuntime comfort(profile.comfort);
    vrclient::shared::hud::HudServiceRuntime hud(profile.hud);

    DiagnosticsCounts counts;
    VrAdapterDiagnosticsService diagnostics{};
    diagnostics.size = sizeof(VrAdapterDiagnosticsService);
    diagnostics.version = VRCLIENT_SHARED_SERVICE_VERSION;
    diagnostics.log_event = &logDiagnostic;
    diagnostics.user_data = &counts;
    VrAdapterService diagnostics_service{
        sizeof(VrAdapterService),
        VRCLIENT_ADAPTER_SERVICE_DIAGNOSTICS,
        VRCLIENT_SHARED_SERVICE_VERSION,
        &diagnostics,
    };

    vrclient::plugins::host::PluginLoadRequest request;
    request.plugin_path = std::filesystem::path(argv[1]);
    request.target_game_id = "vrclient-smoke-host";
    request.target_build_id = "smoke-2026-06-11";
    request.services = {
        diagnostics_service,
        input.adapterService(),
        comfort.adapterService(),
        hud.adapterService(),
    };

    auto loaded = vrclient::plugins::host::loadPluginFromLibrary(request);
    expect(
        loaded.status == vrclient::plugins::host::PluginHostStatus::Loaded,
        "template adapter DLL should load through host resolver: " + loaded.reason_code);
    expect(
        loaded.adapter.adapter_id == "vrclient-template-adapter",
        "loaded adapter id mismatch");
    expect(
        loaded.adapter.plugin_file_token.rfind("fnv1a64:", 0) == 0,
        "plugin file token should be recorded");

    const auto ctx = context(loaded.adapter.host_services);
    expectLoadedCall(
        vrclient::plugins::host::dispatchLifecycle(
            loaded.adapter,
            vrclient::plugins::host::AdapterLifecycleCall::Validate,
            ctx),
        "template adapter validate should pass");
    expectLoadedCall(
        vrclient::plugins::host::dispatchLifecycle(
            loaded.adapter,
            vrclient::plugins::host::AdapterLifecycleCall::Init,
            ctx),
        "template adapter init should pass");
    expect(counts.input == 1, "template adapter should read semantic input service");
    expect(counts.comfort == 1, "template adapter should read comfort service");
    expect(counts.hud == 1, "template adapter should read HUD service");
    expectLoadedCall(
        vrclient::plugins::host::dispatchFrameTick(loaded.adapter, ctx, frame()),
        "template adapter frame tick should pass");
    expectLoadedCall(
        vrclient::plugins::host::dispatchLifecycle(
            loaded.adapter,
            vrclient::plugins::host::AdapterLifecycleCall::Shutdown,
            ctx),
        "template adapter shutdown should pass");

    vrclient::plugins::host::unloadPlugin(loaded.adapter);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}

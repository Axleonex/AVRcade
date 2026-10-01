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

std::filesystem::path repoProfilePath() {
#if defined(VRCLIENT_SOURCE_DIR)
  return std::filesystem::path(VRCLIENT_SOURCE_DIR) / "config" / "profiles" /
      "repo-game-profile.json";
#else
  return std::filesystem::path("config/profiles/repo-game-profile.json");
#endif
}

VrAdapterContext context(const VrAdapterHostServices& services) {
  VrAdapterContext out{};
  out.size = sizeof(VrAdapterContext);
  out.host_services = &services;
  out.target.size = sizeof(VrAdapterTargetIdentity);
  out.target.game_id = "repo";
  out.target.build_id = "steam-3241660-build-23363152";
  out.target.executable_path = "H:/SteamLibrary/steamapps/common/REPO/REPO.exe";
  out.target.source = "repo_adapter_load_test";
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
  int init = 0;
  int input = 0;
  int comfort = 0;
  int hud = 0;
  int hook_block = 0;
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
  if (event == "repo_adapter_init") {
    counts->init += 1;
  } else if (event == "repo_adapter_input_service") {
    counts->input += 1;
  } else if (event == "repo_adapter_comfort_service") {
    counts->comfort += 1;
  } else if (event == "repo_adapter_hud_service") {
    counts->hud += 1;
  } else if (event == "repo_adapter_hook_surface_unvalidated") {
    counts->hook_block += 1;
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    expect(argc >= 2, "usage: vr_repo_adapter_tests <repo-adapter-dll>");

    const auto loaded_profile =
        vrclient::shared::loadGameProfileFromFile(repoProfilePath().string().c_str());
    expect(loaded_profile.loaded, loaded_profile.message);
    expect(
        loaded_profile.profile.profile_id == "repo-steam-3241660-build-23363152",
        "R.E.P.O. profile id mismatch");

    vrclient::shared::input::InputServiceRuntime input(loaded_profile.profile.input);
    input.setActionState(VRCLIENT_INPUT_ACTION_INTERACT, true, 1.0f, 456);
    vrclient::shared::comfort::ComfortServiceRuntime comfort(loaded_profile.profile.comfort);
    vrclient::shared::hud::HudServiceRuntime hud(loaded_profile.profile.hud);

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
    std::vector<VrAdapterService> services = {
        diagnostics_service,
        input.adapterService(),
        comfort.adapterService(),
        hud.adapterService(),
    };

    vrclient::plugins::host::PluginLoadRequest wrong_request;
    wrong_request.plugin_path = std::filesystem::path(argv[1]);
    wrong_request.target_game_id = "vrclient-smoke-host";
    wrong_request.target_build_id = "smoke-2026-06-11";
    wrong_request.services = services;
    auto wrong_load = vrclient::plugins::host::loadPluginFromLibrary(wrong_request);
    expect(
        wrong_load.status == vrclient::plugins::host::PluginHostStatus::WrongTarget,
        "R.E.P.O. adapter should refuse template smoke host");
    expect(wrong_load.reason_code == "wrong_game_or_build",
           "wrong target reason mismatch");

    vrclient::plugins::host::PluginLoadRequest request;
    request.plugin_path = std::filesystem::path(argv[1]);
    request.target_game_id = "repo";
    request.target_build_id = "steam-3241660-build-23363152";
    request.services = services;

    auto loaded = vrclient::plugins::host::loadPluginFromLibrary(request);
    expect(
        loaded.status == vrclient::plugins::host::PluginHostStatus::Loaded,
        "R.E.P.O. adapter DLL should load through host resolver: " + loaded.reason_code);
    expect(
        loaded.adapter.adapter_id == "vrclient-repo-adapter",
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
        "R.E.P.O. adapter validate should pass");
    expectLoadedCall(
        vrclient::plugins::host::dispatchLifecycle(
            loaded.adapter,
            vrclient::plugins::host::AdapterLifecycleCall::Init,
            ctx),
        "R.E.P.O. adapter init should pass");
    expect(counts.init == 1, "R.E.P.O. adapter should log init");
    expect(counts.input == 1, "R.E.P.O. adapter should read semantic input service");
    expect(counts.comfort == 1, "R.E.P.O. adapter should read comfort service");
    expect(counts.hud == 1, "R.E.P.O. adapter should read HUD service");
    expect(
        counts.hook_block == 1,
        "R.E.P.O. adapter should record unvalidated hook-surface blocker");
    expectLoadedCall(
        vrclient::plugins::host::dispatchFrameTick(loaded.adapter, ctx, frame()),
        "R.E.P.O. adapter frame tick should pass");
    expectLoadedCall(
        vrclient::plugins::host::dispatchLifecycle(
            loaded.adapter,
            vrclient::plugins::host::AdapterLifecycleCall::Shutdown,
            ctx),
        "R.E.P.O. adapter shutdown should pass");

    vrclient::plugins::host::unloadPlugin(loaded.adapter);
    std::cout << "R.E.P.O. adapter host-load test passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}

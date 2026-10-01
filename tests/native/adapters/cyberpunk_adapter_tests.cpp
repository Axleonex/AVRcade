#include "plugins/host/plugin_library.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct DiagnosticsCounts {
  int bridge_unavailable = 0;
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
  if (std::string(event_name) == "cyberpunk_adapter_bridge_unavailable") {
    counts->bridge_unavailable += 1;
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    expect(argc >= 2, "usage: vr_cyberpunk_adapter_tests <adapter-dll>");

    vrclient::plugins::host::PluginLoadRequest wrong_request;
    wrong_request.plugin_path = std::filesystem::path(argv[1]);
    wrong_request.target_game_id = "repo";
    wrong_request.target_build_id = "steam-3241660-build-23363152";
    auto wrong = vrclient::plugins::host::loadPluginFromLibrary(wrong_request);
    expect(
        wrong.status == vrclient::plugins::host::PluginHostStatus::WrongTarget,
        "Cyberpunk adapter must refuse a different game/build");

    DiagnosticsCounts counts;
    VrAdapterDiagnosticsService diagnostics{};
    diagnostics.size = sizeof(VrAdapterDiagnosticsService);
    diagnostics.version = 1;
    diagnostics.log_event = &logDiagnostic;
    diagnostics.user_data = &counts;
    VrAdapterService service{
        sizeof(VrAdapterService),
        VRCLIENT_ADAPTER_SERVICE_DIAGNOSTICS,
        1,
        &diagnostics,
    };

    vrclient::plugins::host::PluginLoadRequest request;
    request.plugin_path = std::filesystem::path(argv[1]);
    request.target_game_id = "cyberpunk-2077";
    request.target_build_id = "steam-1091500-build-20383525";
    request.services = {service};
    auto loaded = vrclient::plugins::host::loadPluginFromLibrary(request);
    expect(
        loaded.status == vrclient::plugins::host::PluginHostStatus::Loaded,
        "Cyberpunk adapter DLL should load for preflight inspection");

    VrAdapterContext context{};
    context.size = sizeof(VrAdapterContext);
    context.host_services = &loaded.adapter.host_services;
    context.target.size = sizeof(VrAdapterTargetIdentity);
    context.target.game_id = "cyberpunk-2077";
    context.target.build_id = "steam-1091500-build-20383525";
    context.target.executable_path = "C:/fixture/Cyberpunk2077.exe";
    context.target.source = "cyberpunk_adapter_load_test";

    const auto validation = vrclient::plugins::host::dispatchLifecycle(
        loaded.adapter,
        vrclient::plugins::host::AdapterLifecycleCall::Validate,
        context);
    expect(
        validation.status == vrclient::plugins::host::PluginHostStatus::AdapterError,
        "Cyberpunk adapter validation must fail without RED4ext bridge services");
    expect(
        validation.adapter_result == VR_ADAPTER_ERROR_UNSUPPORTED_API,
        "missing RED4ext bridge services must return unsupported API");
    expect(!validation.adapter_disabled, "a missing bridge must not poison the DLL");
    expect(counts.bridge_unavailable == 1, "missing bridge must be diagnosable");

    vrclient::plugins::host::unloadPlugin(loaded.adapter);
    std::cout << "Cyberpunk adapter fail-closed host-load test passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}

// SDK-02 closure test (ADDITIVE — new harness, no module source touched).
//
// The plugin lifecycle must support load, validate, init, tick, suspend, resume,
// and shutdown. plugin_tests.cpp already exercises load + validate + tick (and
// template_adapter_load_tests.cpp adds init + shutdown via the real DLL). The
// remaining un-asserted lifecycle calls are SUSPEND, RESUME, and RELOAD_CONFIG.
//
// This test drives ALL SEVEN lifecycle stages through the host dispatcher against
// an in-process fake adapter and asserts each callback actually runs (a counter
// per stage) and that the host reports each as a successful dispatch.

#include "plugins/host/plugin_host.h"

#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct LifecycleCounters {
  int validate = 0;
  int init = 0;
  int tick = 0;
  int suspend = 0;
  int resume = 0;
  int reload_config = 0;
  int shutdown = 0;
};

LifecycleCounters g_counts;

const char* kGames[] = {"vrclient-smoke-host"};
const VrAdapterBuildId kBuilds[] = {
    {sizeof(VrAdapterBuildId), "vrclient-smoke-host", "smoke-2026-06-11"}};

VrAdapterMetadata g_metadata = {
    sizeof(VrAdapterMetadata),
    VRCLIENT_ADAPTER_ABI_VERSION,
    "lifecycle-adapter",
    "0.1.0",
    "Lifecycle Adapter",
    kGames,
    1,
    kBuilds,
    1,
    {sizeof(VrAdapterApiVersion), 1, 0, 0},
    {sizeof(VrAdapterApiVersion), 1, 0, 0},
    VR_ADAPTER_CAP_FRAME_TICK | VR_ADAPTER_CAP_CONFIG_RELOAD |
        VR_ADAPTER_CAP_FAST_RESTART,
    0};

VrAdapterResult VRCLIENT_ADAPTER_CALL onValidate(
    VrGameAdapter* adapter,
    const VrAdapterContext* context) {
  if (adapter == nullptr || context == nullptr) {
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  }
  g_counts.validate += 1;
  return VR_ADAPTER_OK;
}

VrAdapterResult VRCLIENT_ADAPTER_CALL onInit(
    VrGameAdapter* adapter,
    const VrAdapterContext* context) {
  if (adapter == nullptr || context == nullptr) {
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  }
  g_counts.init += 1;
  return VR_ADAPTER_OK;
}

VrAdapterResult VRCLIENT_ADAPTER_CALL onTick(
    VrGameAdapter* adapter,
    const VrAdapterContext*,
    const VrAdapterFrameInfo*) {
  if (adapter == nullptr) {
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  }
  g_counts.tick += 1;
  return VR_ADAPTER_OK;
}

VrAdapterResult VRCLIENT_ADAPTER_CALL onSuspend(
    VrGameAdapter* adapter,
    const VrAdapterContext*) {
  if (adapter == nullptr) {
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  }
  g_counts.suspend += 1;
  return VR_ADAPTER_OK;
}

VrAdapterResult VRCLIENT_ADAPTER_CALL onResume(
    VrGameAdapter* adapter,
    const VrAdapterContext*) {
  if (adapter == nullptr) {
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  }
  g_counts.resume += 1;
  return VR_ADAPTER_OK;
}

VrAdapterResult VRCLIENT_ADAPTER_CALL onReloadConfig(
    VrGameAdapter* adapter,
    const VrAdapterContext*) {
  if (adapter == nullptr) {
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  }
  g_counts.reload_config += 1;
  return VR_ADAPTER_OK;
}

VrAdapterResult VRCLIENT_ADAPTER_CALL onShutdown(
    VrGameAdapter* adapter,
    const VrAdapterContext*) {
  if (adapter == nullptr) {
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  }
  g_counts.shutdown += 1;
  return VR_ADAPTER_OK;
}

VrGameAdapter g_adapter = {
    sizeof(VrGameAdapter),
    nullptr,
    onValidate,
    onInit,
    onTick,
    onSuspend,
    onResume,
    onReloadConfig,
    onShutdown};

std::uint32_t VRCLIENT_ADAPTER_CALL getAbi() {
  return VRCLIENT_ADAPTER_ABI_VERSION;
}

const VrAdapterMetadata* VRCLIENT_ADAPTER_CALL getMetadata() {
  return &g_metadata;
}

VrAdapterResult VRCLIENT_ADAPTER_CALL createAdapter(
    const VrAdapterHostServices* services,
    VrGameAdapter** adapter) {
  if (services == nullptr || adapter == nullptr) {
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  }
  *adapter = &g_adapter;
  return VR_ADAPTER_OK;
}

void VRCLIENT_ADAPTER_CALL destroyAdapter(VrGameAdapter*) {}

vrclient::plugins::host::PluginLoadRequest goodRequest() {
  vrclient::plugins::host::PluginLoadRequest request;
  request.plugin_path = "C:/VRClient/adapters/lifecycle-adapter.dll";
  request.target_game_id = "vrclient-smoke-host";
  request.target_build_id = "smoke-2026-06-11";
  request.exports = {getAbi, getMetadata, createAdapter, destroyAdapter};
  request.frame_callback_budget_us = 1000000;
  return request;
}

VrAdapterContext context() {
  VrAdapterContext out{};
  out.size = sizeof(VrAdapterContext);
  out.target.size = sizeof(VrAdapterTargetIdentity);
  out.target.game_id = "vrclient-smoke-host";
  out.target.build_id = "smoke-2026-06-11";
  out.target.executable_path = "C:/VRClientSmoke/vrclient_smoke_host.exe";
  out.target.source = "lifecycle_full_test";
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

using vrclient::plugins::host::AdapterLifecycleCall;
using vrclient::plugins::host::PluginHostStatus;

void expectLoaded(
    const vrclient::plugins::host::PluginCallResult& call,
    const std::string& stage) {
  expect(call.status == PluginHostStatus::Loaded,
         stage + " dispatch should report Loaded: " + call.reason_code);
  expect(!call.adapter_disabled, stage + " dispatch should not disable the adapter");
}

}  // namespace

int main() {
  g_counts = {};

  // load
  auto loaded = vrclient::plugins::host::loadPlugin(goodRequest());
  expect(loaded.status == PluginHostStatus::Loaded, "lifecycle adapter should load");
  auto& instance = loaded.adapter;
  const auto ctx = context();

  // validate
  expectLoaded(
      vrclient::plugins::host::dispatchLifecycle(
          instance, AdapterLifecycleCall::Validate, ctx),
      "validate");
  // init
  expectLoaded(
      vrclient::plugins::host::dispatchLifecycle(
          instance, AdapterLifecycleCall::Init, ctx),
      "init");
  // tick
  expectLoaded(
      vrclient::plugins::host::dispatchFrameTick(instance, ctx, frame()),
      "tick");
  // suspend
  expectLoaded(
      vrclient::plugins::host::dispatchLifecycle(
          instance, AdapterLifecycleCall::Suspend, ctx),
      "suspend");
  // resume
  expectLoaded(
      vrclient::plugins::host::dispatchLifecycle(
          instance, AdapterLifecycleCall::Resume, ctx),
      "resume");
  // reload_config
  expectLoaded(
      vrclient::plugins::host::dispatchLifecycle(
          instance, AdapterLifecycleCall::ReloadConfig, ctx),
      "reload_config");
  // shutdown
  expectLoaded(
      vrclient::plugins::host::dispatchLifecycle(
          instance, AdapterLifecycleCall::Shutdown, ctx),
      "shutdown");

  vrclient::plugins::host::unloadPlugin(instance);

  // Every lifecycle stage actually executed exactly once.
  expect(g_counts.validate == 1, "validate callback must run");
  expect(g_counts.init == 1, "init callback must run");
  expect(g_counts.tick == 1, "tick callback must run");
  expect(g_counts.suspend == 1, "suspend callback must run");
  expect(g_counts.resume == 1, "resume callback must run");
  expect(g_counts.reload_config == 1, "reload_config callback must run");
  expect(g_counts.shutdown == 1, "shutdown callback must run");

  return 0;
}

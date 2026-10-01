#include "plugins/host/plugin_host.h"
#include "plugins/validation/adapter_validator.h"

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct FakeState {
  bool throw_on_tick = false;
  bool recoverable_on_tick = false;
  bool spin_on_tick = false;
  std::uint64_t tick_count = 0;
};

FakeState g_state;

const char* kGames[] = {"vrclient-smoke-host"};
const VrAdapterBuildId kBuilds[] = {
    {sizeof(VrAdapterBuildId), "vrclient-smoke-host", "smoke-2026-06-11"}};

VrAdapterMetadata g_metadata = {
    sizeof(VrAdapterMetadata),
    VRCLIENT_ADAPTER_ABI_VERSION,
    "test-adapter",
    "0.1.0",
    "Test Adapter",
    kGames,
    1,
    kBuilds,
    1,
    {sizeof(VrAdapterApiVersion), 1, 0, 0},
    {sizeof(VrAdapterApiVersion), 1, 0, 0},
    VR_ADAPTER_CAP_FRAME_TICK | VR_ADAPTER_CAP_FAST_RESTART,
    0};

VrAdapterResult VRCLIENT_ADAPTER_CALL okSimple(
    VrGameAdapter* adapter,
    const VrAdapterContext*) {
  return adapter == nullptr ? VR_ADAPTER_ERROR_INVALID_ARGUMENT : VR_ADAPTER_OK;
}

VrAdapterResult VRCLIENT_ADAPTER_CALL okValidate(
    VrGameAdapter* adapter,
    const VrAdapterContext* context) {
  return adapter == nullptr || context == nullptr
      ? VR_ADAPTER_ERROR_INVALID_ARGUMENT
      : VR_ADAPTER_OK;
}

VrAdapterResult VRCLIENT_ADAPTER_CALL testTick(
    VrGameAdapter* adapter,
    const VrAdapterContext*,
    const VrAdapterFrameInfo*) {
  if (adapter == nullptr) {
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  }
  if (g_state.throw_on_tick) {
    throw std::runtime_error("test adapter throw");
  }
  if (g_state.recoverable_on_tick) {
    return VR_ADAPTER_ERROR_RECOVERABLE;
  }
  if (g_state.spin_on_tick) {
    volatile std::uint64_t value = 0;
    for (std::uint64_t index = 0; index < 250000; ++index) {
      value += index;
    }
  }
  g_state.tick_count += 1;
  return VR_ADAPTER_OK;
}

VrGameAdapter g_adapter = {
    sizeof(VrGameAdapter),
    &g_state,
    okValidate,
    okValidate,
    testTick,
    okSimple,
    okSimple,
    okSimple,
    okSimple};

std::uint32_t VRCLIENT_ADAPTER_CALL getAbi() {
  return VRCLIENT_ADAPTER_ABI_VERSION;
}

std::uint32_t VRCLIENT_ADAPTER_CALL getBadAbi() {
  return 99;
}

const VrAdapterMetadata* VRCLIENT_ADAPTER_CALL getMetadata() {
  return &g_metadata;
}

const VrAdapterMetadata* VRCLIENT_ADAPTER_CALL getNoMetadata() {
  return nullptr;
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

vrclient::plugins::host::PluginExports goodExports() {
  return {getAbi, getMetadata, createAdapter, destroyAdapter};
}

vrclient::plugins::host::PluginLoadRequest goodRequest() {
  vrclient::plugins::host::PluginLoadRequest request;
  request.plugin_path = "C:/VRClient/adapters/test-adapter.dll";
  request.target_game_id = "vrclient-smoke-host";
  request.target_build_id = "smoke-2026-06-11";
  request.exports = goodExports();
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
  out.target.source = "direct";
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

void runLoadValidationTests() {
  auto request = goodRequest();
  auto result = vrclient::plugins::host::loadPlugin(request);
  expect(result.status == vrclient::plugins::host::PluginHostStatus::Loaded,
         "good plugin should load");
  expect(result.adapter.adapter_id == "test-adapter", "adapter id mismatch");
  expect(result.adapter.plugin_file_token.find("fnv1a64:") == 0,
         "plugin file token should be recorded");
  vrclient::plugins::host::unloadPlugin(result.adapter);

  request = goodRequest();
  request.exports.get_metadata = getNoMetadata;
  result = vrclient::plugins::host::loadPlugin(request);
  expect(result.status == vrclient::plugins::host::PluginHostStatus::MissingMetadata,
         "missing metadata should refuse");

  request = goodRequest();
  request.exports.get_abi = getBadAbi;
  result = vrclient::plugins::host::loadPlugin(request);
  expect(result.status == vrclient::plugins::host::PluginHostStatus::AbiMismatch,
         "ABI mismatch should refuse");

  request = goodRequest();
  request.target_build_id = "wrong-build";
  result = vrclient::plugins::host::loadPlugin(request);
  expect(result.status == vrclient::plugins::host::PluginHostStatus::WrongTarget,
         "wrong build should refuse");

  request = goodRequest();
  request.exports.create_adapter = nullptr;
  result = vrclient::plugins::host::loadPlugin(request);
  expect(result.status == vrclient::plugins::host::PluginHostStatus::MissingExport,
         "missing export should refuse");

  request = goodRequest();
  request.plugin_path = "C:/VRClient/adapters/../bad.dll";
  result = vrclient::plugins::host::loadPlugin(request);
  expect(result.status == vrclient::plugins::host::PluginHostStatus::UnsafePath,
         "unsafe path should refuse");
}

void runLifecycleTests() {
  g_state = {};
  auto loaded = vrclient::plugins::host::loadPlugin(goodRequest());
  auto instance = loaded.adapter;
  const auto ctx = context();

  auto call = vrclient::plugins::host::dispatchLifecycle(
      instance,
      vrclient::plugins::host::AdapterLifecycleCall::Validate,
      ctx);
  expect(call.status == vrclient::plugins::host::PluginHostStatus::Loaded,
         "validate should pass");

  auto tick = vrclient::plugins::host::dispatchFrameTick(instance, ctx, frame());
  expect(tick.status == vrclient::plugins::host::PluginHostStatus::Loaded,
         "tick should pass");
  expect(g_state.tick_count == 1, "tick should run");

  g_state.recoverable_on_tick = true;
  tick = vrclient::plugins::host::dispatchFrameTick(instance, ctx, frame());
  expect(tick.status == vrclient::plugins::host::PluginHostStatus::AdapterError,
         "recoverable adapter error should disable");
  expect(instance.disabled, "adapter should be disabled after recoverable error");

  vrclient::plugins::host::unloadPlugin(instance);
}

void runOverrunAndExceptionTests() {
  g_state = {};
  auto request = goodRequest();
  request.frame_callback_budget_us = 0;
  request.overrun_disable_threshold = 1;
  auto loaded = vrclient::plugins::host::loadPlugin(request);
  auto instance = loaded.adapter;
  g_state.spin_on_tick = true;
  auto tick = vrclient::plugins::host::dispatchFrameTick(instance, context(), frame());
  expect(tick.status == vrclient::plugins::host::PluginHostStatus::CallbackOverrun,
         "callback overrun should be reported");
  expect(instance.disabled, "overrun threshold should disable adapter");
  vrclient::plugins::host::unloadPlugin(instance);

  g_state = {};
  loaded = vrclient::plugins::host::loadPlugin(goodRequest());
  instance = loaded.adapter;
  g_state.throw_on_tick = true;
  tick = vrclient::plugins::host::dispatchFrameTick(instance, context(), frame());
  expect(tick.reason_code == "adapter_exception", "throwing callback should be caught");
  expect(instance.disabled, "throwing callback should disable adapter");
  vrclient::plugins::host::unloadPlugin(instance);
}

void runReloadTests() {
  auto loaded = vrclient::plugins::host::loadPlugin(goodRequest());
  auto instance = loaded.adapter;

  vrclient::plugins::host::ReloadState state;
  state.active_callbacks = true;
  auto reload = vrclient::plugins::host::requestHotReload(instance, state);
  expect(reload.status == vrclient::plugins::host::PluginHostStatus::ReloadRefused,
         "hot reload should refuse active callbacks");

  state = {};
  reload = vrclient::plugins::host::requestHotReload(instance, state);
  expect(reload.status == vrclient::plugins::host::PluginHostStatus::Loaded,
         "hot reload should allow safe state");

  const auto restart = vrclient::plugins::host::fastRestart(instance);
  expect(restart.reason_code == "fast_restart_preserves_diagnostics",
         "fast restart should preserve diagnostics context");
  vrclient::plugins::host::unloadPlugin(instance);
}

void runValidatorTests() {
  vrclient::plugins::validation::AdapterValidationRequest request;
  request.plugin_path = "C:/VRClient/adapters/test-adapter.dll";
  request.expected_game_id = "vrclient-smoke-host";
  request.expected_build_id = "smoke-2026-06-11";
  request.exports = goodExports();
  request.manifest_required = false;
  const auto result = vrclient::plugins::validation::validateAdapterPackage(request);
  expect(result.passed, "validator should pass good exports");
  expect(result.load_compatible, "validator should report load-compatible");
  expect(result.target_compatible, "validator should report target-compatible");
  expect(result.runtime_safe, "validator should report runtime-safe");

  auto bad = request;
  bad.expected_build_id = "wrong-build";
  const auto wrong_build = vrclient::plugins::validation::validateAdapterPackage(bad);
  expect(!wrong_build.passed, "validator should fail wrong build");
}

}  // namespace

int main() {
  runLoadValidationTests();
  runLifecycleTests();
  runOverrunAndExceptionTests();
  runReloadTests();
  runValidatorTests();
  return 0;
}

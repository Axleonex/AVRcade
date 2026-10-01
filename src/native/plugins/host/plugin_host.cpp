#include "plugins/host/plugin_host.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace vrclient::plugins::host {
namespace {

std::string boolText(bool value) {
  return value ? "true" : "false";
}

std::string pathString(const std::filesystem::path& path) {
  return path.lexically_normal().generic_string();
}

bool stringEquals(const char* left, const std::string& right) {
  return left != nullptr && right == left;
}

bool hasUnsafePathComponent(const std::filesystem::path& path) {
  for (const auto& part : path) {
    if (part == "..") {
      return true;
    }
  }
  return false;
}

bool isNetworkPath(const std::filesystem::path& path) {
  const std::string text = path.generic_string();
  return text.rfind("//", 0) == 0 || text.rfind("\\\\", 0) == 0;
}

std::uint64_t fnv1a(const std::string& text, std::uint64_t seed = 1469598103934665603ull) {
  std::uint64_t value = seed;
  for (unsigned char ch : text) {
    value ^= ch;
    value *= 1099511628211ull;
  }
  return value;
}

std::string hex64(std::uint64_t value) {
  constexpr char digits[] = "0123456789abcdef";
  std::string out(16, '0');
  for (int index = 15; index >= 0; --index) {
    out[static_cast<std::size_t>(index)] = digits[value & 0x0f];
    value >>= 4;
  }
  return out;
}

std::string fileToken(const std::filesystem::path& path, bool file_exists) {
  std::uint64_t token = fnv1a(pathString(path));
  if (file_exists) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (!error) {
      token = fnv1a(std::to_string(size), token);
    }
  }
  return "fnv1a64:" + hex64(token);
}

PluginHostResult result(
    PluginHostStatus status,
    std::string reason_code,
    std::string message,
    const PluginLoadRequest& request) {
  PluginHostResult out;
  out.status = status;
  out.reason_code = std::move(reason_code);
  out.message = std::move(message);
  out.diagnostics_fields = {
      {"plugin_path", pathString(request.plugin_path)},
      {"reason_code", out.reason_code},
      {"target_game_id", request.target_game_id},
      {"target_build_id", request.target_build_id},
  };
  return out;
}

PluginCallResult callResult(
    PluginHostStatus status,
    std::string reason_code,
    std::string message,
    const AdapterInstance& adapter,
    AdapterLifecycleCall call) {
  PluginCallResult out;
  out.status = status;
  out.reason_code = std::move(reason_code);
  out.message = std::move(message);
  out.adapter_disabled = adapter.disabled;
  out.diagnostics_fields = {
      {"adapter_id", adapter.adapter_id},
      {"adapter_version", adapter.adapter_version},
      {"lifecycle_call", lifecycleCallName(call)},
      {"reason_code", out.reason_code},
      {"disabled", boolText(adapter.disabled)},
  };
  return out;
}

void logLoad(
    const PluginHostResult& load,
    diagnostics::AsyncLogger* logger) {
  if (logger == nullptr) {
    return;
  }
  logger->log(
      load.status == PluginHostStatus::Loaded ? diagnostics::Severity::Info
                                              : diagnostics::Severity::Warning,
      "plugin_load_result",
      load.diagnostics_fields);
}

void logCall(
    const PluginCallResult& call,
    diagnostics::AsyncLogger* logger) {
  if (logger == nullptr) {
    return;
  }
  logger->log(
      call.status == PluginHostStatus::Loaded ? diagnostics::Severity::Info
                                              : diagnostics::Severity::Warning,
      "plugin_lifecycle_result",
      call.diagnostics_fields);
}

bool metadataSupportsTarget(
    const VrAdapterMetadata& metadata,
    const std::string& target_game_id,
    const std::string& target_build_id) {
  bool game_supported = false;
  for (std::uint32_t index = 0; index < metadata.supported_game_id_count; ++index) {
    if (stringEquals(metadata.supported_game_ids[index], target_game_id)) {
      game_supported = true;
      break;
    }
  }
  if (!game_supported) {
    return false;
  }
  for (std::uint32_t index = 0; index < metadata.supported_build_count; ++index) {
    const VrAdapterBuildId& build = metadata.supported_builds[index];
    if (stringEquals(build.game_id, target_game_id) &&
        stringEquals(build.build_id, target_build_id)) {
      return true;
    }
  }
  return false;
}

bool hasAllExports(const PluginExports& exports) {
  return exports.get_abi != nullptr &&
      exports.get_metadata != nullptr &&
      exports.create_adapter != nullptr &&
      exports.destroy_adapter != nullptr;
}

void disable(AdapterInstance& adapter, std::string reason) {
  adapter.disabled = true;
  adapter.disabled_reason = std::move(reason);
}

using Clock = std::chrono::steady_clock;

std::uint64_t elapsedMicros(Clock::time_point start, Clock::time_point end) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}

}  // namespace

const char* pluginHostStatusName(PluginHostStatus status) {
  switch (status) {
    case PluginHostStatus::Loaded:
      return "loaded";
    case PluginHostStatus::Disabled:
      return "disabled";
    case PluginHostStatus::MissingMetadata:
      return "missing_metadata";
    case PluginHostStatus::MissingExport:
      return "missing_export";
    case PluginHostStatus::AbiMismatch:
      return "abi_mismatch";
    case PluginHostStatus::IncompatibleApi:
      return "incompatible_api";
    case PluginHostStatus::WrongTarget:
      return "wrong_target";
    case PluginHostStatus::UnsafePath:
      return "unsafe_path";
    case PluginHostStatus::CreateFailed:
      return "create_failed";
    case PluginHostStatus::AdapterError:
      return "adapter_error";
    case PluginHostStatus::CallbackOverrun:
      return "callback_overrun";
    case PluginHostStatus::ReloadRefused:
      return "reload_refused";
    case PluginHostStatus::InvalidRequest:
      return "invalid_request";
  }
  return "invalid_request";
}

const char* lifecycleCallName(AdapterLifecycleCall call) {
  switch (call) {
    case AdapterLifecycleCall::Validate:
      return "validate";
    case AdapterLifecycleCall::Init:
      return "init";
    case AdapterLifecycleCall::Tick:
      return "tick";
    case AdapterLifecycleCall::Suspend:
      return "suspend";
    case AdapterLifecycleCall::Resume:
      return "resume";
    case AdapterLifecycleCall::ReloadConfig:
      return "reload_config";
    case AdapterLifecycleCall::Shutdown:
      return "shutdown";
  }
  return "unknown";
}

PluginHostResult loadPlugin(
    const PluginLoadRequest& request,
    diagnostics::AsyncLogger* logger) {
  if (request.plugin_path.empty()) {
    auto out = result(
        PluginHostStatus::InvalidRequest,
        "missing_plugin_path",
        "plugin load requires an explicit path",
        request);
    logLoad(out, logger);
    return out;
  }
  if (hasUnsafePathComponent(request.plugin_path) || isNetworkPath(request.plugin_path)) {
    auto out = result(
        PluginHostStatus::UnsafePath,
        "unsafe_plugin_path",
        "plugin path is not a safe explicit local path",
        request);
    logLoad(out, logger);
    return out;
  }
  if (!hasAllExports(request.exports)) {
    auto out = result(
        PluginHostStatus::MissingExport,
        "missing_exported_symbol",
        "plugin does not expose the required adapter ABI symbols",
        request);
    logLoad(out, logger);
    return out;
  }
  if (request.exports.get_abi() != VRCLIENT_ADAPTER_ABI_VERSION) {
    auto out = result(
        PluginHostStatus::AbiMismatch,
        "abi_version_mismatch",
        "plugin ABI version does not match host ABI",
        request);
    logLoad(out, logger);
    return out;
  }
  const VrAdapterMetadata* metadata = request.exports.get_metadata();
  if (!sdk::metadataHasAbiShape(metadata)) {
    auto out = result(
        PluginHostStatus::MissingMetadata,
        "missing_or_malformed_metadata",
        "plugin metadata is missing required ABI fields",
        request);
    logLoad(out, logger);
    return out;
  }
  if (!sdk::apiVersionInRange(
          request.host_api_version,
          metadata->required_host_api_min,
          metadata->required_host_api_max)) {
    auto out = result(
        PluginHostStatus::IncompatibleApi,
        "incompatible_api_version",
        "plugin required host API range does not include this host",
        request);
    logLoad(out, logger);
    return out;
  }
  if (!metadataSupportsTarget(*metadata, request.target_game_id, request.target_build_id)) {
    auto out = result(
        PluginHostStatus::WrongTarget,
        "wrong_game_or_build",
        "plugin metadata does not support the detected game/build identity",
        request);
    logLoad(out, logger);
    return out;
  }

  VrGameAdapter* created = nullptr;
  auto service_table = std::make_shared<std::vector<VrAdapterService>>(request.services);
  VrAdapterHostServices services{};
  services.size = sizeof(VrAdapterHostServices);
  services.host_api_version = request.host_api_version;
  services.services = service_table->empty() ? nullptr : service_table->data();
  services.service_count = static_cast<std::uint32_t>(service_table->size());
  const VrAdapterResult create_result = request.exports.create_adapter(&services, &created);
  if (create_result != VR_ADAPTER_OK || created == nullptr ||
      created->size < sizeof(VrGameAdapter)) {
    auto out = result(
        PluginHostStatus::CreateFailed,
        "adapter_create_failed",
        "plugin factory did not create a valid adapter instance",
        request);
    logLoad(out, logger);
    return out;
  }

  PluginHostResult out = result(
      PluginHostStatus::Loaded,
      "adapter_loaded",
      "plugin adapter loaded and matched the target identity",
      request);
  out.adapter.plugin_path = request.plugin_path.lexically_normal();
  out.adapter.plugin_file_token = fileToken(request.plugin_path, request.plugin_file_exists);
  out.adapter.adapter_id = metadata->adapter_id;
  out.adapter.adapter_version = metadata->adapter_version;
  out.adapter.target_game_id = request.target_game_id;
  out.adapter.target_build_id = request.target_build_id;
  out.adapter.exports = request.exports;
  out.adapter.service_table = service_table;
  out.adapter.metadata = metadata;
  out.adapter.adapter = created;
  out.adapter.host_services = services;
  out.adapter.frame_callback_budget_us = request.frame_callback_budget_us;
  out.adapter.overrun_disable_threshold =
      std::max<std::uint32_t>(1, request.overrun_disable_threshold);
  out.diagnostics_fields.push_back({"adapter_id", out.adapter.adapter_id});
  out.diagnostics_fields.push_back({"adapter_version", out.adapter.adapter_version});
  out.diagnostics_fields.push_back({"plugin_file_token", out.adapter.plugin_file_token});
  logLoad(out, logger);
  return out;
}

PluginCallResult dispatchLifecycle(
    AdapterInstance& adapter,
    AdapterLifecycleCall call,
    const VrAdapterContext& context,
    diagnostics::AsyncLogger* logger) {
  if (adapter.disabled) {
    auto out = callResult(
        PluginHostStatus::Disabled,
        adapter.disabled_reason.empty() ? "adapter_disabled" : adapter.disabled_reason,
        "adapter is disabled",
        adapter,
        call);
    logCall(out, logger);
    return out;
  }
  if (adapter.adapter == nullptr) {
    auto out = callResult(
        PluginHostStatus::InvalidRequest,
        "missing_adapter_instance",
        "lifecycle dispatch requires an adapter instance",
        adapter,
        call);
    logCall(out, logger);
    return out;
  }

  VrAdapterSimpleFn simple = nullptr;
  VrAdapterResult adapter_result = VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  try {
    switch (call) {
      case AdapterLifecycleCall::Validate:
        if (adapter.adapter->validate == nullptr) {
          adapter_result = VR_ADAPTER_ERROR_INVALID_ARGUMENT;
        } else {
          adapter_result = adapter.adapter->validate(adapter.adapter, &context);
        }
        break;
      case AdapterLifecycleCall::Init:
        if (adapter.adapter->init == nullptr) {
          adapter_result = VR_ADAPTER_ERROR_INVALID_ARGUMENT;
        } else {
          adapter_result = adapter.adapter->init(adapter.adapter, &context);
        }
        break;
      case AdapterLifecycleCall::Suspend:
        simple = adapter.adapter->suspend;
        break;
      case AdapterLifecycleCall::Resume:
        simple = adapter.adapter->resume;
        break;
      case AdapterLifecycleCall::ReloadConfig:
        simple = adapter.adapter->reload_config;
        break;
      case AdapterLifecycleCall::Shutdown:
        simple = adapter.adapter->shutdown;
        break;
      case AdapterLifecycleCall::Tick:
        break;
    }
    if (simple != nullptr) {
      adapter_result = simple(adapter.adapter, &context);
    }
  } catch (...) {
    disable(adapter, "adapter_exception");
    auto out = callResult(
        PluginHostStatus::AdapterError,
        "adapter_exception",
        "adapter threw across the guarded lifecycle boundary",
        adapter,
        call);
    out.adapter_result = VR_ADAPTER_ERROR_FATAL;
    out.adapter_disabled = true;
    logCall(out, logger);
    return out;
  }

  PluginHostStatus status = PluginHostStatus::Loaded;
  std::string reason = "adapter_call_ok";
  std::string message = "adapter lifecycle call succeeded";
  if (adapter_result == VR_ADAPTER_ERROR_RECOVERABLE ||
      adapter_result == VR_ADAPTER_ERROR_FATAL) {
    disable(adapter, adapter_result == VR_ADAPTER_ERROR_FATAL
        ? "adapter_fatal_error"
        : "adapter_recoverable_error");
    status = PluginHostStatus::AdapterError;
    reason = adapter.disabled_reason;
    message = "adapter lifecycle call failed and the adapter was disabled";
  } else if (adapter_result != VR_ADAPTER_OK && adapter_result != VR_ADAPTER_SKIPPED) {
    status = PluginHostStatus::AdapterError;
    reason = "adapter_call_failed";
    message = "adapter lifecycle call failed";
  }

  auto out = callResult(status, reason, message, adapter, call);
  out.adapter_result = adapter_result;
  out.adapter_disabled = adapter.disabled;
  logCall(out, logger);
  return out;
}

PluginCallResult dispatchFrameTick(
    AdapterInstance& adapter,
    const VrAdapterContext& context,
    const VrAdapterFrameInfo& frame,
    diagnostics::AsyncLogger* logger) {
  if (adapter.disabled) {
    auto out = callResult(
        PluginHostStatus::Disabled,
        adapter.disabled_reason.empty() ? "adapter_disabled" : adapter.disabled_reason,
        "adapter is disabled",
        adapter,
        AdapterLifecycleCall::Tick);
    logCall(out, logger);
    return out;
  }
  if (adapter.adapter == nullptr || adapter.adapter->tick == nullptr) {
    auto out = callResult(
        PluginHostStatus::InvalidRequest,
        "missing_tick_callback",
        "adapter has no frame tick callback",
        adapter,
        AdapterLifecycleCall::Tick);
    logCall(out, logger);
    return out;
  }

  const auto start = Clock::now();
  VrAdapterResult adapter_result = VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  adapter.active_callbacks += 1;
  try {
    adapter_result = adapter.adapter->tick(adapter.adapter, &context, &frame);
  } catch (...) {
    adapter.active_callbacks -= 1;
    disable(adapter, "adapter_exception");
    auto out = callResult(
        PluginHostStatus::AdapterError,
        "adapter_exception",
        "adapter threw across the guarded frame callback boundary",
        adapter,
        AdapterLifecycleCall::Tick);
    out.adapter_result = VR_ADAPTER_ERROR_FATAL;
    out.adapter_disabled = true;
    logCall(out, logger);
    return out;
  }
  adapter.active_callbacks -= 1;
  const std::uint64_t duration = elapsedMicros(start, Clock::now());

  if (adapter_result == VR_ADAPTER_ERROR_RECOVERABLE ||
      adapter_result == VR_ADAPTER_ERROR_FATAL) {
    disable(adapter, adapter_result == VR_ADAPTER_ERROR_FATAL
        ? "adapter_fatal_error"
        : "adapter_recoverable_error");
    auto out = callResult(
        PluginHostStatus::AdapterError,
        adapter.disabled_reason,
        "adapter frame callback failed and the adapter was disabled",
        adapter,
        AdapterLifecycleCall::Tick);
    out.duration_us = duration;
    out.adapter_result = adapter_result;
    out.adapter_disabled = true;
    logCall(out, logger);
    return out;
  }

  if (duration > adapter.frame_callback_budget_us) {
    adapter.overrun_count += 1;
    const bool disable_now = adapter.overrun_count >= adapter.overrun_disable_threshold;
    if (disable_now) {
      disable(adapter, "callback_overrun_threshold");
    }
    auto out = callResult(
        PluginHostStatus::CallbackOverrun,
        disable_now ? "callback_overrun_threshold" : "callback_overrun",
        "adapter frame callback exceeded the configured timing budget",
        adapter,
        AdapterLifecycleCall::Tick);
    out.duration_us = duration;
    out.adapter_result = adapter_result;
    out.adapter_disabled = adapter.disabled;
    out.diagnostics_fields.push_back({"duration_us", std::to_string(duration)});
    out.diagnostics_fields.push_back(
        {"budget_us", std::to_string(adapter.frame_callback_budget_us)});
    out.diagnostics_fields.push_back(
        {"overrun_count", std::to_string(adapter.overrun_count)});
    logCall(out, logger);
    return out;
  }

  auto out = callResult(
      PluginHostStatus::Loaded,
      "adapter_frame_ok",
      "adapter frame callback completed within budget",
      adapter,
      AdapterLifecycleCall::Tick);
  out.duration_us = duration;
  out.adapter_result = adapter_result;
  out.diagnostics_fields.push_back({"duration_us", std::to_string(duration)});
  logCall(out, logger);
  return out;
}

PluginCallResult requestHotReload(
    AdapterInstance& adapter,
    const ReloadState& state,
    diagnostics::AsyncLogger* logger) {
  const bool active = state.active_callbacks || adapter.active_callbacks > 0;
  const bool unsafe = active || state.hooks_installed || state.owned_worker_threads ||
      state.static_teardown_risk || state.target_teardown_active;
  if (unsafe) {
    auto out = callResult(
        PluginHostStatus::ReloadRefused,
        "hot_reload_unsafe_state",
        "hot reload refused because adapter state cannot be unloaded safely",
        adapter,
        AdapterLifecycleCall::ReloadConfig);
    out.diagnostics_fields.push_back({"active_callbacks", boolText(active)});
    out.diagnostics_fields.push_back({"hooks_installed", boolText(state.hooks_installed)});
    out.diagnostics_fields.push_back({"owned_worker_threads", boolText(state.owned_worker_threads)});
    out.diagnostics_fields.push_back({"target_teardown_active", boolText(state.target_teardown_active)});
    logCall(out, logger);
    return out;
  }
  auto out = callResult(
      PluginHostStatus::Loaded,
      "hot_reload_allowed",
      "hot reload allowed for the adapter",
      adapter,
      AdapterLifecycleCall::ReloadConfig);
  logCall(out, logger);
  return out;
}

PluginCallResult fastRestart(
    AdapterInstance& adapter,
    diagnostics::AsyncLogger* logger) {
  auto out = callResult(
      PluginHostStatus::Loaded,
      "fast_restart_preserves_diagnostics",
      "fast restart will preserve adapter diagnostics context",
      adapter,
      AdapterLifecycleCall::ReloadConfig);
  out.diagnostics_fields.push_back({"adapter_id", adapter.adapter_id});
  out.diagnostics_fields.push_back({"plugin_file_token", adapter.plugin_file_token});
  logCall(out, logger);
  return out;
}

void unloadPlugin(AdapterInstance& adapter) {
  if (adapter.adapter != nullptr && adapter.exports.destroy_adapter != nullptr) {
    adapter.exports.destroy_adapter(adapter.adapter);
  }
  adapter.adapter = nullptr;
  adapter.library_handle.reset();
  adapter.disabled = true;
  adapter.disabled_reason = "adapter_unloaded";
}

}  // namespace vrclient::plugins::host

#pragma once

#include "diagnostics/logging/diagnostic_logger.h"
#include "plugins/sdk/adapter_context.h"
#include "plugins/sdk/adapter_metadata.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace vrclient::plugins::host {

enum class PluginHostStatus {
  Loaded,
  Disabled,
  MissingMetadata,
  MissingExport,
  AbiMismatch,
  IncompatibleApi,
  WrongTarget,
  UnsafePath,
  CreateFailed,
  AdapterError,
  CallbackOverrun,
  ReloadRefused,
  InvalidRequest
};

enum class AdapterLifecycleCall {
  Validate,
  Init,
  Tick,
  Suspend,
  Resume,
  ReloadConfig,
  Shutdown
};

struct PluginExports {
  VrClientGetAdapterAbiFn get_abi = nullptr;
  VrClientGetAdapterMetadataFn get_metadata = nullptr;
  VrClientCreateAdapterFn create_adapter = nullptr;
  VrClientDestroyAdapterFn destroy_adapter = nullptr;
};

struct PluginLoadRequest {
  std::filesystem::path plugin_path;
  std::string target_game_id;
  std::string target_build_id;
  PluginExports exports;
  std::vector<VrAdapterService> services;
  VrAdapterApiVersion host_api_version = sdk::kHostApiVersion;
  std::uint64_t frame_callback_budget_us = 11000;
  std::uint32_t overrun_disable_threshold = 3;
  bool plugin_file_exists = true;
};

struct AdapterInstance {
  std::filesystem::path plugin_path;
  std::string plugin_file_token;
  std::string adapter_id;
  std::string adapter_version;
  std::string target_game_id;
  std::string target_build_id;
  PluginExports exports;
  std::shared_ptr<void> library_handle;
  std::shared_ptr<std::vector<VrAdapterService>> service_table;
  const VrAdapterMetadata* metadata = nullptr;
  VrGameAdapter* adapter = nullptr;
  VrAdapterHostServices host_services{};
  std::uint64_t frame_callback_budget_us = 11000;
  std::uint32_t overrun_disable_threshold = 3;
  std::uint32_t overrun_count = 0;
  std::uint32_t active_callbacks = 0;
  bool disabled = false;
  std::string disabled_reason;
};

struct PluginHostResult {
  PluginHostStatus status = PluginHostStatus::InvalidRequest;
  std::string reason_code;
  std::string message;
  AdapterInstance adapter;
  std::vector<diagnostics::LogField> diagnostics_fields;
};

struct PluginCallResult {
  PluginHostStatus status = PluginHostStatus::InvalidRequest;
  std::string reason_code;
  std::string message;
  VrAdapterResult adapter_result = VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  std::uint64_t duration_us = 0;
  bool adapter_disabled = false;
  std::vector<diagnostics::LogField> diagnostics_fields;
};

struct ReloadState {
  bool active_callbacks = false;
  bool hooks_installed = false;
  bool owned_worker_threads = false;
  bool static_teardown_risk = false;
  bool target_teardown_active = false;
};

const char* pluginHostStatusName(PluginHostStatus status);
const char* lifecycleCallName(AdapterLifecycleCall call);

PluginHostResult loadPlugin(
    const PluginLoadRequest& request,
    diagnostics::AsyncLogger* logger = nullptr);

PluginCallResult dispatchLifecycle(
    AdapterInstance& adapter,
    AdapterLifecycleCall call,
    const VrAdapterContext& context,
    diagnostics::AsyncLogger* logger = nullptr);

PluginCallResult dispatchFrameTick(
    AdapterInstance& adapter,
    const VrAdapterContext& context,
    const VrAdapterFrameInfo& frame,
    diagnostics::AsyncLogger* logger = nullptr);

PluginCallResult requestHotReload(
    AdapterInstance& adapter,
    const ReloadState& state,
    diagnostics::AsyncLogger* logger = nullptr);

PluginCallResult fastRestart(
    AdapterInstance& adapter,
    diagnostics::AsyncLogger* logger = nullptr);

void unloadPlugin(AdapterInstance& adapter);

}  // namespace vrclient::plugins::host

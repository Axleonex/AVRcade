#include "vr_runtime_api.h"

#include "diagnostics/logging/diagnostic_logger.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <string>
#include <thread>

namespace {

std::atomic_bool g_stop_requested{false};

void handleSignal(int) {
  g_stop_requested.store(true);
}

const char* stateName(VrRuntimeState state) {
  switch (state) {
    case VR_RUNTIME_STATE_STOPPED:
      return "stopped";
    case VR_RUNTIME_STATE_INITIALIZING:
      return "initializing";
    case VR_RUNTIME_STATE_READY:
      return "ready";
    case VR_RUNTIME_STATE_RUNNING:
      return "running";
    case VR_RUNTIME_STATE_DEGRADED:
      return "degraded";
    case VR_RUNTIME_STATE_LOSS_PENDING:
      return "loss_pending";
    case VR_RUNTIME_STATE_EXITING:
      return "exiting";
    case VR_RUNTIME_STATE_ERROR:
      return "error";
  }
  return "unknown";
}

void onStateChanged(
    void* user_data,
    VrRuntimeState previous,
    VrRuntimeState next,
    int32_t detail_code) {
  auto* logger = static_cast<vrclient::diagnostics::AsyncLogger*>(user_data);
  if (logger == nullptr) {
    return;
  }
  logger->log(vrclient::diagnostics::Severity::Info, "harness_runtime_state", {
      {"previous", stateName(previous)},
      {"next", stateName(next)},
      {"detail_code", std::to_string(detail_code)},
  });
}

}  // namespace

int main(int argc, char** argv) {
  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);

  vrclient::diagnostics::SessionMetadata harness_metadata;
  harness_metadata.session_id = vrclient::diagnostics::makeSessionId();
  harness_metadata.runtime_version = "0.1.0";
  harness_metadata.launch_path =
      argc > 0 && argv[0] != nullptr ? argv[0] : "vr_runtime_harness";

  vrclient::diagnostics::LoggerConfig harness_log_config;
  harness_log_config.file_prefix = "vrclient-harness";

  vrclient::diagnostics::AsyncLogger harness_logger;
  harness_logger.start(harness_log_config, harness_metadata);
  harness_logger.log(vrclient::diagnostics::Severity::Info, "harness_started");

  const char* profile_path = "config/defaults/runtime-profile.json";
  if (argc > 1) {
    profile_path = argv[1];
  }

  VrRuntimeDesc desc{};
  desc.size = sizeof(VrRuntimeDesc);
  desc.application_name = "VR Conversion Manager Harness";
  desc.runtime_profile_path = profile_path;
  desc.preferred_graphics_backend = VR_RUNTIME_GRAPHICS_BACKEND_VULKAN;
  desc.state_callback = onStateChanged;
  desc.state_callback_user_data = &harness_logger;

  VrRuntime* runtime = nullptr;
  VrRuntimeResult result = vr_runtime_create(&desc, &runtime);
  if (result != VR_RUNTIME_OK) {
    harness_logger.log(vrclient::diagnostics::Severity::Error, "harness_create_failed", {
        {"result", vr_runtime_result_name(result)},
    });
    return 1;
  }

  result = vr_runtime_start(runtime);
  if (result != VR_RUNTIME_OK) {
    harness_logger.log(vrclient::diagnostics::Severity::Error, "harness_start_failed", {
        {"result", vr_runtime_result_name(result)},
    });
    vr_runtime_destroy(runtime);
    return 1;
  }

  while (!g_stop_requested.load()) {
    result = vr_runtime_run_frame(runtime, nullptr, nullptr);
    if (result == VR_RUNTIME_SKIPPED) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    if (result != VR_RUNTIME_OK) {
      harness_logger.log(vrclient::diagnostics::Severity::Error, "harness_frame_failed", {
          {"result", vr_runtime_result_name(result)},
      });
      break;
    }

    VrRuntimeFrameData frame_data{};
    result = vr_runtime_get_frame_data(runtime, &frame_data);
    if (result != VR_RUNTIME_OK) {
      harness_logger.log(vrclient::diagnostics::Severity::Error, "harness_frame_data_failed", {
          {"result", vr_runtime_result_name(result)},
      });
      break;
    }

    const VrRuntimeState state = vr_runtime_get_state(runtime);
    if (state == VR_RUNTIME_STATE_EXITING ||
        state == VR_RUNTIME_STATE_LOSS_PENDING ||
        state == VR_RUNTIME_STATE_ERROR) {
      break;
    }
  }

  vr_runtime_stop(runtime);
  vr_runtime_destroy(runtime);
  harness_logger.log(vrclient::diagnostics::Severity::Info, "harness_stopped", {
      {"result", vr_runtime_result_name(result)},
  });
  harness_logger.stop();
  return result == VR_RUNTIME_OK || result == VR_RUNTIME_SKIPPED ? 0 : 1;
}

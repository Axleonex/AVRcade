#include <RED4ext/Common.hpp>
#include <RED4ext/Api/ApiVersion.hpp>
#include <RED4ext/Api/v1/EMainReason.hpp>
#include <RED4ext/Api/v1/PluginHandle.hpp>
#include <RED4ext/Api/v1/PluginInfo.hpp>
#include <RED4ext/Api/v1/Sdk.hpp>
#include <RED4ext/Api/v1/Version.hpp>

#include "adapters/redengine/redengine_graphics_hooks.h"
#if defined(VRCLIENT_REDENGINE_OPENXR)
#include "adapters/redengine/redengine_openxr_bridge.h"
#endif

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

constexpr wchar_t kPluginName[] = L"VRClient.REDengine";
constexpr wchar_t kPluginAuthor[] = L"AVRcade";
constexpr RED4ext::v1::FileVer kSupportedRuntime{3, 0, 80, 51928};

bool sameProductVersion(const RED4ext::v1::SemVer& runtime) {
  // CDPR's displayed patch 2.31 is encoded in the fixed product-version
  // resource (and therefore RED4ext's SemVer) as 2.3.1.
  return runtime.major == 2 && runtime.minor == 3 && runtime.patch == 1;
}

void logInfo(
    const RED4ext::v1::Sdk* sdk,
    RED4ext::v1::PluginHandle handle,
    const char* message) {
  if (sdk != nullptr && sdk->logger != nullptr && sdk->logger->Info != nullptr) {
    sdk->logger->Info(handle, message);
  }
}

void logError(
    const RED4ext::v1::Sdk* sdk,
    RED4ext::v1::PluginHandle handle,
    const char* message) {
  if (sdk != nullptr && sdk->logger != nullptr && sdk->logger->Error != nullptr) {
    sdk->logger->Error(handle, message);
  }
}

const char* graphicsHookResultMessage(
    vrclient::adapters::redengine::GraphicsHookResult result) {
  using vrclient::adapters::redengine::GraphicsHookResult;
  switch (result) {
    case GraphicsHookResult::Installed:
      return "VRClient REDengine D3D12 observer installed (pass-through mode)";
    case GraphicsHookResult::AlreadyInstalled:
      return "VRClient REDengine D3D12 observer was already installed";
    case GraphicsHookResult::ModuleUnavailable:
      return "VRClient REDengine D3D12 observer could not load a factory module";
    case GraphicsHookResult::ExportUnavailable:
      return "VRClient REDengine D3D12 observer could not find factory exports";
    case GraphicsHookResult::HookFailed:
      return "VRClient REDengine D3D12 observer hook transaction failed";
  }
  return "VRClient REDengine D3D12 observer returned an unknown result";
}

#if defined(VRCLIENT_REDENGINE_OPENXR)
bool writeLastVrcamCapture() {
  std::vector<std::uint8_t> rgba;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  if (vrclient::adapters::redengine::readbackCapturedVrcam(
          &rgba, &width, &height) != VR_RUNTIME_OK ||
      width == 0 || height == 0 ||
      rgba.size() != static_cast<std::size_t>(width) * height * 4) {
    return false;
  }
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCWSTR>(&writeLastVrcamCapture), &module)) {
    return false;
  }
  wchar_t module_path[32768]{};
  const DWORD length =
      GetModuleFileNameW(module, module_path, std::size(module_path));
  if (length == 0 || length >= std::size(module_path)) {
    return false;
  }
  std::filesystem::path output(module_path);
  output.replace_filename(L"vrcam-last-capture.ppm");
  std::ofstream stream(output, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return false;
  }
  stream << "P6\n" << width << ' ' << height << "\n255\n";
  for (std::size_t pixel = 0; pixel < rgba.size(); pixel += 4) {
    stream.write(
        reinterpret_cast<const char*>(rgba.data() + pixel), 3);
  }
  return stream.good();
}
#endif

}  // namespace

RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports() {
  return RED4EXT_API_VERSION_1;
}

RED4EXT_C_EXPORT void RED4EXT_CALL Query(RED4ext::v1::PluginInfo* info) {
  if (info == nullptr) {
    return;
  }
  info->name = kPluginName;
  info->author = kPluginAuthor;
#if defined(VRCLIENT_REDENGINE_OPENXR)
  info->version = RED4EXT_V1_SEMVER(0, 6, 6);
#else
  info->version = RED4EXT_V1_SEMVER(0, 5, 0);
#endif
  info->runtime = kSupportedRuntime;
  info->sdk = RED4EXT_V1_SDK_VERSION_CURRENT;
}

RED4EXT_C_EXPORT bool RED4EXT_CALL Main(
    RED4ext::v1::PluginHandle handle,
    RED4ext::v1::EMainReason reason,
    const RED4ext::v1::Sdk* sdk) {
  if (sdk == nullptr || sdk->runtime == nullptr ||
      !sameProductVersion(*sdk->runtime)) {
    logError(
        sdk,
        handle,
        "VRClient REDengine bridge refused an unsupported Cyberpunk runtime");
    return false;
  }

  switch (reason) {
    case RED4ext::v1::EMainReason::Load:
      {
        const auto hook_result =
            vrclient::adapters::redengine::startGraphicsObservation();
        if (hook_result !=
                vrclient::adapters::redengine::GraphicsHookResult::Installed &&
            hook_result != vrclient::adapters::redengine::GraphicsHookResult::AlreadyInstalled) {
          logError(sdk, handle, graphicsHookResultMessage(hook_result));
          return false;
        }
        logInfo(sdk, handle, graphicsHookResultMessage(hook_result));
      }
      logInfo(
          sdk,
          handle,
          "VRClient REDengine bridge loaded; build=steam-1091500-build-20383525");
#if defined(VRCLIENT_REDENGINE_OPENXR)
      if (!vrclient::adapters::redengine::startRedengineOpenXrBridge()) {
        logError(sdk, handle, "VRClient REDengine OpenXR worker failed to start");
        vrclient::adapters::redengine::stopGraphicsObservation();
        return false;
      }
      logInfo(sdk, handle, "VRClient REDengine OpenXR worker started");
#endif
      break;
    case RED4ext::v1::EMainReason::Unload:
      {
#if defined(VRCLIENT_REDENGINE_OPENXR)
        vrclient::adapters::redengine::stopRedengineOpenXrBridge();
        const auto openxr_status =
            vrclient::adapters::redengine::queryRedengineOpenXrStatus();
        const bool vrcam_dump_ready = writeLastVrcamCapture();
#endif
        VrRedengineGraphicsSnapshot snapshot{};
        snapshot.size = sizeof(snapshot);
        vrclient::adapters::redengine::queryGraphicsObservation(&snapshot);
        char evidence[6144]{};
        const int summary_length = std::snprintf(
            evidence,
            sizeof(evidence),
            "VRClient REDengine graphics observation; ready=%u width=%u height=%u format=%u buffers=%u presents=%llu resizes=%llu candidate=%ux%u candidate_resources=%llu candidate_color=%p candidate_color_format=%u candidate_color_resources=%llu candidate_color_rtvs=%llu vrcam_ready=%u vrcam_color=%p vrcam_format=%u vrcam_candidate_index=%u vrcam_binds=%llu vrcam_shader_read_transitions=%llu vrcam_source_state=%u vrcam_capture_ready=%u vrcam_captures=%llu vrcam_capture_failures=%llu candidate_unorm_resources=%llu candidate_unorm_rtv_resources=%llu candidate_unorm_rtvs=%llu candidate_unorm_bound_resources=%llu candidate_unorm_binds=%llu candidate_unorm_cleared_resources=%llu candidate_unorm_clears=%llu candidate_unorm_shader_read_resources=%llu candidate_unorm_shader_read_transitions=%llu candidate_unorm_copy_source_resources=%llu candidate_unorm_copy_sources=%llu candidate_unorm_bind_counts=[",
            snapshot.ready,
            snapshot.width,
            snapshot.height,
            snapshot.format,
            snapshot.buffer_count,
            static_cast<unsigned long long>(snapshot.present_count),
            static_cast<unsigned long long>(snapshot.resize_count),
            snapshot.candidate_view_width,
            snapshot.candidate_view_height,
            static_cast<unsigned long long>(
                snapshot.candidate_view_resource_count),
            snapshot.candidate_color_resource,
            snapshot.candidate_color_format,
            static_cast<unsigned long long>(
                snapshot.candidate_color_resource_count),
            static_cast<unsigned long long>(
                snapshot.candidate_color_rtv_count),
            snapshot.vrcam_ready,
            snapshot.vrcam_color_resource,
            snapshot.vrcam_color_format,
            snapshot.vrcam_candidate_index,
            static_cast<unsigned long long>(snapshot.vrcam_bind_count),
            static_cast<unsigned long long>(
                snapshot.vrcam_shader_read_transition_count),
            snapshot.vrcam_source_state,
            snapshot.vrcam_capture_ready,
            static_cast<unsigned long long>(snapshot.vrcam_capture_count),
            static_cast<unsigned long long>(
                snapshot.vrcam_capture_fail_count),
            static_cast<unsigned long long>(
                snapshot.candidate_unorm_resource_count),
            static_cast<unsigned long long>(
                snapshot.candidate_unorm_rtv_resource_count),
            static_cast<unsigned long long>(
                snapshot.candidate_unorm_rtv_count),
            static_cast<unsigned long long>(
                snapshot.candidate_unorm_bound_resource_count),
            static_cast<unsigned long long>(
                snapshot.candidate_unorm_bind_count),
            static_cast<unsigned long long>(
                snapshot.candidate_unorm_cleared_resource_count),
            static_cast<unsigned long long>(
                snapshot.candidate_unorm_clear_count),
            static_cast<unsigned long long>(
                snapshot.candidate_unorm_shader_read_resource_count),
            static_cast<unsigned long long>(
                snapshot.candidate_unorm_shader_read_transition_count),
            static_cast<unsigned long long>(
                snapshot.candidate_unorm_copy_source_resource_count),
            static_cast<unsigned long long>(
                snapshot.candidate_unorm_copy_source_count));
        std::size_t written = summary_length > 0
            ? (std::min)(
                  static_cast<std::size_t>(summary_length),
                  sizeof(evidence) - 1)
            : 0;
        const auto append_text = [&](const char* text) {
          if (written >= sizeof(evidence) - 1) {
            return;
          }
          const int length = std::snprintf(
              evidence + written, sizeof(evidence) - written, "%s", text);
          if (length > 0) {
            written = (std::min)(
                written + static_cast<std::size_t>(length),
                sizeof(evidence) - 1);
          }
        };
#if defined(VRCLIENT_REDENGINE_OPENXR)
        const auto append_integer = [&](std::int64_t value) {
          if (written >= sizeof(evidence) - 1) {
            return;
          }
          const int length = std::snprintf(
              evidence + written,
              sizeof(evidence) - written,
              "%lld",
              static_cast<long long>(value));
          if (length > 0) {
            written = (std::min)(
                written + static_cast<std::size_t>(length),
                sizeof(evidence) - 1);
          }
        };
#endif
        const auto append_counts = [&](const std::uint64_t* counts) {
          for (std::uint32_t index = 0;
               index < snapshot.candidate_unorm_tracked_count &&
               written < sizeof(evidence) - 1;
               ++index) {
            const int length = std::snprintf(
                evidence + written,
                sizeof(evidence) - written,
                "%s%llu",
                index == 0 ? "" : ",",
                static_cast<unsigned long long>(counts[index]));
            if (length <= 0) {
              break;
            }
            written = (std::min)(
                written + static_cast<std::size_t>(length),
                sizeof(evidence) - 1);
          }
          append_text("]");
        };
        append_counts(snapshot.candidate_unorm_bind_counts);
        append_text(" candidate_unorm_clear_counts=[");
        append_counts(snapshot.candidate_unorm_clear_counts);
        append_text(" candidate_unorm_shader_read_transition_counts=[");
        append_counts(
            snapshot.candidate_unorm_shader_read_transition_counts);
        append_text(" candidate_unorm_copy_source_counts=[");
        append_counts(snapshot.candidate_unorm_copy_source_counts);
        append_text(" candidate_unorm_names=[");
        for (std::uint32_t index = 0;
             index < snapshot.candidate_unorm_tracked_count; ++index) {
          if (index != 0) {
            append_text(",");
          }
          append_text("\"");
          append_text(snapshot.candidate_unorm_names[index]);
          append_text("\"");
        }
        append_text("]");
        append_text(" candidate_unorm_primary_queue_exec_counts=[");
        append_counts(
            snapshot.candidate_unorm_primary_queue_execute_counts);
        append_text(" candidate_unorm_other_direct_queue_exec_counts=[");
        append_counts(
            snapshot.candidate_unorm_other_direct_queue_execute_counts);
        append_text(" candidate_unorm_compute_queue_exec_counts=[");
        append_counts(
            snapshot.candidate_unorm_compute_queue_execute_counts);
        append_text(" candidate_unorm_copy_queue_exec_counts=[");
        append_counts(
            snapshot.candidate_unorm_copy_queue_execute_counts);
#if defined(VRCLIENT_REDENGINE_OPENXR)
        append_text(" openxr_worker_started=");
        append_integer(openxr_status.worker_started ? 1 : 0);
        append_text(" openxr_start_result=");
        append_integer(static_cast<std::int64_t>(openxr_status.start_result));
        append_text(" openxr_frame_result=");
        append_integer(static_cast<std::int64_t>(openxr_status.frame_result));
        append_text(" openxr_frames=");
        append_integer(static_cast<std::int64_t>(openxr_status.frame_count));
        append_text(" openxr_eyes=");
        append_integer(openxr_status.eye_count);
        append_text(" openxr_orientation_valid=");
        append_integer(openxr_status.orientation_valid ? 1 : 0);
        append_text(" openxr_position_valid=");
        append_integer(openxr_status.position_valid ? 1 : 0);
        append_text(" vrcam_dump_ready=");
        append_integer(vrcam_dump_ready ? 1 : 0);
#endif
        logInfo(sdk, handle, evidence);
      }
      if (!vrclient::adapters::redengine::stopGraphicsObservation()) {
        logError(
            sdk,
            handle,
            "VRClient REDengine D3D12 observer failed to detach cleanly");
        return false;
      }
      logInfo(
          sdk,
          handle,
          "VRClient REDengine bridge unloaded; build=steam-1091500-build-20383525");
      break;
  }
  return true;
}

RED4EXT_C_EXPORT void RED4EXT_CALL vrclient_redengine_query_graphics(
    VrRedengineGraphicsSnapshot* snapshot) {
  vrclient::adapters::redengine::queryGraphicsObservation(snapshot);
}

RED4EXT_C_EXPORT void RED4EXT_CALL vrclient_redengine_request_vrcam_capture() {
  vrclient::adapters::redengine::requestVrcamCapture();
}

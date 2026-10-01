#include "diagnostics/diagnostics_system.h"

#include <array>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <unistd.h>
#endif

namespace vrclient::diagnostics {
namespace {

const char* configuredProfilePath(const char* explicit_path) {
  if (explicit_path != nullptr && explicit_path[0] != '\0') {
    return explicit_path;
  }
  if (const char* env_path = std::getenv("VRCLIENT_DIAGNOSTICS_PROFILE")) {
    if (env_path[0] != '\0') {
      return env_path;
    }
  }
  return "config/defaults/diagnostics-profile.json";
}

const char* diagnosticsResultName(VrRuntimeResult result) {
  switch (result) {
    case VR_RUNTIME_OK:
      return "VR_RUNTIME_OK";
    case VR_RUNTIME_SKIPPED:
      return "VR_RUNTIME_SKIPPED";
    case VR_RUNTIME_ERROR_INVALID_ARGUMENT:
      return "VR_RUNTIME_ERROR_INVALID_ARGUMENT";
    case VR_RUNTIME_ERROR_PROFILE:
      return "VR_RUNTIME_ERROR_PROFILE";
    case VR_RUNTIME_ERROR_VR_API:
      return "VR_RUNTIME_ERROR_VR_API";
    case VR_RUNTIME_ERROR_GRAPHICS:
      return "VR_RUNTIME_ERROR_GRAPHICS";
    case VR_RUNTIME_ERROR_STATE:
      return "VR_RUNTIME_ERROR_STATE";
    case VR_RUNTIME_ERROR_RUNTIME_UNAVAILABLE:
      return "VR_RUNTIME_ERROR_RUNTIME_UNAVAILABLE";
  }
  return "VR_RUNTIME_ERROR_UNKNOWN";
}

std::string currentLaunchPath(std::string_view fallback) {
#if defined(_WIN32)
  std::array<char, 4096> buffer{};
  const DWORD length = GetModuleFileNameA(
      nullptr,
      buffer.data(),
      static_cast<DWORD>(buffer.size()));
  if (length > 0 && length < buffer.size()) {
    return std::string(buffer.data(), buffer.data() + length);
  }
#else
  std::array<char, 4096> buffer{};
  const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
  if (length > 0) {
    return std::string(buffer.data(), buffer.data() + length);
  }
#endif
  return fallback.empty() ? "unknown" : std::string(fallback);
}

CrashArtifactMetadata crashMetadataFromSession(
    const SessionMetadata& metadata,
    std::string reason = "runtime_snapshot",
    std::int32_t exception_code = 0) {
  CrashArtifactMetadata crash_metadata;
  crash_metadata.reason = std::move(reason);
  crash_metadata.exception_code = exception_code;
  crash_metadata.runtime_state = metadata.headset_state.runtime_state;
  crash_metadata.game_id = metadata.game_id;
  crash_metadata.build_id = metadata.build_id;
  crash_metadata.adapter_id = metadata.adapter_id;
  crash_metadata.launch_path = metadata.launch_path;
  return crash_metadata;
}

}  // namespace

DiagnosticsSystem::~DiagnosticsSystem() {
  shutdown();
}

bool DiagnosticsSystem::initialize(
    std::string application_name,
    const char* diagnostics_profile_path) {
  shutdown();

  const auto loaded = loadDiagnosticsProfileFromFile(
      configuredProfilePath(diagnostics_profile_path));
  profile_ = loaded.profile;

  metadata_.session_id = makeSessionId();
  metadata_.runtime_version = "0.1.0";
  metadata_.game_id = "none";
  metadata_.build_id = "unknown";
  metadata_.adapter_id = "none";
  metadata_.launch_path = currentLaunchPath(application_name);

  profile_.logging.file_prefix = application_name.empty()
      ? "vrclient-runtime"
      : "vrclient-runtime";
  profile_.crash.session_id = metadata_.session_id;
  profile_.export_request.session_id = metadata_.session_id;

  LoggerConfig logging_config = profile_.logging;
  if (!profile_.enabled) {
    logging_config.enabled = false;
    profile_.overlay_enabled = false;
  }

  const bool logger_started = logger_.start(logging_config, metadata_);
  overlay_.setEnabled(profile_.overlay_enabled);
  crash_capture_.updateMetadata(crashMetadataFromSession(metadata_, "diagnostics_initialized"));
  const bool crash_installed =
      crash_capture_.install(profile_.crash, logger_.enabled() ? &logger_ : nullptr);

  if (logger_.enabled()) {
    logger_.log(Severity::Info, "diagnostics_started", {
        {"profile_loaded", loaded.loaded ? "true" : "false"},
        {"profile_message", loaded.message},
        {"crash_capture", crash_installed ? "installed" : "unavailable"},
    });
  }

  initialized_ = true;
  return logger_started && crash_installed;
}

void DiagnosticsSystem::shutdown() {
  if (!initialized_) {
    return;
  }
  if (logger_.enabled()) {
    logger_.log(Severity::Info, "diagnostics_stopping");
  }
  crash_capture_.uninstall();
  logger_.stop();
  initialized_ = false;
}

void DiagnosticsSystem::setRuntimeIds(
    std::string game_id,
    std::string build_id,
    std::string adapter_id) {
  metadata_.game_id = std::move(game_id);
  metadata_.build_id = std::move(build_id);
  metadata_.adapter_id = std::move(adapter_id);
  syncMetadata();
}

void DiagnosticsSystem::updateHeadsetState(
    const VrRuntimeHeadsetState& headset_state) {
  metadata_.headset_state = headset_state;
  logger_.updateHeadsetState(headset_state);
  crash_capture_.updateMetadata(crashMetadataFromSession(metadata_));
}

void DiagnosticsSystem::updateFrameSnapshot(
    const VrRuntimeFrameData& frame,
    const VrRuntimeHeadsetState& headset_state) {
  updateHeadsetState(headset_state);
  overlay_.updateFromFrame(
      frame,
      headset_state,
      metadata_.game_id,
      metadata_.build_id,
      metadata_.adapter_id);
}

void DiagnosticsSystem::logStateTransition(
    VrRuntimeState previous,
    VrRuntimeState next,
    int32_t detail_code) {
  logger_.log(Severity::Info, "runtime_state_transition", {
      {"previous", runtimeStateName(previous)},
      {"next", runtimeStateName(next)},
      {"detail_code", std::to_string(detail_code)},
  });
}

void DiagnosticsSystem::logRuntimeEvent(
    Severity severity,
    std::string_view event_name) {
  logger_.log(severity, event_name);
}

void DiagnosticsSystem::logRuntimeError(
    std::string_view event_name,
    VrRuntimeResult result,
    int32_t detail_code) {
  logger_.log(Severity::Error, event_name, {
      {"result", diagnosticsResultName(result)},
      {"detail_code", std::to_string(detail_code)},
  });
  overlay_.recordWarning("error", std::string(event_name));
}

void DiagnosticsSystem::syncMetadata() {
  logger_.updateSessionMetadata(metadata_);
  crash_capture_.updateMetadata(crashMetadataFromSession(metadata_));
}

}  // namespace vrclient::diagnostics

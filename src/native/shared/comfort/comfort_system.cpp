#include "shared/comfort/comfort_system.h"

namespace vrclient::shared::comfort {

VrClientComfortSettings makeComfortSettings(const ComfortProfile& profile) {
  ComfortProfile clamped = profile;
  clampComfortProfile(&clamped);
  VrClientComfortSettings settings{};
  settings.size = sizeof(VrClientComfortSettings);
  settings.version = VRCLIENT_SHARED_SERVICE_VERSION;
  settings.snap_turn_enabled = clamped.snap_turn_enabled ? 1u : 0u;
  settings.smooth_turn_enabled = clamped.smooth_turn_enabled ? 1u : 0u;
  settings.vignette_enabled = clamped.vignette_enabled ? 1u : 0u;
  settings.seated_mode = clamped.seated_mode ? 1u : 0u;
  settings.snap_turn_degrees = clamped.snap_turn_degrees;
  settings.smooth_turn_degrees_per_second = clamped.smooth_turn_degrees_per_second;
  settings.vignette_strength = clamped.vignette_strength;
  settings.world_scale = clamped.world_scale;
  settings.height_offset_m = clamped.height_offset_m;
  return settings;
}

bool validateComfortProfile(
    const ComfortProfile& profile,
    std::vector<std::string>* issues) {
  ComfortProfile clamped = profile;
  bool changed = false;
  clampComfortProfile(&clamped, &changed);
  if (changed && issues != nullptr) {
    issues->push_back("comfort profile values required clamping");
  }
  return true;
}

ComfortServiceRuntime::ComfortServiceRuntime() {
  updateFromProfile(defaultGameProfile().comfort);
}

ComfortServiceRuntime::ComfortServiceRuntime(const ComfortProfile& profile) {
  updateFromProfile(profile);
}

bool ComfortServiceRuntime::updateFromProfile(
    const ComfortProfile& profile,
    std::vector<std::string>* issues) {
  validateComfortProfile(profile, issues);
  settings_ = makeComfortSettings(profile);
  service_.size = sizeof(VrClientComfortService);
  service_.version = VRCLIENT_SHARED_SERVICE_VERSION;
  service_.snapshot = &ComfortServiceRuntime::snapshotCallback;
  service_.user_data = this;
  return true;
}

bool ComfortServiceRuntime::updateRuntimeSettings(
    const VrClientComfortSettings& settings) {
  if (settings.size < sizeof(VrClientComfortSettings) ||
      settings.version != VRCLIENT_SHARED_SERVICE_VERSION) {
    return false;
  }
  ComfortProfile profile;
  profile.snap_turn_enabled = settings.snap_turn_enabled != 0;
  profile.smooth_turn_enabled = settings.smooth_turn_enabled != 0;
  profile.vignette_enabled = settings.vignette_enabled != 0;
  profile.seated_mode = settings.seated_mode != 0;
  profile.snap_turn_degrees = settings.snap_turn_degrees;
  profile.smooth_turn_degrees_per_second = settings.smooth_turn_degrees_per_second;
  profile.vignette_strength = settings.vignette_strength;
  profile.world_scale = settings.world_scale;
  profile.height_offset_m = settings.height_offset_m;
  settings_ = makeComfortSettings(profile);
  return true;
}

VrAdapterService ComfortServiceRuntime::adapterService() {
  return {
      sizeof(VrAdapterService),
      VRCLIENT_ADAPTER_SERVICE_COMFORT,
      VRCLIENT_SHARED_SERVICE_VERSION,
      &service_,
  };
}

VrAdapterResult VRCLIENT_ADAPTER_CALL ComfortServiceRuntime::snapshotCallback(
    void* user_data,
    VrClientComfortSettings* out_settings) {
  if (user_data == nullptr || out_settings == nullptr) {
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  }
  auto* runtime = static_cast<ComfortServiceRuntime*>(user_data);
  *out_settings = runtime->settings_;
  return VR_ADAPTER_OK;
}

}  // namespace vrclient::shared::comfort

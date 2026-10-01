#pragma once

#include "public/vr_runtime_api.h"

#include <string>

namespace vrclient::runtime::perf {

enum class RefreshMode {
  SystemDefault,
  Hz72,
  Hz80,
  Hz90,
  Hz120,
  Hz144
};

struct DynamicResolutionProfile {
  bool enabled = true;
  float min_scale = 0.7f;
  float max_scale = 1.0f;
  float initial_scale = 0.85f;
  float step_scale = 0.05f;
  float frame_time_budget_ms = 11.1f;
};

struct FoveationProfile {
  bool enabled = true;
  VrRuntimeFoveationPreset preset = VR_RUNTIME_FOVEATION_MEDIUM;
  float inner_radius = 0.45f;
  float outer_radius = 0.85f;
};

struct FramePacingProfile {
  int max_missed_frames = 2;
  float cpu_guard_ms = 1.0f;
  float gpu_guard_ms = 1.5f;
  int recovery_frames = 45;
};

struct DebugValidationProfile {
  bool openxr = false;
  bool renderdoc_markers = false;
  bool assert_on_contract_violation = false;
};

struct LifecycleProfile {
  int startup_timeout_ms = 15000;
  int loss_recovery_timeout_ms = 30000;
};

struct RuntimeProfile {
  int version = 1;
  RefreshMode target_refresh_mode = RefreshMode::SystemDefault;
  DynamicResolutionProfile dynamic_resolution;
  FoveationProfile foveation;
  FramePacingProfile frame_pacing;
  DebugValidationProfile debug_validation;
  LifecycleProfile lifecycle;
};

struct RuntimeProfileLoadResult {
  VrRuntimeResult result = VR_RUNTIME_OK;
  RuntimeProfile profile;
  std::string message;
};

RuntimeProfile defaultRuntimeProfile();
RuntimeProfileLoadResult loadRuntimeProfileFromFile(const char* path);

const char* refreshModeName(RefreshMode mode);
const char* foveationPresetName(VrRuntimeFoveationPreset preset);
int refreshModeHz(RefreshMode mode);

}  // namespace vrclient::runtime::perf

#include "perf/runtime_profile.h"

#include <charconv>
#include <fstream>
#include <iterator>
#include <optional>
#include <string_view>
#include <system_error>

namespace vrclient::runtime::perf {
namespace {

std::optional<std::string_view> findObject(std::string_view json, std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\"";
  const size_t key_pos = json.find(needle);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }

  const size_t open = json.find('{', key_pos + needle.size());
  if (open == std::string_view::npos) {
    return std::nullopt;
  }

  int depth = 0;
  for (size_t i = open; i < json.size(); ++i) {
    if (json[i] == '{') {
      ++depth;
    } else if (json[i] == '}') {
      --depth;
      if (depth == 0) {
        return json.substr(open, i - open + 1);
      }
    }
  }

  return std::nullopt;
}

std::optional<std::string_view> findValueToken(std::string_view json, std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\"";
  const size_t key_pos = json.find(needle);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }

  const size_t colon = json.find(':', key_pos + needle.size());
  if (colon == std::string_view::npos) {
    return std::nullopt;
  }

  size_t value_start = colon + 1;
  while (value_start < json.size() &&
         (json[value_start] == ' ' || json[value_start] == '\t' ||
          json[value_start] == '\r' || json[value_start] == '\n')) {
    ++value_start;
  }

  if (value_start >= json.size()) {
    return std::nullopt;
  }

  if (json[value_start] == '"') {
    const size_t close = json.find('"', value_start + 1);
    if (close == std::string_view::npos) {
      return std::nullopt;
    }
    return json.substr(value_start + 1, close - value_start - 1);
  }

  size_t value_end = value_start;
  while (value_end < json.size() && json[value_end] != ',' &&
         json[value_end] != '}' && json[value_end] != '\r' &&
         json[value_end] != '\n') {
    ++value_end;
  }

  while (value_end > value_start &&
         (json[value_end - 1] == ' ' || json[value_end - 1] == '\t')) {
    --value_end;
  }

  return json.substr(value_start, value_end - value_start);
}

std::optional<bool> readBool(std::string_view json, std::string_view key) {
  const auto token = findValueToken(json, key);
  if (!token) {
    return std::nullopt;
  }
  if (*token == "true") {
    return true;
  }
  if (*token == "false") {
    return false;
  }
  return std::nullopt;
}

std::optional<int> readInt(std::string_view json, std::string_view key) {
  const auto token = findValueToken(json, key);
  if (!token) {
    return std::nullopt;
  }
  int value = 0;
  const auto* first = token->data();
  const auto* last = token->data() + token->size();
  const auto result = std::from_chars(first, last, value);
  if (result.ec != std::errc() || result.ptr != last) {
    return std::nullopt;
  }
  return value;
}

std::optional<float> readFloat(std::string_view json, std::string_view key) {
  const auto token = findValueToken(json, key);
  if (!token) {
    return std::nullopt;
  }
  float value = 0.0f;
  const auto* first = token->data();
  const auto* last = token->data() + token->size();
  const auto result = std::from_chars(first, last, value);
  if (result.ec != std::errc() || result.ptr != last) {
    return std::nullopt;
  }
  return value;
}

std::optional<RefreshMode> readRefreshMode(std::string_view json) {
  const auto token = findValueToken(json, "target_refresh_mode");
  if (!token) {
    return std::nullopt;
  }
  if (*token == "system_default") {
    return RefreshMode::SystemDefault;
  }
  if (*token == "72hz") {
    return RefreshMode::Hz72;
  }
  if (*token == "80hz") {
    return RefreshMode::Hz80;
  }
  if (*token == "90hz") {
    return RefreshMode::Hz90;
  }
  if (*token == "120hz") {
    return RefreshMode::Hz120;
  }
  if (*token == "144hz") {
    return RefreshMode::Hz144;
  }
  return std::nullopt;
}

std::optional<VrRuntimeFoveationPreset> readFoveationPreset(std::string_view json) {
  const auto token = findValueToken(json, "preset");
  if (!token) {
    return std::nullopt;
  }
  if (*token == "off") {
    return VR_RUNTIME_FOVEATION_OFF;
  }
  if (*token == "low") {
    return VR_RUNTIME_FOVEATION_LOW;
  }
  if (*token == "medium") {
    return VR_RUNTIME_FOVEATION_MEDIUM;
  }
  if (*token == "high") {
    return VR_RUNTIME_FOVEATION_HIGH;
  }
  return std::nullopt;
}

RuntimeProfileLoadResult fail(std::string message) {
  RuntimeProfileLoadResult result;
  result.result = VR_RUNTIME_ERROR_PROFILE;
  result.profile = defaultRuntimeProfile();
  result.message = std::move(message);
  return result;
}

}  // namespace

RuntimeProfile defaultRuntimeProfile() {
  return RuntimeProfile{};
}

RuntimeProfileLoadResult loadRuntimeProfileFromFile(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    RuntimeProfileLoadResult result;
    result.profile = defaultRuntimeProfile();
    return result;
  }

  std::ifstream input(path, std::ios::in | std::ios::binary);
  if (!input) {
    return fail("runtime profile could not be opened");
  }

  const std::string json((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());

  RuntimeProfile profile = defaultRuntimeProfile();

  const auto version = readInt(json, "version");
  const auto refresh = readRefreshMode(json);
  const auto dynamic = findObject(json, "dynamic_resolution");
  const auto foveation = findObject(json, "foveation");
  const auto pacing = findObject(json, "frame_pacing");
  const auto debug = findObject(json, "debug_validation");
  const auto lifecycle = findObject(json, "lifecycle");

  if (!version || !refresh || !dynamic || !foveation || !pacing || !debug || !lifecycle) {
    return fail("runtime profile is missing required fields");
  }

  profile.version = *version;
  profile.target_refresh_mode = *refresh;

  const auto dr_enabled = readBool(*dynamic, "enabled");
  const auto dr_min = readFloat(*dynamic, "min_scale");
  const auto dr_max = readFloat(*dynamic, "max_scale");
  const auto dr_initial = readFloat(*dynamic, "initial_scale");
  const auto dr_step = readFloat(*dynamic, "step_scale");
  const auto dr_budget = readFloat(*dynamic, "frame_time_budget_ms");
  if (!dr_enabled || !dr_min || !dr_max || !dr_initial || !dr_step || !dr_budget) {
    return fail("dynamic_resolution profile is incomplete");
  }

  profile.dynamic_resolution.enabled = *dr_enabled;
  profile.dynamic_resolution.min_scale = *dr_min;
  profile.dynamic_resolution.max_scale = *dr_max;
  profile.dynamic_resolution.initial_scale = *dr_initial;
  profile.dynamic_resolution.step_scale = *dr_step;
  profile.dynamic_resolution.frame_time_budget_ms = *dr_budget;

  const auto fv_enabled = readBool(*foveation, "enabled");
  const auto fv_preset = readFoveationPreset(*foveation);
  const auto fv_inner = readFloat(*foveation, "inner_radius");
  const auto fv_outer = readFloat(*foveation, "outer_radius");
  if (!fv_enabled || !fv_preset || !fv_inner || !fv_outer) {
    return fail("foveation profile is incomplete");
  }

  profile.foveation.enabled = *fv_enabled;
  profile.foveation.preset = *fv_preset;
  profile.foveation.inner_radius = *fv_inner;
  profile.foveation.outer_radius = *fv_outer;

  const auto max_missed = readInt(*pacing, "max_missed_frames");
  const auto cpu_guard = readFloat(*pacing, "cpu_guard_ms");
  const auto gpu_guard = readFloat(*pacing, "gpu_guard_ms");
  const auto recovery = readInt(*pacing, "recovery_frames");
  if (!max_missed || !cpu_guard || !gpu_guard || !recovery) {
    return fail("frame_pacing profile is incomplete");
  }

  profile.frame_pacing.max_missed_frames = *max_missed;
  profile.frame_pacing.cpu_guard_ms = *cpu_guard;
  profile.frame_pacing.gpu_guard_ms = *gpu_guard;
  profile.frame_pacing.recovery_frames = *recovery;

  const auto debug_openxr = readBool(*debug, "openxr");
  const auto debug_markers = readBool(*debug, "renderdoc_markers");
  const auto debug_asserts = readBool(*debug, "assert_on_contract_violation");
  if (!debug_openxr || !debug_markers || !debug_asserts) {
    return fail("debug_validation profile is incomplete");
  }

  profile.debug_validation.openxr = *debug_openxr;
  profile.debug_validation.renderdoc_markers = *debug_markers;
  profile.debug_validation.assert_on_contract_violation = *debug_asserts;

  const auto startup_timeout = readInt(*lifecycle, "startup_timeout_ms");
  const auto loss_timeout = readInt(*lifecycle, "loss_recovery_timeout_ms");
  if (!startup_timeout || !loss_timeout) {
    return fail("lifecycle profile is incomplete");
  }

  profile.lifecycle.startup_timeout_ms = *startup_timeout;
  profile.lifecycle.loss_recovery_timeout_ms = *loss_timeout;

  if (profile.dynamic_resolution.min_scale > profile.dynamic_resolution.initial_scale ||
      profile.dynamic_resolution.initial_scale > profile.dynamic_resolution.max_scale) {
    return fail("dynamic_resolution scale range is invalid");
  }

  if (profile.foveation.inner_radius > profile.foveation.outer_radius) {
    return fail("foveation radius range is invalid");
  }

  RuntimeProfileLoadResult result;
  result.profile = profile;
  return result;
}

const char* refreshModeName(RefreshMode mode) {
  switch (mode) {
    case RefreshMode::SystemDefault:
      return "system_default";
    case RefreshMode::Hz72:
      return "72hz";
    case RefreshMode::Hz80:
      return "80hz";
    case RefreshMode::Hz90:
      return "90hz";
    case RefreshMode::Hz120:
      return "120hz";
    case RefreshMode::Hz144:
      return "144hz";
  }
  return "system_default";
}

const char* foveationPresetName(VrRuntimeFoveationPreset preset) {
  switch (preset) {
    case VR_RUNTIME_FOVEATION_OFF:
      return "off";
    case VR_RUNTIME_FOVEATION_LOW:
      return "low";
    case VR_RUNTIME_FOVEATION_MEDIUM:
      return "medium";
    case VR_RUNTIME_FOVEATION_HIGH:
      return "high";
  }
  return "off";
}

int refreshModeHz(RefreshMode mode) {
  switch (mode) {
    case RefreshMode::Hz72:
      return 72;
    case RefreshMode::Hz80:
      return 80;
    case RefreshMode::Hz90:
      return 90;
    case RefreshMode::Hz120:
      return 120;
    case RefreshMode::Hz144:
      return 144;
    case RefreshMode::SystemDefault:
      return 0;
  }
  return 0;
}

}  // namespace vrclient::runtime::perf

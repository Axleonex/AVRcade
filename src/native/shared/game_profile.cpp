#include "shared/game_profile.h"

#include "shared/input/input_system.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>

namespace vrclient::shared {
namespace {

std::string readText(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::in | std::ios::binary);
  if (!input) {
    return {};
  }
  return std::string(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
}

std::optional<std::string_view> findBalanced(
    std::string_view text,
    size_t open,
    char open_ch,
    char close_ch) {
  int depth = 0;
  bool in_string = false;
  for (size_t i = open; i < text.size(); ++i) {
    const char ch = text[i];
    const bool escaped = i > 0 && text[i - 1] == '\\';
    if (ch == '"' && !escaped) {
      in_string = !in_string;
      continue;
    }
    if (in_string) {
      continue;
    }
    if (ch == open_ch) {
      ++depth;
    } else if (ch == close_ch) {
      --depth;
      if (depth == 0) {
        return text.substr(open, i - open + 1);
      }
    }
  }
  return std::nullopt;
}

size_t findKey(std::string_view text, std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\"";
  return text.find(needle);
}

std::optional<std::string_view> findObject(
    std::string_view text,
    std::string_view key) {
  const size_t key_pos = findKey(text, key);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }
  const size_t open = text.find('{', key_pos);
  if (open == std::string_view::npos) {
    return std::nullopt;
  }
  return findBalanced(text, open, '{', '}');
}

std::optional<std::string_view> findArray(
    std::string_view text,
    std::string_view key) {
  const size_t key_pos = findKey(text, key);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }
  const size_t open = text.find('[', key_pos);
  if (open == std::string_view::npos) {
    return std::nullopt;
  }
  return findBalanced(text, open, '[', ']');
}

std::optional<std::string_view> findValueToken(
    std::string_view text,
    std::string_view key) {
  const size_t key_pos = findKey(text, key);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }
  const size_t colon = text.find(':', key_pos);
  if (colon == std::string_view::npos) {
    return std::nullopt;
  }
  size_t value_start = colon + 1;
  while (value_start < text.size() &&
         (text[value_start] == ' ' || text[value_start] == '\t' ||
          text[value_start] == '\r' || text[value_start] == '\n')) {
    ++value_start;
  }
  if (value_start >= text.size()) {
    return std::nullopt;
  }
  if (text[value_start] == '"') {
    const size_t close = text.find('"', value_start + 1);
    if (close == std::string_view::npos) {
      return std::nullopt;
    }
    return text.substr(value_start + 1, close - value_start - 1);
  }

  size_t value_end = value_start;
  while (value_end < text.size() && text[value_end] != ',' &&
         text[value_end] != '}' && text[value_end] != '\r' &&
         text[value_end] != '\n') {
    ++value_end;
  }
  while (value_end > value_start &&
         (text[value_end - 1] == ' ' || text[value_end - 1] == '\t')) {
    --value_end;
  }
  return text.substr(value_start, value_end - value_start);
}

std::optional<std::string> readString(
    std::string_view text,
    std::string_view key) {
  const auto token = findValueToken(text, key);
  if (!token) {
    return std::nullopt;
  }
  return std::string(*token);
}

std::optional<bool> readBool(std::string_view text, std::string_view key) {
  const auto token = findValueToken(text, key);
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

std::optional<int> readInt(std::string_view text, std::string_view key) {
  const auto token = findValueToken(text, key);
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

std::optional<float> readFloat(std::string_view text, std::string_view key) {
  const auto token = findValueToken(text, key);
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

std::vector<std::string_view> readObjectArray(
    std::string_view text,
    std::string_view key) {
  std::vector<std::string_view> objects;
  const auto array = findArray(text, key);
  if (!array) {
    return objects;
  }

  bool in_string = false;
  int depth = 0;
  size_t object_start = std::string_view::npos;
  for (size_t i = 0; i < array->size(); ++i) {
    const char ch = (*array)[i];
    const bool escaped = i > 0 && (*array)[i - 1] == '\\';
    if (ch == '"' && !escaped) {
      in_string = !in_string;
      continue;
    }
    if (in_string) {
      continue;
    }
    if (ch == '{') {
      if (depth == 0) {
        object_start = i;
      }
      ++depth;
    } else if (ch == '}') {
      --depth;
      if (depth == 0 && object_start != std::string_view::npos) {
        objects.push_back(array->substr(object_start, i - object_start + 1));
        object_start = std::string_view::npos;
      }
    }
  }
  return objects;
}

InputActionBinding readInputAction(std::string_view object) {
  InputActionBinding binding;
  binding.action = readString(object, "id").value_or("");
  binding.action_id = input::inputActionIdFromName(binding.action);
  binding.binding = readString(object, "binding").value_or("");
  binding.gamepad_binding = readString(object, "gamepad_binding").value_or("");
  binding.gamepad_fallback = readBool(object, "gamepad_fallback").value_or(false);
  binding.dead_zone = readFloat(object, "dead_zone").value_or(0.0f);
  return binding;
}

InputProfile readInputProfile(std::string_view text) {
  InputProfile profile;
  const auto input_object = findObject(text, "input");
  if (!input_object) {
    return profile;
  }
  profile.motion_aim_mode =
      readString(*input_object, "motion_aim_mode").value_or(profile.motion_aim_mode);
  profile.dominant_hand =
      readString(*input_object, "dominant_hand").value_or(profile.dominant_hand);
  profile.handedness_swap =
      readBool(*input_object, "handedness_swap").value_or(profile.handedness_swap);
  for (std::string_view action : readObjectArray(*input_object, "actions")) {
    profile.actions.push_back(readInputAction(action));
  }
  return profile;
}

ComfortProfile readComfortProfile(std::string_view text, bool* clamped) {
  ComfortProfile profile;
  const auto comfort = findObject(text, "comfort");
  if (!comfort) {
    return profile;
  }

  if (const auto snap = findObject(*comfort, "snap_turn")) {
    profile.snap_turn_enabled = readBool(*snap, "enabled").value_or(profile.snap_turn_enabled);
    profile.snap_turn_degrees = readFloat(*snap, "degrees").value_or(profile.snap_turn_degrees);
  }
  if (const auto smooth = findObject(*comfort, "smooth_turn")) {
    profile.smooth_turn_enabled =
        readBool(*smooth, "enabled").value_or(profile.smooth_turn_enabled);
    profile.smooth_turn_degrees_per_second =
        readFloat(*smooth, "degrees_per_second")
            .value_or(profile.smooth_turn_degrees_per_second);
  }
  if (const auto vignette = findObject(*comfort, "vignette")) {
    profile.vignette_enabled = readBool(*vignette, "enabled").value_or(profile.vignette_enabled);
    profile.vignette_strength =
        readFloat(*vignette, "strength").value_or(profile.vignette_strength);
  }
  if (const auto seated = findObject(*comfort, "seated_standing")) {
    const std::string mode = readString(*seated, "mode").value_or("standing");
    profile.seated_mode = mode == "seated";
    profile.height_offset_m =
        readFloat(*seated, "height_offset_m").value_or(profile.height_offset_m);
  }
  if (const auto recenter = findObject(*comfort, "recenter")) {
    profile.recenter_enabled = readBool(*recenter, "enabled").value_or(profile.recenter_enabled);
  }
  profile.world_scale = readFloat(*comfort, "world_scale").value_or(profile.world_scale);
  clampComfortProfile(&profile, clamped);
  return profile;
}

HudAnchorProfile readHudAnchor(std::string_view object) {
  HudAnchorProfile anchor;
  anchor.element_id = readString(object, "element_id").value_or("");
  anchor.template_data = readBool(object, "template_data").value_or(anchor.template_data);
  const std::string mode = readString(object, "anchor_mode").value_or("head_locked");
  anchor.anchor_mode = mode == "world_locked"
      ? VRCLIENT_HUD_ANCHOR_WORLD_LOCKED
      : VRCLIENT_HUD_ANCHOR_HEAD_LOCKED;
  const std::string visibility = readString(object, "visibility_rule").value_or("always");
  if (visibility == "when_menu") {
    anchor.visibility_rule = VRCLIENT_HUD_VISIBILITY_WHEN_MENU;
  } else if (visibility == "when_gameplay") {
    anchor.visibility_rule = VRCLIENT_HUD_VISIBILITY_WHEN_GAMEPLAY;
  } else {
    anchor.visibility_rule = VRCLIENT_HUD_VISIBILITY_ALWAYS;
  }
  anchor.depth_m = readFloat(object, "depth_m").value_or(anchor.depth_m);
  anchor.scale = readFloat(object, "scale").value_or(anchor.scale);
  anchor.curvature = readFloat(object, "curvature").value_or(anchor.curvature);
  if (const auto offset = findObject(object, "offset")) {
    anchor.offset_x_m = readFloat(*offset, "x_m").value_or(anchor.offset_x_m);
    anchor.offset_y_m = readFloat(*offset, "y_m").value_or(anchor.offset_y_m);
    anchor.offset_z_m = readFloat(*offset, "z_m").value_or(anchor.offset_z_m);
  }
  return anchor;
}

HudProfile readHudProfile(std::string_view text) {
  HudProfile profile;
  const auto hud = findObject(text, "hud");
  if (!hud) {
    return profile;
  }
  profile.coordinate_convention =
      readString(*hud, "coordinate_convention").value_or(profile.coordinate_convention);
  for (std::string_view anchor : readObjectArray(*hud, "anchors")) {
    profile.anchors.push_back(readHudAnchor(anchor));
  }
  return profile;
}

void addIssue(std::vector<std::string>* issues, std::string issue) {
  if (issues != nullptr) {
    issues->push_back(std::move(issue));
  }
}

bool validComfortRange(const ComfortProfile& profile) {
  return profile.snap_turn_degrees >= 15.0f &&
      profile.snap_turn_degrees <= 90.0f &&
      profile.smooth_turn_degrees_per_second >= 30.0f &&
      profile.smooth_turn_degrees_per_second <= 360.0f &&
      profile.vignette_strength >= 0.0f &&
      profile.vignette_strength <= 1.0f &&
      profile.world_scale >= 0.5f &&
      profile.world_scale <= 2.0f &&
      profile.height_offset_m >= -1.0f &&
      profile.height_offset_m <= 1.0f;
}

}  // namespace

GameProfile defaultGameProfile() {
  GameProfile profile;
  profile.input.actions = {
      {"move_x", VRCLIENT_INPUT_ACTION_MOVE_X, "left_thumbstick_x", "left_stick_x", true, 0.15f},
      {"move_y", VRCLIENT_INPUT_ACTION_MOVE_Y, "left_thumbstick_y", "left_stick_y", true, 0.15f},
      {"turn_x", VRCLIENT_INPUT_ACTION_TURN_X, "right_thumbstick_x", "right_stick_x", true, 0.20f},
      {"interact", VRCLIENT_INPUT_ACTION_INTERACT, "right_trigger", "gamepad_a", true, 0.05f},
      {"menu", VRCLIENT_INPUT_ACTION_MENU, "left_menu", "gamepad_start", true, 0.0f},
      {"recenter", VRCLIENT_INPUT_ACTION_RECENTER, "right_grip+left_grip", "gamepad_back", true, 0.0f},
      {"comfort_snap_turn", VRCLIENT_INPUT_ACTION_COMFORT_SNAP_TURN, "right_thumbstick_x", "right_stick_x", true, 0.20f},
      {"comfort_vignette_toggle", VRCLIENT_INPUT_ACTION_COMFORT_VIGNETTE_TOGGLE, "left_grip", "gamepad_y", true, 0.0f},
  };
  profile.hud.anchors = {{
      "template-center-reticle",
      true,
      VRCLIENT_HUD_ANCHOR_HEAD_LOCKED,
      VRCLIENT_HUD_VISIBILITY_ALWAYS,
      1.5f,
      1.0f,
      0.0f,
      0.0f,
      0.0f,
      0.0f,
  }};
  return profile;
}

GameProfileLoadResult loadGameProfileFromFile(const char* path) {
  GameProfileLoadResult result;
  result.profile = defaultGameProfile();
  if (path == nullptr || path[0] == '\0') {
    result.loaded = validateGameProfile(result.profile);
    result.message = "using default game profile";
    return result;
  }

  const std::string text = readText(path);
  if (text.empty()) {
    result.message = "game profile not found or empty";
    return result;
  }

  result.profile.version = readInt(text, "version").value_or(1);
  result.profile.profile_id = readString(text, "profile_id").value_or("template-default");
  result.profile.input = readInputProfile(text);
  result.profile.comfort = readComfortProfile(text, &result.clamped);
  result.profile.hud = readHudProfile(text);
  result.loaded = validateGameProfile(result.profile);
  result.message = result.loaded ? "game profile loaded"
                                 : "game profile missing required fields";
  return result;
}

bool validateGameProfile(
    const GameProfile& profile,
    std::vector<std::string>* issues) {
  bool ok = true;
  if (profile.version < 1) {
    addIssue(issues, "version must be at least 1");
    ok = false;
  }
  if (!input::validateBindingProfile(profile.input, issues)) {
    ok = false;
  }
  if (!validComfortRange(profile.comfort)) {
    addIssue(issues, "comfort values are outside supported runtime ranges");
    ok = false;
  }
  if (!validateHudProfile(profile.hud, issues)) {
    ok = false;
  }
  return ok;
}

bool validateRequiredInputActions(
    const InputProfile& profile,
    std::vector<std::string>* missing) {
  constexpr std::uint32_t required[] = {
      VRCLIENT_INPUT_ACTION_MOVE_X,
      VRCLIENT_INPUT_ACTION_MOVE_Y,
      VRCLIENT_INPUT_ACTION_TURN_X,
      VRCLIENT_INPUT_ACTION_INTERACT,
      VRCLIENT_INPUT_ACTION_MENU,
      VRCLIENT_INPUT_ACTION_RECENTER,
      VRCLIENT_INPUT_ACTION_COMFORT_SNAP_TURN,
      VRCLIENT_INPUT_ACTION_COMFORT_VIGNETTE_TOGGLE,
  };
  bool ok = true;
  for (std::uint32_t action_id : required) {
    if (findInputBinding(profile, action_id) == nullptr) {
      ok = false;
      if (missing != nullptr) {
        missing->push_back(input::inputActionName(action_id));
      }
    }
  }
  return ok;
}

bool validateHudProfile(
    const HudProfile& profile,
    std::vector<std::string>* issues) {
  bool ok = true;
  if (profile.anchors.empty()) {
    addIssue(issues, "hud anchors must not be empty");
    return false;
  }
  for (const HudAnchorProfile& anchor : profile.anchors) {
    if (anchor.element_id.empty()) {
      addIssue(issues, "hud anchor element_id is required");
      ok = false;
    }
    if (anchor.depth_m <= 0.0f || anchor.depth_m > 10.0f) {
      addIssue(issues, "hud anchor depth_m must be in (0, 10]");
      ok = false;
    }
    if (anchor.scale <= 0.0f || anchor.scale > 10.0f) {
      addIssue(issues, "hud anchor scale must be in (0, 10]");
      ok = false;
    }
  }
  return ok;
}

void clampComfortProfile(ComfortProfile* profile, bool* clamped) {
  if (profile == nullptr) {
    return;
  }
  bool changed = false;
  const auto clamp = [&changed](float value, float minimum, float maximum) {
    const float clamped_value = std::clamp(value, minimum, maximum);
    changed = changed || clamped_value != value;
    return clamped_value;
  };
  profile->snap_turn_degrees = clamp(profile->snap_turn_degrees, 15.0f, 90.0f);
  profile->smooth_turn_degrees_per_second =
      clamp(profile->smooth_turn_degrees_per_second, 30.0f, 360.0f);
  profile->vignette_strength = clamp(profile->vignette_strength, 0.0f, 1.0f);
  profile->world_scale = clamp(profile->world_scale, 0.5f, 2.0f);
  profile->height_offset_m = clamp(profile->height_offset_m, -1.0f, 1.0f);
  if (clamped != nullptr) {
    *clamped = *clamped || changed;
  }
}

const InputActionBinding* findInputBinding(
    const InputProfile& profile,
    std::uint32_t action_id) {
  const auto it = std::find_if(
      profile.actions.begin(),
      profile.actions.end(),
      [action_id](const InputActionBinding& binding) {
        return binding.action_id == action_id;
      });
  return it == profile.actions.end() ? nullptr : &*it;
}

}  // namespace vrclient::shared

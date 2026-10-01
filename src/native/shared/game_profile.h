#pragma once

#include "plugins/sdk/shared_services.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace vrclient::shared {

struct InputActionBinding {
  std::string action;
  std::uint32_t action_id = VRCLIENT_INPUT_ACTION_UNKNOWN;
  std::string binding;
  std::string gamepad_binding;
  bool gamepad_fallback = false;
  float dead_zone = 0.0f;
};

struct InputProfile {
  std::string motion_aim_mode = "controller";
  std::string dominant_hand = "right";
  bool handedness_swap = false;
  std::vector<InputActionBinding> actions;
};

struct ComfortProfile {
  bool snap_turn_enabled = true;
  bool smooth_turn_enabled = false;
  bool vignette_enabled = true;
  bool seated_mode = false;
  bool recenter_enabled = true;
  float snap_turn_degrees = 30.0f;
  float smooth_turn_degrees_per_second = 90.0f;
  float vignette_strength = 0.55f;
  float world_scale = 1.0f;
  float height_offset_m = 0.0f;
};

struct HudAnchorProfile {
  std::string element_id;
  bool template_data = true;
  std::uint32_t anchor_mode = VRCLIENT_HUD_ANCHOR_HEAD_LOCKED;
  std::uint32_t visibility_rule = VRCLIENT_HUD_VISIBILITY_ALWAYS;
  float depth_m = 1.5f;
  float scale = 1.0f;
  float offset_x_m = 0.0f;
  float offset_y_m = 0.0f;
  float offset_z_m = 0.0f;
  float curvature = 0.0f;
};

struct HudProfile {
  std::string coordinate_convention =
      "row_major_right_handed_meters_forward_negative_z";
  std::vector<HudAnchorProfile> anchors;
};

struct GameProfile {
  int version = 1;
  std::string profile_id = "template-default";
  InputProfile input;
  ComfortProfile comfort;
  HudProfile hud;
};

struct GameProfileLoadResult {
  bool loaded = false;
  bool clamped = false;
  std::string message;
  GameProfile profile;
};

GameProfile defaultGameProfile();
GameProfileLoadResult loadGameProfileFromFile(const char* path);

bool validateGameProfile(
    const GameProfile& profile,
    std::vector<std::string>* issues = nullptr);

bool validateRequiredInputActions(
    const InputProfile& profile,
    std::vector<std::string>* missing = nullptr);

bool validateHudProfile(
    const HudProfile& profile,
    std::vector<std::string>* issues = nullptr);

void clampComfortProfile(ComfortProfile* profile, bool* clamped = nullptr);

const InputActionBinding* findInputBinding(
    const InputProfile& profile,
    std::uint32_t action_id);

}  // namespace vrclient::shared

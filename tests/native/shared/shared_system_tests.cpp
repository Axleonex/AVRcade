// SHARED-01..06 closure test.
//
// Hardened against NDEBUG: this file previously asserted its entire result via
// the C `assert()` macro, which is compiled out under any Release/NDEBUG build
// (build-and-test.ps1 auto-promotes to Release for -OpenXR on, and this target
// is registered unconditionally). Under that config every assertion vanished
// and the SHARED-01..06 evidence became a vacuous pass — the binding profile
// could validate wrong, comfort clamping could break, or query_action could
// fail and the test would still exit 0. The checks now use a runtime `expect()`
// helper that throws (escaping main -> std::terminate -> non-zero exit, caught
// by CTest) so the evidence is genuine in BOTH Debug and Release.

#include "shared/comfort/comfort_system.h"
#include "shared/game_profile.h"
#include "shared/hud/hud_system.h"
#include "shared/input/input_system.h"

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool near(float left, float right) {
  return std::fabs(left - right) < 0.0001f;
}

std::string defaultProfilePath() {
  return std::string(VRCLIENT_SOURCE_DIR) + "/config/defaults/game-profile.json";
}

void runProfileLoadTest() {
  const auto loaded =
      vrclient::shared::loadGameProfileFromFile(defaultProfilePath().c_str());
  expect(loaded.loaded, "default game-profile.json must load (SHARED-02/05)");
  expect(!loaded.profile.input.actions.empty(),
         "loaded profile must carry input actions");
  expect(vrclient::shared::findInputBinding(
             loaded.profile.input,
             VRCLIENT_INPUT_ACTION_INTERACT) != nullptr,
         "interact action must be bound in loaded profile");
  expect(loaded.profile.comfort.snap_turn_enabled,
         "snap turn enabled from config");
  expect(loaded.profile.comfort.vignette_enabled,
         "vignette enabled from config");
  expect(near(loaded.profile.comfort.world_scale, 1.0f),
         "world_scale default 1.0 from config");
  expect(!loaded.profile.hud.anchors.empty(),
         "loaded profile must carry HUD anchors");
  expect(loaded.profile.hud.anchors.front().template_data,
         "first HUD anchor is template data");
}

void runInputTests() {
  const auto profile = vrclient::shared::defaultGameProfile();
  std::vector<std::string> issues;
  expect(vrclient::shared::input::validateBindingProfile(profile.input, &issues),
         "default binding profile must validate (SHARED-01)");

  auto missing = profile.input;
  missing.actions.erase(missing.actions.begin());
  std::vector<std::string> missing_actions;
  expect(!vrclient::shared::validateRequiredInputActions(missing, &missing_actions),
         "removing a required action must fail validation");
  expect(!missing_actions.empty(), "missing-action list must be reported");

  auto unknown = profile.input;
  unknown.actions.push_back({"unknown_action", VRCLIENT_INPUT_ACTION_UNKNOWN, "x", "y", true, 0.0f});
  issues.clear();
  expect(!vrclient::shared::input::validateBindingProfile(unknown, &issues),
         "unknown action must be rejected by validation");

  vrclient::shared::input::InputServiceRuntime input(profile.input);
  input.setActionState(VRCLIENT_INPUT_ACTION_INTERACT, true, 1.0f, 123);
  VrClientInputActionState state{};
  state.size = sizeof(VrClientInputActionState);
  expect(input.service()->query_action(
             input.service()->user_data,
             VRCLIENT_INPUT_ACTION_INTERACT,
             &state) == VR_ADAPTER_OK,
         "query_action must succeed over the ABI table");
  expect(state.active, "queried action state must be active");
  expect(near(state.value, 1.0f), "queried action value round-trips");
  expect(state.timestamp_ns == 123, "queried action timestamp round-trips");
}

void runComfortTests() {
  const auto profile = vrclient::shared::defaultGameProfile();
  vrclient::shared::comfort::ComfortServiceRuntime comfort(profile.comfort);
  VrClientComfortSettings snapshot = comfort.snapshot();
  expect(snapshot.snap_turn_enabled, "comfort default: snap turn enabled");
  expect(snapshot.vignette_enabled, "comfort default: vignette enabled");
  expect(near(snapshot.world_scale, 1.0f), "comfort default: world_scale 1.0");

  VrClientComfortSettings invalid = snapshot;
  invalid.world_scale = 50.0f;
  invalid.height_offset_m = -50.0f;
  invalid.vignette_strength = 2.0f;
  expect(comfort.updateRuntimeSettings(invalid),
         "out-of-range comfort update must be accepted after clamping");
  snapshot = comfort.snapshot();
  expect(near(snapshot.world_scale, 2.0f),
         "world_scale must clamp to max 2.0 (SHARED-03)");
  expect(near(snapshot.height_offset_m, -1.0f),
         "height_offset must clamp to -1.0 (SHARED-03)");
  expect(near(snapshot.vignette_strength, 1.0f),
         "vignette_strength must clamp to 1.0 (SHARED-03)");
}

void runHudTests() {
  const auto profile = vrclient::shared::defaultGameProfile();
  const auto& anchor = profile.hud.anchors.front();
  float transform[16]{};
  vrclient::shared::hud::computeHudTransform(anchor, transform);
  expect(near(transform[0], anchor.scale), "HUD transform scale.x (SHARED-04)");
  expect(near(transform[5], anchor.scale), "HUD transform scale.y (SHARED-04)");
  expect(near(transform[10], anchor.scale), "HUD transform scale.z (SHARED-04)");
  expect(near(transform[14], -anchor.depth_m),
         "HUD transform must place anchor at -depth_m (SHARED-04)");

  auto invalid = anchor;
  invalid.element_id.clear();
  expect(!vrclient::shared::hud::validateHudAnchor(invalid),
         "HUD anchor with empty element_id must be rejected");

  vrclient::shared::hud::HudServiceRuntime hud(profile.hud);
  expect(hud.service()->anchor_count(hud.service()->user_data) == 1,
         "HUD service reports the configured anchor count");
  VrClientHudAnchor queried{};
  queried.size = sizeof(VrClientHudAnchor);
  expect(hud.service()->query_anchor(
             hud.service()->user_data,
             "template-center-reticle",
             &queried) == VR_ADAPTER_OK,
         "query_anchor must succeed over the ABI table");
  expect(queried.element_id != nullptr, "queried anchor element_id present");
  expect(near(queried.transform[14], -anchor.depth_m),
         "queried anchor transform round-trips depth");
}

}  // namespace

int main() {
  runProfileLoadTest();
  runInputTests();
  runComfortTests();
  runHudTests();
  return 0;
}

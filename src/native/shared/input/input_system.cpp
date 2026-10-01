#include "shared/input/input_system.h"

#include <algorithm>
#include <utility>

namespace vrclient::shared::input {
namespace {

struct ActionName {
  std::uint32_t id;
  const char* name;
};

constexpr ActionName kActions[] = {
    {VRCLIENT_INPUT_ACTION_MOVE_X, "move_x"},
    {VRCLIENT_INPUT_ACTION_MOVE_Y, "move_y"},
    {VRCLIENT_INPUT_ACTION_TURN_X, "turn_x"},
    {VRCLIENT_INPUT_ACTION_INTERACT, "interact"},
    {VRCLIENT_INPUT_ACTION_MENU, "menu"},
    {VRCLIENT_INPUT_ACTION_RECENTER, "recenter"},
    {VRCLIENT_INPUT_ACTION_COMFORT_SNAP_TURN, "comfort_snap_turn"},
    {VRCLIENT_INPUT_ACTION_COMFORT_VIGNETTE_TOGGLE, "comfort_vignette_toggle"},
};

void addIssue(std::vector<std::string>* issues, std::string issue) {
  if (issues != nullptr) {
    issues->push_back(std::move(issue));
  }
}

}  // namespace

const char* inputActionName(std::uint32_t action_id) {
  for (const ActionName& action : kActions) {
    if (action.id == action_id) {
      return action.name;
    }
  }
  return "unknown";
}

std::uint32_t inputActionIdFromName(std::string_view name) {
  for (const ActionName& action : kActions) {
    if (name == action.name) {
      return action.id;
    }
  }
  return VRCLIENT_INPUT_ACTION_UNKNOWN;
}

bool validateBindingProfile(
    const InputProfile& profile,
    std::vector<std::string>* issues) {
  bool ok = true;
  for (const InputActionBinding& binding : profile.actions) {
    if (binding.action_id == VRCLIENT_INPUT_ACTION_UNKNOWN || binding.action.empty()) {
      addIssue(issues, "input action has unknown semantic id");
      ok = false;
    }
    if (binding.binding.empty()) {
      addIssue(issues, "input action is missing primary binding");
      ok = false;
    }
    if (binding.gamepad_fallback && binding.gamepad_binding.empty()) {
      addIssue(issues, "input action enables gamepad fallback without a gamepad binding");
      ok = false;
    }
    if (binding.dead_zone < 0.0f || binding.dead_zone > 1.0f) {
      addIssue(issues, "input action dead zone must be in [0, 1]");
      ok = false;
    }
  }
  std::vector<std::string> missing;
  if (!validateRequiredInputActions(profile, &missing)) {
    for (const std::string& name : missing) {
      addIssue(issues, "missing required input action: " + name);
    }
    ok = false;
  }
  return ok;
}

InputServiceRuntime::InputServiceRuntime() {
  configure(defaultGameProfile().input);
}

InputServiceRuntime::InputServiceRuntime(const InputProfile& profile) {
  configure(profile);
}

bool InputServiceRuntime::configure(
    const InputProfile& profile,
    std::vector<std::string>* issues) {
  if (!validateBindingProfile(profile, issues)) {
    return false;
  }
  for (VrClientInputActionState& state : states_) {
    state = {};
  }
  action_count_ = 0;
  for (const InputActionBinding& binding : profile.actions) {
    if (binding.action_id >= states_.size()) {
      continue;
    }
    VrClientInputActionState& state = states_[binding.action_id];
    state.size = sizeof(VrClientInputActionState);
    state.action_id = binding.action_id;
    state.active = 0;
    state.value = 0.0f;
    state.timestamp_ns = 0;
    action_count_ = std::max(action_count_, binding.action_id);
  }
  service_.size = sizeof(VrClientInputService);
  service_.version = VRCLIENT_SHARED_SERVICE_VERSION;
  service_.query_action = &InputServiceRuntime::queryAction;
  service_.user_data = this;
  service_.action_count = action_count_;
  return true;
}

void InputServiceRuntime::setActionState(
    std::uint32_t action_id,
    bool active,
    float value,
    std::int64_t timestamp_ns) {
  if (action_id >= states_.size()) {
    return;
  }
  VrClientInputActionState& state = states_[action_id];
  state.size = sizeof(VrClientInputActionState);
  state.action_id = action_id;
  state.active = active ? 1u : 0u;
  state.value = std::clamp(value, -1.0f, 1.0f);
  state.timestamp_ns = timestamp_ns;
}

VrAdapterService InputServiceRuntime::adapterService() {
  return {
      sizeof(VrAdapterService),
      VRCLIENT_ADAPTER_SERVICE_INPUT,
      VRCLIENT_SHARED_SERVICE_VERSION,
      &service_,
  };
}

VrAdapterResult VRCLIENT_ADAPTER_CALL InputServiceRuntime::queryAction(
    void* user_data,
    std::uint32_t action_id,
    VrClientInputActionState* out_state) {
  if (user_data == nullptr || out_state == nullptr) {
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  }
  auto* runtime = static_cast<InputServiceRuntime*>(user_data);
  if (action_id >= runtime->states_.size() ||
      runtime->states_[action_id].size != sizeof(VrClientInputActionState)) {
    return VR_ADAPTER_ERROR_UNSUPPORTED_TARGET;
  }
  *out_state = runtime->states_[action_id];
  return VR_ADAPTER_OK;
}

}  // namespace vrclient::shared::input

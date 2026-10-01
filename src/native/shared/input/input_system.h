#pragma once

#include "shared/game_profile.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace vrclient::shared::input {

const char* inputActionName(std::uint32_t action_id);
std::uint32_t inputActionIdFromName(std::string_view name);

bool validateBindingProfile(
    const InputProfile& profile,
    std::vector<std::string>* issues = nullptr);

class InputServiceRuntime {
 public:
  InputServiceRuntime();
  explicit InputServiceRuntime(const InputProfile& profile);

  bool configure(const InputProfile& profile, std::vector<std::string>* issues = nullptr);
  void setActionState(
      std::uint32_t action_id,
      bool active,
      float value,
      std::int64_t timestamp_ns);

  VrClientInputService* service() { return &service_; }
  const VrClientInputService* service() const { return &service_; }
  VrAdapterService adapterService();

 private:
  static VrAdapterResult VRCLIENT_ADAPTER_CALL queryAction(
      void* user_data,
      std::uint32_t action_id,
      VrClientInputActionState* out_state);

  std::array<VrClientInputActionState, VRCLIENT_INPUT_MAX_ACTIONS> states_{};
  std::uint32_t action_count_ = 0;
  VrClientInputService service_{};
};

}  // namespace vrclient::shared::input

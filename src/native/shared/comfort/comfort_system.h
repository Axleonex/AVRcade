#pragma once

#include "shared/game_profile.h"

#include <vector>

namespace vrclient::shared::comfort {

VrClientComfortSettings makeComfortSettings(const ComfortProfile& profile);
bool validateComfortProfile(const ComfortProfile& profile, std::vector<std::string>* issues = nullptr);

class ComfortServiceRuntime {
 public:
  ComfortServiceRuntime();
  explicit ComfortServiceRuntime(const ComfortProfile& profile);

  bool updateFromProfile(const ComfortProfile& profile, std::vector<std::string>* issues = nullptr);
  bool updateRuntimeSettings(const VrClientComfortSettings& settings);

  VrClientComfortSettings snapshot() const { return settings_; }
  VrClientComfortService* service() { return &service_; }
  const VrClientComfortService* service() const { return &service_; }
  VrAdapterService adapterService();

 private:
  static VrAdapterResult VRCLIENT_ADAPTER_CALL snapshotCallback(
      void* user_data,
      VrClientComfortSettings* out_settings);

  VrClientComfortSettings settings_{};
  VrClientComfortService service_{};
};

}  // namespace vrclient::shared::comfort

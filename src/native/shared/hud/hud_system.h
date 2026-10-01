#pragma once

#include "shared/game_profile.h"

#include <string>
#include <vector>

namespace vrclient::shared::hud {

void computeHudTransform(const HudAnchorProfile& anchor, float out_transform[16]);
VrClientHudAnchor makeHudAnchor(const HudAnchorProfile& profile, const char* element_id);
bool validateHudAnchor(const HudAnchorProfile& anchor);

class HudServiceRuntime {
 public:
  HudServiceRuntime();
  explicit HudServiceRuntime(const HudProfile& profile);

  bool configure(const HudProfile& profile, std::vector<std::string>* issues = nullptr);

  VrClientHudService* service() { return &service_; }
  const VrClientHudService* service() const { return &service_; }
  VrAdapterService adapterService();

 private:
  static std::uint32_t VRCLIENT_ADAPTER_CALL anchorCount(void* user_data);
  static VrAdapterResult VRCLIENT_ADAPTER_CALL queryAnchor(
      void* user_data,
      const char* element_id,
      VrClientHudAnchor* out_anchor);
  static VrAdapterResult VRCLIENT_ADAPTER_CALL queryAnchorByIndex(
      void* user_data,
      std::uint32_t index,
      VrClientHudAnchor* out_anchor);

  std::vector<std::string> element_ids_;
  std::vector<VrClientHudAnchor> anchors_;
  VrClientHudService service_{};
};

}  // namespace vrclient::shared::hud

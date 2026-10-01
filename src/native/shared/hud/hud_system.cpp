#include "shared/hud/hud_system.h"

#include <algorithm>
#include <string_view>

namespace vrclient::shared::hud {

void computeHudTransform(const HudAnchorProfile& anchor, float out_transform[16]) {
  for (int index = 0; index < 16; ++index) {
    out_transform[index] = 0.0f;
  }
  out_transform[0] = anchor.scale;
  out_transform[5] = anchor.scale;
  out_transform[10] = anchor.scale;
  out_transform[15] = 1.0f;
  out_transform[12] = anchor.offset_x_m;
  out_transform[13] = anchor.offset_y_m;
  out_transform[14] = -anchor.depth_m + anchor.offset_z_m;
}

VrClientHudAnchor makeHudAnchor(const HudAnchorProfile& profile, const char* element_id) {
  VrClientHudAnchor anchor{};
  anchor.size = sizeof(VrClientHudAnchor);
  anchor.version = VRCLIENT_SHARED_SERVICE_VERSION;
  anchor.element_id = element_id;
  anchor.anchor_mode = profile.anchor_mode;
  anchor.visibility_rule = profile.visibility_rule;
  anchor.template_data = profile.template_data ? 1u : 0u;
  anchor.visible = 1u;
  anchor.depth_m = profile.depth_m;
  anchor.scale = profile.scale;
  anchor.offset_x_m = profile.offset_x_m;
  anchor.offset_y_m = profile.offset_y_m;
  anchor.offset_z_m = profile.offset_z_m;
  anchor.curvature = profile.curvature;
  computeHudTransform(profile, anchor.transform);
  return anchor;
}

bool validateHudAnchor(const HudAnchorProfile& anchor) {
  return !anchor.element_id.empty() &&
      anchor.depth_m > 0.0f &&
      anchor.depth_m <= 10.0f &&
      anchor.scale > 0.0f &&
      anchor.scale <= 10.0f;
}

HudServiceRuntime::HudServiceRuntime() {
  configure(defaultGameProfile().hud);
}

HudServiceRuntime::HudServiceRuntime(const HudProfile& profile) {
  configure(profile);
}

bool HudServiceRuntime::configure(
    const HudProfile& profile,
    std::vector<std::string>* issues) {
  if (!validateHudProfile(profile, issues)) {
    return false;
  }
  element_ids_.clear();
  anchors_.clear();
  element_ids_.reserve(profile.anchors.size());
  anchors_.reserve(profile.anchors.size());
  for (const HudAnchorProfile& profile_anchor : profile.anchors) {
    if (!validateHudAnchor(profile_anchor)) {
      continue;
    }
    element_ids_.push_back(profile_anchor.element_id);
  }
  for (size_t index = 0; index < element_ids_.size(); ++index) {
    anchors_.push_back(makeHudAnchor(profile.anchors[index], element_ids_[index].c_str()));
  }
  service_.size = sizeof(VrClientHudService);
  service_.version = VRCLIENT_SHARED_SERVICE_VERSION;
  service_.anchor_count = &HudServiceRuntime::anchorCount;
  service_.query_anchor = &HudServiceRuntime::queryAnchor;
  service_.query_anchor_by_index = &HudServiceRuntime::queryAnchorByIndex;
  service_.user_data = this;
  return !anchors_.empty();
}

VrAdapterService HudServiceRuntime::adapterService() {
  return {
      sizeof(VrAdapterService),
      VRCLIENT_ADAPTER_SERVICE_HUD,
      VRCLIENT_SHARED_SERVICE_VERSION,
      &service_,
  };
}

std::uint32_t VRCLIENT_ADAPTER_CALL HudServiceRuntime::anchorCount(void* user_data) {
  if (user_data == nullptr) {
    return 0;
  }
  const auto* runtime = static_cast<const HudServiceRuntime*>(user_data);
  return static_cast<std::uint32_t>(runtime->anchors_.size());
}

VrAdapterResult VRCLIENT_ADAPTER_CALL HudServiceRuntime::queryAnchor(
    void* user_data,
    const char* element_id,
    VrClientHudAnchor* out_anchor) {
  if (user_data == nullptr || element_id == nullptr || out_anchor == nullptr) {
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  }
  auto* runtime = static_cast<HudServiceRuntime*>(user_data);
  const auto it = std::find_if(
      runtime->anchors_.begin(),
      runtime->anchors_.end(),
      [element_id](const VrClientHudAnchor& anchor) {
        return anchor.element_id != nullptr && std::string_view(anchor.element_id) == element_id;
      });
  if (it == runtime->anchors_.end()) {
    return VR_ADAPTER_ERROR_UNSUPPORTED_TARGET;
  }
  *out_anchor = *it;
  return VR_ADAPTER_OK;
}

VrAdapterResult VRCLIENT_ADAPTER_CALL HudServiceRuntime::queryAnchorByIndex(
    void* user_data,
    std::uint32_t index,
    VrClientHudAnchor* out_anchor) {
  if (user_data == nullptr || out_anchor == nullptr) {
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  }
  auto* runtime = static_cast<HudServiceRuntime*>(user_data);
  if (index >= runtime->anchors_.size()) {
    return VR_ADAPTER_ERROR_UNSUPPORTED_TARGET;
  }
  *out_anchor = runtime->anchors_[index];
  return VR_ADAPTER_OK;
}

}  // namespace vrclient::shared::hud

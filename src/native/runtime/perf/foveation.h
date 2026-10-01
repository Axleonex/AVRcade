#pragma once

#include "perf/runtime_profile.h"

namespace vrclient::runtime::perf {

struct FoveationSettings {
  VrRuntimeFoveationPreset preset = VR_RUNTIME_FOVEATION_OFF;
  float inner_radius = 0.0f;
  float outer_radius = 1.0f;
};

FoveationSettings makeFoveationSettings(const FoveationProfile& profile);

}  // namespace vrclient::runtime::perf

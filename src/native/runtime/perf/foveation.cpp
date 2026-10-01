#include "perf/foveation.h"

namespace vrclient::runtime::perf {

FoveationSettings makeFoveationSettings(const FoveationProfile& profile) {
  FoveationSettings settings;
  if (!profile.enabled || profile.preset == VR_RUNTIME_FOVEATION_OFF) {
    return settings;
  }

  settings.preset = profile.preset;
  settings.inner_radius = profile.inner_radius;
  settings.outer_radius = profile.outer_radius;
  return settings;
}

}  // namespace vrclient::runtime::perf

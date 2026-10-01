#pragma once

#include "adapters/unreal/unreal_view_seam_locator.h"

#include <string_view>

namespace vrclient::adapters::unreal {

std::string_view mecchaCameraBuildId();
std::string_view mecchaCameraExecutableSha256();
const UnrealPinnedViewSeam& mecchaCameraViewSeam();

}  // namespace vrclient::adapters::unreal

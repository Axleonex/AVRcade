#pragma once

#include "adapters/unreal/unreal_view_seam_locator.h"

#include <filesystem>
#include <string>

namespace vrclient::adapters::unreal {

enum class UnrealViewSeamPeResult {
  Ready,
  InvalidOutput,
  FileReadFailed,
  InvalidDosHeader,
  InvalidNtHeaders,
  UnsupportedArchitecture,
  MissingTextSection,
  MissingRdataSection,
  MissingRuntimeFunctions,
};

UnrealViewSeamPeResult loadUnrealViewSeamImageFromFile(
    const std::filesystem::path& path,
    UnrealViewSeamImage* output,
    std::string* error = nullptr);

}  // namespace vrclient::adapters::unreal

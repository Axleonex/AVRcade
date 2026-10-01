#pragma once

#include "adapters/unreal/native_unreal_contract.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace vrclient::adapters::unreal {

struct UnrealObjectCandidate {
  std::uint64_t object_id = 0;
  std::string object_path;
  std::string component_name;
  bool active = false;
};

struct UnrealObjectSelector {
  std::string object_path;
  std::string component_name;
  bool require_active = true;
};

enum class UnrealDiscoveryResult {
  Found,
  Missing,
  Ambiguous,
  InvalidSelector,
};

UnrealDiscoveryResult selectUniqueUnrealObject(
    const UnrealObjectSelector& selector,
    const std::vector<UnrealObjectCandidate>& candidates,
    std::uint64_t* object_id);

struct UnrealMatrix4 {
  float values[16]{};
};

struct UnrealEyeSubmission {
  UnrealMatrix4 view;
  UnrealMatrix4 projection;
  void* color_target = nullptr;
  std::uint32_t color_array_index = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::int64_t color_format = 0;
};

struct UnrealFrameSubmission {
  std::uint64_t frame_index = 0;
  std::int64_t predicted_display_time_ns = 0;
  RendererApi renderer = RendererApi::Unknown;
  std::array<UnrealEyeSubmission, 2> eyes{};
};

enum class UnrealSubmissionResult {
  Ready,
  InvalidFrame,
  UnsupportedRenderer,
  MissingEyeTarget,
  SharedEyeTarget,
  InvalidDimensions,
  InvalidFormat,
  NonFiniteMatrix,
};

UnrealSubmissionResult validateUnrealFrameSubmission(
    const UnrealFrameSubmission& submission);

enum class UnrealHudMode {
  PreserveFlat,
  WorldLocked,
  HeadLocked,
  Hidden,
};

enum class UnrealInputSemantic {
  Move,
  Look,
  Interact,
  PrimaryAction,
  SecondaryAction,
  Menu,
};

struct UnrealInteractionPolicy {
  UnrealHudMode hud_mode = UnrealHudMode::PreserveFlat;
  bool gamepad_fallback = true;
  bool motion_controls_enabled = false;
};

}  // namespace vrclient::adapters::unreal

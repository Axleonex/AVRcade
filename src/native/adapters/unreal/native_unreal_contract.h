#pragma once

#include <string>

namespace vrclient::adapters::unreal {

enum class RendererApi {
  Unknown,
  D3D11,
  D3D12,
  Vulkan,
};

struct NativeUnrealProfile {
  std::string game_id;
  std::string build_id;
  std::string shipping_executable;
  std::string expected_sha256;
  RendererApi expected_renderer = RendererApi::Unknown;
  bool external_converter_absent_required = true;
};

struct NativeUnrealTarget {
  std::string game_id;
  std::string build_id;
  std::string executable_path;
  std::string executable_sha256;
  bool external_converter_present = false;
};

struct UnrealGraphicsObservation {
  RendererApi renderer = RendererApi::Unknown;
  bool device_present = false;
  bool queue_present = false;
  bool swapchain_present = false;
};

enum class NativeUnrealReadiness {
  Ready,
  InvalidProfile,
  FingerprintMissing,
  WrongGame,
  WrongBuild,
  WrongExecutable,
  HashMissing,
  HashMismatch,
  ExternalConverterPresent,
  RendererUnobserved,
  UnsupportedRenderer,
  MissingDevice,
  MissingQueue,
  MissingSwapchain,
};

NativeUnrealReadiness evaluateNativeUnrealReadiness(
    const NativeUnrealProfile& profile,
    const NativeUnrealTarget& target,
    const UnrealGraphicsObservation& graphics);

const char* nativeUnrealReadinessName(NativeUnrealReadiness readiness);

}  // namespace vrclient::adapters::unreal

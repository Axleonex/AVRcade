#include "adapters/unreal/native_unreal_contract.h"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace vrclient::adapters::unreal {
namespace {

bool isHexSha256(std::string_view value) {
  return value.size() == 64 &&
      std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isxdigit(character) != 0;
      });
}

std::string asciiLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::string fileName(std::string_view path) {
  const auto separator = path.find_last_of("/\\");
  return std::string(
      separator == std::string_view::npos ? path : path.substr(separator + 1));
}

}  // namespace

NativeUnrealReadiness evaluateNativeUnrealReadiness(
    const NativeUnrealProfile& profile,
    const NativeUnrealTarget& target,
    const UnrealGraphicsObservation& graphics) {
  if (profile.game_id.empty() || profile.build_id.empty() ||
      profile.shipping_executable.empty() ||
      profile.expected_renderer != RendererApi::D3D12) {
    return NativeUnrealReadiness::InvalidProfile;
  }
  if (!isHexSha256(profile.expected_sha256)) {
    return NativeUnrealReadiness::FingerprintMissing;
  }
  if (target.game_id != profile.game_id) {
    return NativeUnrealReadiness::WrongGame;
  }
  if (target.build_id != profile.build_id) {
    return NativeUnrealReadiness::WrongBuild;
  }
  if (asciiLower(fileName(target.executable_path)) !=
      asciiLower(fileName(profile.shipping_executable))) {
    return NativeUnrealReadiness::WrongExecutable;
  }
  if (!isHexSha256(target.executable_sha256)) {
    return NativeUnrealReadiness::HashMissing;
  }
  if (asciiLower(target.executable_sha256) !=
      asciiLower(profile.expected_sha256)) {
    return NativeUnrealReadiness::HashMismatch;
  }
  if (profile.external_converter_absent_required &&
      target.external_converter_present) {
    return NativeUnrealReadiness::ExternalConverterPresent;
  }
  if (graphics.renderer == RendererApi::Unknown) {
    return NativeUnrealReadiness::RendererUnobserved;
  }
  if (graphics.renderer != profile.expected_renderer) {
    return NativeUnrealReadiness::UnsupportedRenderer;
  }
  if (!graphics.device_present) {
    return NativeUnrealReadiness::MissingDevice;
  }
  if (!graphics.queue_present) {
    return NativeUnrealReadiness::MissingQueue;
  }
  if (!graphics.swapchain_present) {
    return NativeUnrealReadiness::MissingSwapchain;
  }
  return NativeUnrealReadiness::Ready;
}

const char* nativeUnrealReadinessName(NativeUnrealReadiness readiness) {
  switch (readiness) {
    case NativeUnrealReadiness::Ready: return "ready";
    case NativeUnrealReadiness::InvalidProfile: return "invalid_profile";
    case NativeUnrealReadiness::FingerprintMissing: return "fingerprint_missing";
    case NativeUnrealReadiness::WrongGame: return "wrong_game";
    case NativeUnrealReadiness::WrongBuild: return "wrong_build";
    case NativeUnrealReadiness::WrongExecutable: return "wrong_executable";
    case NativeUnrealReadiness::HashMissing: return "hash_missing";
    case NativeUnrealReadiness::HashMismatch: return "hash_mismatch";
    case NativeUnrealReadiness::ExternalConverterPresent:
      return "external_converter_present";
    case NativeUnrealReadiness::RendererUnobserved: return "renderer_unobserved";
    case NativeUnrealReadiness::UnsupportedRenderer: return "unsupported_renderer";
    case NativeUnrealReadiness::MissingDevice: return "missing_device";
    case NativeUnrealReadiness::MissingQueue: return "missing_queue";
    case NativeUnrealReadiness::MissingSwapchain: return "missing_swapchain";
  }
  return "unknown";
}

}  // namespace vrclient::adapters::unreal

#include "safety/detection_inputs.h"

#include <algorithm>
#include <cctype>

namespace vrclient::safety {
namespace {

std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

// Conservative match: an observed value matches a catalog indicator if either
// contains the other (case-insensitive). This is intentionally generous on the
// RISKY side (it is harder to sneak past a block) and never used to assert
// safety. Pure string comparison; no process interaction.
bool indicatorMatches(const std::string& observed, const std::string& indicator) {
  if (observed.empty() || indicator.empty()) {
    return false;
  }
  const std::string a = toLower(observed);
  const std::string b = toLower(indicator);
  return a.find(b) != std::string::npos || b.find(a) != std::string::npos;
}

// Returns the catalog indicators matched by any observation. Empty result means
// "no known-risky indicator observed" (which is only Clear when enumeration is
// complete; otherwise Uncertain).
std::vector<std::string> collectMatches(
    const std::vector<std::string>& observations,
    const std::vector<std::string>& catalog) {
  std::vector<std::string> matched;
  for (const std::string& observed : observations) {
    for (const std::string& indicator : catalog) {
      if (indicatorMatches(observed, indicator)) {
        if (std::find(matched.begin(), matched.end(), indicator) == matched.end()) {
          matched.push_back(indicator);
        }
      }
    }
  }
  return matched;
}

RiskSignal classifyDimension(
    const std::vector<std::string>& matched,
    bool enumeration_complete) {
  if (!matched.empty()) {
    return RiskSignal::Risky;
  }
  return enumeration_complete ? RiskSignal::Clear : RiskSignal::Uncertain;
}

}  // namespace

const char* riskSignalName(RiskSignal signal) {
  switch (signal) {
    case RiskSignal::Clear:
      return "clear";
    case RiskSignal::Uncertain:
      return "uncertain";
    case RiskSignal::Risky:
      return "risky";
  }
  return "uncertain";
}

const char* moddingPostureName(ModdingPosture posture) {
  switch (posture) {
    case ModdingPosture::Official:
      return "official";
    case ModdingPosture::CommunitySupported:
      return "community-supported";
    case ModdingPosture::Unsupported:
      return "unsupported";
    case ModdingPosture::Unknown:
      return "unknown";
    case ModdingPosture::ConflictingWithIntegrityPolicy:
      return "conflicting-with-integrity-policy";
  }
  return "unknown";
}

ModdingPosture parseModdingPosture(const std::string& value) {
  const std::string normalized = toLower(value);
  if (normalized == "official") {
    return ModdingPosture::Official;
  }
  if (normalized == "community-supported" || normalized == "community_supported") {
    return ModdingPosture::CommunitySupported;
  }
  if (normalized == "unsupported") {
    return ModdingPosture::Unsupported;
  }
  if (normalized == "conflicting-with-integrity-policy" ||
      normalized == "conflicting_with_integrity_policy") {
    return ModdingPosture::ConflictingWithIntegrityPolicy;
  }
  // Anything unrecognized (including the literal "unknown") is treated as
  // Unknown -> default-block-favorable-neutral. Never guess a favorable value.
  return ModdingPosture::Unknown;
}

DetectionSignals classifyDetectionSignals(
    const DetectionObservations& observations,
    const DetectionIndicatorCatalog& catalog) {
  DetectionSignals signals;

  signals.matched_anti_cheat = collectMatches(
      observations.observed_anti_cheat_indicators,
      catalog.known_anti_cheat_indicators);
  signals.matched_online_flags = collectMatches(
      observations.observed_online_mode_flags, catalog.online_only_flags);
  signals.matched_storefront_drm = collectMatches(
      observations.observed_storefront_drm, catalog.storefront_drm_indicators);

  signals.anti_cheat = classifyDimension(
      signals.matched_anti_cheat, observations.module_enumeration_complete);
  signals.online_mode = classifyDimension(
      signals.matched_online_flags, observations.module_enumeration_complete);
  signals.storefront_drm = classifyDimension(
      signals.matched_storefront_drm, observations.module_enumeration_complete);

  return signals;
}

}  // namespace vrclient::safety

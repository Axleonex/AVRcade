#pragma once

// Phase 7 Task T02 — DEFENSIVE detection inputs.
//
// This header models *observations* that a future caller can collect WITHOUT
// interacting with, disabling, hiding from, patching, or bypassing any
// anti-cheat component. Everything here is read-only description: names a caller
// already saw, flags a storefront already reported, posture a config already
// declared. There is intentionally NO code that touches, suspends, unhooks, or
// circumvents a protection system. Detection is conservative and transparent:
// an uncertain signal is surfaced as uncertain and the policy engine
// default-blocks on it; we never upgrade uncertainty into an allow.
//
// The catalog of KNOWN anti-cheat product / module indicators lives in CONFIG
// (config/safety/default-rules.json), not in compiled constants, so the list is
// hot-reloadable and deliverable independently of the client (SAFE-03).

#include <string>
#include <vector>

namespace vrclient::safety {

// Coarse, conservative classification of a single risk dimension. The ordering
// matters: any dimension resolving to Risky forces a block; Uncertain forces an
// unknown-block unless policy explicitly downgrades it to a warning; only Clear
// is a favorable signal, and a favorable signal never overrides another
// dimension's Risky/Uncertain result.
enum class RiskSignal {
  Clear,      // positively observed safe (e.g. no known-AC indicator matched AND
              //  the source set was fully enumerated)
  Uncertain,  // could not be positively established -> default-block fallback
  Risky       // a known-risky indicator was observed -> hard block
};

const char* riskSignalName(RiskSignal signal);

// Declared modding posture for the target. This is a FAVORABLE signal only; it
// can lower review friction but never overrides identity / anti-cheat / online /
// launch-mode results. The five values are fixed by the plan (T01-AC3).
enum class ModdingPosture {
  Official,                       // first-party mod toolkit / documented mod API
  CommunitySupported,             // common community mod-loader, no first-party stance
  Unsupported,                    // no modding support declared
  Unknown,                        // posture not established -> treated as unsupported
  ConflictingWithIntegrityPolicy  // modding declared but conflicts with integrity policy
};

const char* moddingPostureName(ModdingPosture posture);
ModdingPosture parseModdingPosture(const std::string& value);

// Read-only observations a caller may hand to the detector. Every field is
// something observable without evasion. Empty / default means "not observed",
// which the conservative classifier treats as uncertainty, never as safe.
struct DetectionObservations {
  // Anti-cheat product / service / module names the caller already observed in
  // the target's install or process module list (e.g. an "EasyAntiCheat" module
  // name). Compared, case-insensitively, against the configured indicator list.
  std::vector<std::string> observed_anti_cheat_indicators;

  // Game launch flags / declared online-only mode markers the caller observed
  // (e.g. an "online-only" or "always-online" launch flag). Compared against the
  // configured online-only flag list.
  std::vector<std::string> observed_online_mode_flags;

  // Storefront DRM identifiers the caller observed (e.g. a storefront wrapper /
  // DRM module name). Compared against the configured storefront-DRM list.
  std::vector<std::string> observed_storefront_drm;

  // True only when the caller positively enumerated the full module/source set
  // and is therefore able to assert absence. When false, "no indicator matched"
  // is NOT proof of safety — it stays uncertain.
  bool module_enumeration_complete = false;

  // Declared modding posture (favorable signal only).
  ModdingPosture modding_posture = ModdingPosture::Unknown;
};

// Config-backed indicator catalog (loaded from config/safety/default-rules.json
// by the policy loader). All matching is case-insensitive substring / exact
// token matching against these data lists — no compiled-in product names.
struct DetectionIndicatorCatalog {
  std::vector<std::string> known_anti_cheat_indicators;
  std::vector<std::string> online_only_flags;
  std::vector<std::string> storefront_drm_indicators;
};

// The conservative result of classifying observations against the catalog.
struct DetectionSignals {
  RiskSignal anti_cheat = RiskSignal::Uncertain;
  RiskSignal online_mode = RiskSignal::Uncertain;
  RiskSignal storefront_drm = RiskSignal::Uncertain;
  // The specific indicators that matched, for transparent diagnostics.
  std::vector<std::string> matched_anti_cheat;
  std::vector<std::string> matched_online_flags;
  std::vector<std::string> matched_storefront_drm;
};

// Classify observations against the configured catalog. Conservative rules:
//   * any observed indicator that matches the catalog -> Risky on that dimension.
//   * no match AND module_enumeration_complete == true -> Clear on that dimension.
//   * no match AND enumeration incomplete -> Uncertain (NOT clear).
// This function performs NO process interaction; it is pure string comparison.
DetectionSignals classifyDetectionSignals(
    const DetectionObservations& observations,
    const DetectionIndicatorCatalog& catalog);

}  // namespace vrclient::safety

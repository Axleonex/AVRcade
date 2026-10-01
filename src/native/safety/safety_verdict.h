#pragma once

// Phase 7 Task T01 — pre-injection safety verdict model + policy engine.
//
// This is the independent safety/compliance layer that can VETO launch before
// the injector touches the target process. It wraps (does not replace) the
// Phase 3 `runSafetyPreflight` precondition spine: identity / source /
// anti-cheat / online / architecture / runtime-hash checks remain the
// authoritative gate. On top of that spine this layer adds:
//   * a richer four-verdict model (allow / warn / block / unknown-blocked),
//     each carrying a STABLE machine reason code AND a user-facing message key;
//   * config-backed rule data (anti-cheat indicator lists, online-only flags,
//     storefront DRM lists, per-game rule entries) loaded from
//     config/safety/default-rules.json, hot-reloadable and deliverable
//     independently of client releases (SAFE-03);
//   * DEFENSIVE detection inputs (detection_inputs.h) classified conservatively.
//
// DEFAULT-BLOCK is structural: a default-constructed verdict is UnknownBlocked,
// and unknown game identity, unknown build, missing rule set, or unknown verdict
// all resolve to block / unknown-blocked. Modding posture is only a favorable
// signal; it never overrides identity / anti-cheat / online / launch-mode.
//
// INJECTION-FREE: nothing here touches, suspends, unhooks, patches, or bypasses
// any process or protection system. It only computes a verdict.

#include "diagnostics/logging/diagnostic_logger.h"
#include "injector/safety/safety_preflight.h"
#include "safety/detection_inputs.h"
#include "versioning/game_fingerprint.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace vrclient::safety {

// The four-verdict model. Default is UnknownBlocked so any uninitialized or
// unreached path is fail-closed.
enum class SafetyVerdict {
  Allow,            // safe to inject
  Warn,             // injectable only after explicit user acknowledgement
  Block,            // refused: a known-risky condition was identified
  UnknownBlocked    // refused: insufficient/uncertain signal (default-block)
};

const char* safetyVerdictName(SafetyVerdict verdict);

// True for the two refusal verdicts. Callers that only care "may I proceed?"
// should require verdict == Allow (Warn still requires explicit acknowledgement).
bool isRefusal(SafetyVerdict verdict);

// One per-game rule entry from the rule set. All values are config data.
struct SafetyRuleEntry {
  std::string game_id;
  // When true this entry MAY allow without acknowledgement under controlled
  // conditions (the controlled smoke target). For every commercial target this
  // is false, mirroring the B2 autonomy floor: only smoke auto-approves.
  bool controlled_smoke_target = false;
  // When true, an offline-safe target that passes the preflight spine and whose
  // detection signals are all Clear may resolve to Allow. When false, the best a
  // passing target can reach is Warn (explicit acknowledgement required).
  // AUTONOMY FLOOR: allow_offline:true is only valid together with
  // controlled_smoke_target:true. loadSafetyRuleSet() rejects a non-smoke entry
  // that sets allow_offline (the rule set fails to load -> default-block), so this
  // engine's auto-Allow path can never disagree with the B2 safety_gate, which
  // refuses any non-controlled-smoke target without explicit human authorization.
  bool allow_offline = false;
  // When true, an uncertain detection signal is downgraded to Warn instead of
  // UnknownBlocked. Default false keeps uncertain -> unknown-blocked (the
  // conservative default). A risky signal is ALWAYS Block regardless.
  bool allow_warn_on_uncertain = false;
  // Adapter id permitted for this game (informational + matched against the
  // request's adapter id when non-empty).
  std::string adapter_id;
  // Launch modes this entry permits (e.g. "offline", "private_modded"). A
  // requested launch mode not in this list blocks.
  std::vector<std::string> allowed_launch_modes;
  // Modding postures that are acceptable for this entry. A favorable signal
  // only; an acceptable posture never overrides a risky AC/online signal.
  std::vector<ModdingPosture> acceptable_modding_postures;
};

// The full, versioned rule set (the parsed form of default-rules.json).
struct SafetyRuleSet {
  int version = 0;                 // 0 == not loaded -> missing rule set -> block
  std::string rule_set_version;    // human/string version for delivery tracking
  DetectionIndicatorCatalog indicators;
  std::vector<SafetyRuleEntry> rules;

  [[nodiscard]] bool loaded() const { return version >= 1; }
  [[nodiscard]] const SafetyRuleEntry* findRule(const std::string& game_id) const;
};

struct SafetyRuleLoadResult {
  bool loaded = false;
  std::string message;
  SafetyRuleSet rule_set;
};

// Load + validate the rule set from config/safety/default-rules.json (same
// "version field + validate-on-load" convention as game_fingerprint).
SafetyRuleLoadResult loadSafetyRuleSet(const std::filesystem::path& path);

// Inputs to a single verdict evaluation. Identity is reused from versioning; the
// preflight request is reused from Phase 3 (its checks are the precondition
// spine). Detection observations are the DEFENSIVE T02 inputs.
struct SafetyEvaluationRequest {
  versioning::VersionDetectionResult identity;
  injector::safety::SafetyPreflightRequest preflight;
  DetectionObservations detection;
  std::string adapter_id;
  std::string requested_launch_mode = "offline";
  // The rule-set version the caller believes it is evaluating against, recorded
  // in diagnostics for traceability.
  std::string expected_rule_set_version;

  enum class ArtifactTrustState {
    NotEvaluated,
    Trusted,
    Unsigned,
    HashMismatch,
    SignatureInvalid,
    UntrustedKey,
    Revoked,
    UnsupportedAlgorithm,
    UnreadableArtifact,
    InvalidConfig
  };

  struct ArtifactTrustObservation {
    std::string artifact_id;
    std::string artifact_type;
    ArtifactTrustState state = ArtifactTrustState::NotEvaluated;
    std::string reason_code;
  };

  // B6 supply-chain input. Empty means the caller has no artifact trust result
  // to contribute. When supplied, anything other than Trusted fails closed:
  // unsigned/revoked/hash-mismatched/invalid artifacts block; not-evaluated
  // artifacts unknown-block.
  std::vector<ArtifactTrustObservation> artifact_trust;
};

const char* artifactTrustStateName(
    SafetyEvaluationRequest::ArtifactTrustState state);

struct SafetyVerdictResult {
  SafetyVerdict verdict = SafetyVerdict::UnknownBlocked;
  std::string reason_code = "safety_not_evaluated";
  std::string message;
  std::string message_key = "safety.unknown.not_evaluated";
  // The wrapped Phase 3 preflight reason (if the spine refused), for traceability.
  std::string preflight_reason_code;
  std::vector<diagnostics::LogField> diagnostics_fields;
};

// Evaluate a full safety verdict against a loaded rule set. This is the function
// the injector calls before any process touch. A null/unloaded rule set yields
// UnknownBlocked. The evaluation order is fail-closed:
//   1. rule set loaded?                      -> else missing_rule_set (block)
//   2. identity KnownSupported?              -> else unknown/unsupported (block)
//   3. per-game rule entry exists?           -> else no_rule_for_game (block)
//   4. requested launch mode permitted?      -> else launch_mode_not_permitted
//   5. Phase 3 preflight spine approves?     -> else propagate its reason (block)
//   6. detection signals:
//        any Risky                           -> block (e.g. anti_cheat_detected)
//        any Uncertain                       -> unknown-blocked, unless the rule
//                                               allows warn-on-uncertain -> Warn
//   7. modding posture acceptable?           -> favorable; unacceptable -> Warn
//   8. allow only when the rule explicitly allows offline AND it is the
//      controlled smoke target OR allow_offline is set; otherwise Warn.
SafetyVerdictResult evaluateSafetyVerdict(
    const SafetyEvaluationRequest& request,
    const SafetyRuleSet& rule_set,
    diagnostics::AsyncLogger* logger = nullptr);

}  // namespace vrclient::safety

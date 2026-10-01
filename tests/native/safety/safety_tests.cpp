#include "safety/detection_inputs.h"
#include "safety/safety_verdict.h"
#include "injector/process/process_discovery.h"
#include "injector/safety/safety_preflight.h"
#include "versioning/game_fingerprint.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

// Write a throwaway rule-set JSON to a temp path and return it. Used by the
// review-fix tests that must exercise rule data the SHIPPED config can never
// contain (empty allow-lists, non-smoke allow_offline) without those documents
// being rejected by the schema/validator.
std::filesystem::path writeTempRuleSet(
    const std::string& name, const std::string& body) {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / name;
  std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
  out << body;
  out.close();
  return path;
}

std::filesystem::path repoRoot() {
#if defined(VRCLIENT_SOURCE_DIR)
  return std::filesystem::path(VRCLIENT_SOURCE_DIR);
#else
  return std::filesystem::current_path();
#endif
}

std::filesystem::path sampleConfigPath() {
  return repoRoot() / "config" / "games" / "sample-game.json";
}

std::filesystem::path repoConfigPath() {
  return repoRoot() / "config" / "games" / "repo.json";
}

std::filesystem::path defaultRulesPath() {
  return repoRoot() / "config" / "safety" / "default-rules.json";
}

vrclient::injector::process::TargetDescriptor supportedTarget() {
  vrclient::injector::process::TargetDescriptor target;
  target.flow = vrclient::injector::process::DiscoveryFlow::DirectLaunch;
  target.executable_path = "C:/VRClientSmoke/vrclient_smoke_host.exe";
  target.install_root = "C:/VRClientSmoke";
  target.game_id_hint = "vrclient-smoke-host";
  target.architecture = "x64";
  target.executable_sha256 =
      "a4a4893f8f2befed4daa6785f736cf32aa588af41553b3730b683b13fdc4efd7";
  target.identity_evidence_trusted = true;
  target.identity_evidence_source = "test_file_probe";
  return target;
}

vrclient::injector::process::TargetDescriptor repoTarget() {
  vrclient::injector::process::TargetDescriptor target;
  target.flow = vrclient::injector::process::DiscoveryFlow::SteamLaunch;
  target.executable_path = "H:/SteamLibrary/steamapps/common/REPO/REPO.exe";
  target.install_root = "H:/SteamLibrary/steamapps/common/REPO";
  target.storefront_id = "steam:3241660";
  target.game_id_hint = "repo";
  target.build_id_hint = "steam-3241660-build-23363152";
  target.architecture = "x64";
  target.executable_sha256 =
      "412f7cf79cf16888999e22905ca3bb11a6074857efdf8c754073a47ef872317c";
  target.product_version = "2022.3.67f2 (6bedba8691df)";
  target.identity_evidence_trusted = true;
  target.identity_evidence_source = "phase6_repo_file_probe";
  return target;
}

vrclient::versioning::VersionDetectionResult supportedIdentity() {
  const auto loaded =
      vrclient::versioning::loadGameFingerprintConfig(sampleConfigPath());
  expect(loaded.loaded, loaded.message);
  return vrclient::versioning::detectVersion(supportedTarget(), loaded.config);
}

vrclient::versioning::VersionDetectionResult repoIdentity() {
  const auto loaded =
      vrclient::versioning::loadGameFingerprintConfig(repoConfigPath());
  expect(loaded.loaded, loaded.message);
  return vrclient::versioning::detectVersion(repoTarget(), loaded.config);
}

// A smoke evaluation request that, with a fully-clear detection set, should
// reach Allow under the controlled smoke rule.
vrclient::safety::SafetyEvaluationRequest smokeAllowRequest() {
  const auto identity = supportedIdentity();
  vrclient::safety::SafetyEvaluationRequest request;
  request.identity = identity;
  request.preflight = vrclient::injector::safety::makePreflightRequest(
      supportedTarget(), identity, true, true, identity.runtime.sha256);
  request.adapter_id = "phase3-smoke-adapter";
  request.requested_launch_mode = "offline";
  request.expected_rule_set_version = "2026-06-13.1";
  // Offline-safe: caller positively enumerated the module/source set and saw no
  // anti-cheat / online-only / DRM indicator -> all dimensions Clear.
  request.detection.module_enumeration_complete = true;
  request.detection.modding_posture =
      vrclient::safety::ModdingPosture::CommunitySupported;
  return request;
}

vrclient::safety::SafetyEvaluationRequest repoWarnRequest() {
  const auto identity = repoIdentity();
  vrclient::safety::SafetyEvaluationRequest request;
  request.identity = identity;
  auto preflight = vrclient::injector::safety::makePreflightRequest(
      repoTarget(), identity, false, true, identity.runtime.sha256);
  preflight.multiplayer_session_scope = "private_modded";
  preflight.multiplayer_mod_compatibility_confirmed = true;
  request.preflight = preflight;
  request.adapter_id = "vrclient-repo-adapter";
  request.requested_launch_mode = "private_modded";
  request.expected_rule_set_version = "2026-06-13.1";
  request.detection.module_enumeration_complete = true;
  request.detection.modding_posture =
      vrclient::safety::ModdingPosture::CommunitySupported;
  return request;
}

vrclient::safety::SafetyRuleSet loadRules() {
  const auto loaded = vrclient::safety::loadSafetyRuleSet(defaultRulesPath());
  expect(loaded.loaded, loaded.message);
  return loaded.rule_set;
}

// -------------------------------------------------------------------------
// T01 verdict-model tests: allow / warn / block / unknown-blocked.
// -------------------------------------------------------------------------

void runRuleSetLoadTests() {
  const auto loaded = vrclient::safety::loadSafetyRuleSet(defaultRulesPath());
  expect(loaded.loaded, "default rule set should load: " + loaded.message);
  expect(loaded.rule_set.version == 1, "rule set version should be 1");
  expect(!loaded.rule_set.rule_set_version.empty(),
         "rule set must carry a rule_set_version");
  expect(loaded.rule_set.findRule("vrclient-smoke-host") != nullptr,
         "rule set must contain the smoke host entry");
  expect(!loaded.rule_set.indicators.known_anti_cheat_indicators.empty(),
         "anti-cheat indicators must be config data, not empty");

  // Missing / unloaded rule set is structurally default-block.
  vrclient::safety::SafetyRuleSet empty;
  expect(!empty.loaded(), "default-constructed rule set must be unloaded");
}

void runAllowVerdictTest() {
  const auto rules = loadRules();
  const auto result =
      vrclient::safety::evaluateSafetyVerdict(smokeAllowRequest(), rules);
  expect(result.verdict == vrclient::safety::SafetyVerdict::Allow,
         "controlled smoke target with clear signals should allow");
  expect(result.reason_code == "approved", "allow reason code mismatch");
  expect(result.message_key == "safety.allow.approved",
         "allow message key mismatch");
  std::cout << "safety allow: approved controlled_smoke clear-signals\n";
}

void runWarnVerdictTest() {
  const auto rules = loadRules();
  // The R.E.P.O. commercial target passes the preflight spine + clear signals
  // but is NOT a controlled smoke target and has allow_offline == false, so the
  // best reachable verdict is warn (explicit acknowledgement required).
  const auto result =
      vrclient::safety::evaluateSafetyVerdict(repoWarnRequest(), rules);
  expect(result.verdict == vrclient::safety::SafetyVerdict::Warn,
         "commercial target that passes checks should warn, not allow");
  expect(result.reason_code == "acknowledgement_required",
         "warn reason code mismatch");
  expect(result.message_key == "safety.warn.acknowledgement_required",
         "warn message key mismatch");
  std::cout << "safety warn: acknowledgement_required selected_game clear-signals\n";
}

void runBlockVerdictTests() {
  const auto rules = loadRules();

  // Block via the preflight spine (known-online policy on R.E.P.O.).
  auto online = repoWarnRequest();
  online.preflight.online_risk = "known_online";
  auto online_result =
      vrclient::safety::evaluateSafetyVerdict(online, rules);
  expect(online_result.verdict == vrclient::safety::SafetyVerdict::Block,
         "known-online preflight refusal should block");
  expect(online_result.reason_code == "online_mode_risk",
         "preflight block reason should propagate");
  expect(online_result.preflight_reason_code == "online_mode_risk",
         "wrapped preflight reason should be recorded");

  // Block via launch mode not permitted.
  auto launch = smokeAllowRequest();
  launch.requested_launch_mode = "online_pvp";
  auto launch_result =
      vrclient::safety::evaluateSafetyVerdict(launch, rules);
  expect(launch_result.verdict == vrclient::safety::SafetyVerdict::Block,
         "disallowed launch mode should block");
  expect(launch_result.reason_code == "launch_mode_not_permitted",
         "launch mode block reason mismatch");

  // Block via conflicting modding posture (favorable signal that conflicts).
  auto conflict = smokeAllowRequest();
  conflict.detection.modding_posture =
      vrclient::safety::ModdingPosture::ConflictingWithIntegrityPolicy;
  auto conflict_result =
      vrclient::safety::evaluateSafetyVerdict(conflict, rules);
  expect(conflict_result.verdict == vrclient::safety::SafetyVerdict::Block,
         "conflicting modding posture should block");
  expect(conflict_result.reason_code ==
             "modding_conflicts_with_integrity_policy",
         "conflicting posture block reason mismatch");
  std::cout << "safety block: online_mode_risk / launch_mode_not_permitted / "
               "modding_conflicts_with_integrity_policy\n";
}

void runUnknownBlockedTests() {
  const auto rules = loadRules();

  // Unknown identity -> unknown-blocked.
  auto unknown_identity = smokeAllowRequest();
  unknown_identity.identity.status =
      vrclient::versioning::DetectionStatus::Unknown;
  auto ui_result =
      vrclient::safety::evaluateSafetyVerdict(unknown_identity, rules);
  expect(ui_result.verdict == vrclient::safety::SafetyVerdict::UnknownBlocked,
         "unknown identity should be unknown-blocked");
  expect(ui_result.reason_code == "unknown_game_or_build",
         "unknown identity reason mismatch");

  // Unknown build (ambiguous status) -> unknown-blocked.
  auto ambiguous = smokeAllowRequest();
  ambiguous.identity.status =
      vrclient::versioning::DetectionStatus::Ambiguous;
  auto amb_result = vrclient::safety::evaluateSafetyVerdict(ambiguous, rules);
  expect(amb_result.verdict == vrclient::safety::SafetyVerdict::UnknownBlocked,
         "ambiguous build should be unknown-blocked");

  // KnownUnsupported -> block (not unknown-blocked).
  auto unsupported = smokeAllowRequest();
  unsupported.identity.status =
      vrclient::versioning::DetectionStatus::KnownUnsupported;
  auto unsup_result =
      vrclient::safety::evaluateSafetyVerdict(unsupported, rules);
  expect(unsup_result.verdict == vrclient::safety::SafetyVerdict::Block,
         "known-unsupported identity should block");
  expect(unsup_result.reason_code == "unsupported_identity",
         "unsupported identity reason mismatch");

  // No rule entry for the game -> unknown-blocked.
  auto no_rule = smokeAllowRequest();
  no_rule.identity.game_id = "game-with-no-rule";
  auto no_rule_result =
      vrclient::safety::evaluateSafetyVerdict(no_rule, rules);
  expect(no_rule_result.verdict ==
             vrclient::safety::SafetyVerdict::UnknownBlocked,
         "missing per-game rule should be unknown-blocked");
  expect(no_rule_result.reason_code == "no_rule_for_game",
         "missing rule reason mismatch");

  // Missing rule set entirely -> unknown-blocked (default-block).
  vrclient::safety::SafetyRuleSet empty;
  auto missing_result =
      vrclient::safety::evaluateSafetyVerdict(smokeAllowRequest(), empty);
  expect(missing_result.verdict ==
             vrclient::safety::SafetyVerdict::UnknownBlocked,
         "missing rule set should be unknown-blocked");
  expect(missing_result.reason_code == "missing_rule_set",
         "missing rule set reason mismatch");
  std::cout << "safety unknown-blocked: unknown_game_or_build / no_rule_for_game "
               "/ missing_rule_set\n";
}

void runModdingPostureRepresentationTest() {
  // All five postures round-trip by name and parse back, and NONE of them, when
  // set on an otherwise-blocking request, can lift the block (default-block is
  // never weakened by posture).
  using vrclient::safety::ModdingPosture;
  const ModdingPosture all[] = {
      ModdingPosture::Official,
      ModdingPosture::CommunitySupported,
      ModdingPosture::Unsupported,
      ModdingPosture::Unknown,
      ModdingPosture::ConflictingWithIntegrityPolicy,
  };
  for (const ModdingPosture posture : all) {
    const std::string name = vrclient::safety::moddingPostureName(posture);
    expect(vrclient::safety::parseModdingPosture(name) == posture,
           "modding posture round-trip failed for " + name);
  }

  const auto rules = loadRules();
  // A favorable posture (official) cannot rescue an unknown identity.
  for (const ModdingPosture posture : all) {
    auto request = smokeAllowRequest();
    request.identity.status = vrclient::versioning::DetectionStatus::Unknown;
    request.detection.modding_posture = posture;
    const auto result =
        vrclient::safety::evaluateSafetyVerdict(request, rules);
    expect(vrclient::safety::isRefusal(result.verdict),
           "modding posture must not lift an unknown-identity block");
  }
  std::cout << "safety modding posture: all five representable, never weakens "
               "default-block\n";
}

// -------------------------------------------------------------------------
// T02 detection-input fixtures: known-AC -> block; offline-safe -> allow only
// when rules allow; mod-friendly -> allow only when AC/online/launch all allow;
// uncertain -> unknown-blocked.
// -------------------------------------------------------------------------

void runDetectionClassifierTests() {
  vrclient::safety::DetectionIndicatorCatalog catalog;
  catalog.known_anti_cheat_indicators = {"EasyAntiCheat", "BattlEye"};
  catalog.online_only_flags = {"always-online"};
  catalog.storefront_drm_indicators = {"Denuvo"};

  // Known anti-cheat module observed -> Risky.
  vrclient::safety::DetectionObservations ac;
  ac.observed_anti_cheat_indicators = {"EasyAntiCheat_x64.dll"};
  ac.module_enumeration_complete = true;
  auto ac_signals = vrclient::safety::classifyDetectionSignals(ac, catalog);
  expect(ac_signals.anti_cheat == vrclient::safety::RiskSignal::Risky,
         "known anti-cheat module should classify Risky");

  // Fully enumerated, nothing matched -> Clear on all dimensions.
  vrclient::safety::DetectionObservations clean;
  clean.module_enumeration_complete = true;
  auto clean_signals =
      vrclient::safety::classifyDetectionSignals(clean, catalog);
  expect(clean_signals.anti_cheat == vrclient::safety::RiskSignal::Clear,
         "enumerated empty set should be Clear");

  // Enumeration incomplete, nothing matched -> Uncertain (NOT Clear).
  vrclient::safety::DetectionObservations partial;
  partial.module_enumeration_complete = false;
  auto partial_signals =
      vrclient::safety::classifyDetectionSignals(partial, catalog);
  expect(partial_signals.anti_cheat == vrclient::safety::RiskSignal::Uncertain,
         "incomplete enumeration must stay Uncertain, never guess Clear");
}

void runKnownAntiCheatBlockFixture() {
  const auto rules = loadRules();
  auto request = smokeAllowRequest();
  // Observe a known anti-cheat indicator from the configured list.
  request.detection.observed_anti_cheat_indicators = {"EasyAntiCheat"};
  const auto result = vrclient::safety::evaluateSafetyVerdict(request, rules);
  expect(result.verdict == vrclient::safety::SafetyVerdict::Block,
         "known anti-cheat fixture must block through policy");
  expect(result.reason_code == "anti_cheat_detected",
         "known anti-cheat block reason mismatch");
  std::cout << "safety detection: known anti-cheat -> block (anti_cheat_detected)\n";
}

void runOfflineSafeAllowFixture() {
  const auto rules = loadRules();
  // Offline-safe smoke target with all-clear signals allows ONLY because the
  // smoke rule explicitly permits it.
  const auto allowed =
      vrclient::safety::evaluateSafetyVerdict(smokeAllowRequest(), rules);
  expect(allowed.verdict == vrclient::safety::SafetyVerdict::Allow,
         "offline-safe target allows only when the rule explicitly allows");

  // The SAME clear offline-safe signals on the R.E.P.O. entry (allow_offline
  // false) cannot reach allow -> warn.
  const auto not_allowed =
      vrclient::safety::evaluateSafetyVerdict(repoWarnRequest(), rules);
  expect(not_allowed.verdict == vrclient::safety::SafetyVerdict::Warn,
         "offline-safe signals do not allow when the rule does not permit it");
  std::cout << "safety detection: offline-safe -> allow only when rule allows\n";
}

void runModFriendlyDoesNotBypassFixture() {
  const auto rules = loadRules();

  // Mod-friendly (official posture) but a known anti-cheat indicator present:
  // the favorable mod signal must NOT bypass the anti-cheat block.
  auto ac = smokeAllowRequest();
  ac.detection.modding_posture = vrclient::safety::ModdingPosture::Official;
  ac.detection.observed_anti_cheat_indicators = {"BattlEye"};
  auto ac_result = vrclient::safety::evaluateSafetyVerdict(ac, rules);
  expect(ac_result.verdict == vrclient::safety::SafetyVerdict::Block,
         "mod-friendly posture must not bypass an anti-cheat block");
  expect(ac_result.reason_code == "anti_cheat_detected",
         "mod-friendly anti-cheat block reason mismatch");

  // Mod-friendly but online-only mode observed -> still block.
  auto online = smokeAllowRequest();
  online.detection.modding_posture = vrclient::safety::ModdingPosture::Official;
  online.detection.observed_online_mode_flags = {"always-online"};
  auto online_result = vrclient::safety::evaluateSafetyVerdict(online, rules);
  expect(online_result.verdict == vrclient::safety::SafetyVerdict::Block,
         "mod-friendly posture must not bypass an online-only block");
  expect(online_result.reason_code == "online_only_mode_detected",
         "mod-friendly online block reason mismatch");

  // Mod-friendly but a disallowed launch mode -> still block.
  auto launch = smokeAllowRequest();
  launch.detection.modding_posture = vrclient::safety::ModdingPosture::Official;
  launch.requested_launch_mode = "online_coop";
  auto launch_result = vrclient::safety::evaluateSafetyVerdict(launch, rules);
  expect(launch_result.verdict == vrclient::safety::SafetyVerdict::Block,
         "mod-friendly posture must not bypass a launch-mode block");
  std::cout << "safety detection: mod-friendly never bypasses AC/online/launch\n";
}

void runUncertainSignalFixture() {
  const auto rules = loadRules();
  // Uncertain detection (enumeration incomplete) on the smoke target: even
  // though the rule allows offline, the uncertain signal forces unknown-blocked
  // because allow_warn_on_uncertain is false.
  auto request = smokeAllowRequest();
  request.detection.module_enumeration_complete = false;
  const auto result = vrclient::safety::evaluateSafetyVerdict(request, rules);
  expect(result.verdict == vrclient::safety::SafetyVerdict::UnknownBlocked,
         "uncertain detection signal must be unknown-blocked");
  expect(result.reason_code == "uncertain_detection_signal",
         "uncertain detection reason mismatch");
  std::cout << "safety detection: uncertain -> unknown-blocked\n";
}

// -------------------------------------------------------------------------
// Review-fix regression tests. These exercise rule data the SHIPPED config can
// never legally contain (empty allow-lists, non-smoke allow_offline) to prove
// the engine's least-permissive / autonomy-floor invariants in code, not just
// in schema/data.
// -------------------------------------------------------------------------

// Fix 1: an EMPTY allowed_launch_modes list blocks ALL launch modes (not all).
void runEmptyLaunchModesBlocksFixture() {
  const std::string body =
      "{\n"
      "  \"version\": 1,\n"
      "  \"rule_set_version\": \"test-empty-launch\",\n"
      "  \"anti_cheat_indicators\": [\"EasyAntiCheat\"],\n"
      "  \"online_only_flags\": [],\n"
      "  \"storefront_drm_indicators\": [],\n"
      "  \"rules\": [\n"
      "    {\n"
      "      \"game_id\": \"vrclient-smoke-host\",\n"
      "      \"controlled_smoke_target\": true,\n"
      "      \"allow_offline\": true,\n"
      "      \"allow_warn_on_uncertain\": false,\n"
      "      \"adapter_id\": \"phase3-smoke-adapter\",\n"
      "      \"allowed_launch_modes\": [],\n"
      "      \"acceptable_modding_postures\": [\"community-supported\"]\n"
      "    }\n"
      "  ]\n"
      "}\n";
  const auto path =
      writeTempRuleSet("vrclient_safety_empty_launch.json", body);
  const auto loaded = vrclient::safety::loadSafetyRuleSet(path);
  std::filesystem::remove(path);
  expect(loaded.loaded,
         "rule set with an empty launch-mode list should still LOAD (the engine, "
         "not the loader, enforces the empty-means-block invariant): " +
             loaded.message);

  auto request = smokeAllowRequest();  // requests "offline"
  const auto result =
      vrclient::safety::evaluateSafetyVerdict(request, loaded.rule_set);
  expect(result.verdict == vrclient::safety::SafetyVerdict::Block,
         "an EMPTY allowed_launch_modes list must block every launch mode");
  expect(result.reason_code == "launch_mode_not_permitted",
         "empty launch-mode list should block with launch_mode_not_permitted");
  std::cout << "safety fix1: empty allowed_launch_modes -> block (not allow)\n";
}

// Fix 2: an EMPTY acceptable_modding_postures list forces Warn (no posture
// acceptable), it never lets the verdict reach Allow.
void runEmptyModdingPosturesForcesWarnFixture() {
  const std::string body =
      "{\n"
      "  \"version\": 1,\n"
      "  \"rule_set_version\": \"test-empty-posture\",\n"
      "  \"anti_cheat_indicators\": [\"EasyAntiCheat\"],\n"
      "  \"online_only_flags\": [],\n"
      "  \"storefront_drm_indicators\": [],\n"
      "  \"rules\": [\n"
      "    {\n"
      "      \"game_id\": \"vrclient-smoke-host\",\n"
      "      \"controlled_smoke_target\": true,\n"
      "      \"allow_offline\": true,\n"
      "      \"allow_warn_on_uncertain\": false,\n"
      "      \"adapter_id\": \"phase3-smoke-adapter\",\n"
      "      \"allowed_launch_modes\": [\"offline\", \"single_player\"],\n"
      "      \"acceptable_modding_postures\": []\n"
      "    }\n"
      "  ]\n"
      "}\n";
  const auto path =
      writeTempRuleSet("vrclient_safety_empty_posture.json", body);
  const auto loaded = vrclient::safety::loadSafetyRuleSet(path);
  std::filesystem::remove(path);
  expect(loaded.loaded,
         "rule set with an empty posture list should still LOAD: " +
             loaded.message);

  // The same all-clear smoke request that normally Allows must now only reach
  // Warn, because no posture is acceptable.
  const auto result =
      vrclient::safety::evaluateSafetyVerdict(smokeAllowRequest(), loaded.rule_set);
  expect(result.verdict == vrclient::safety::SafetyVerdict::Warn,
         "an EMPTY acceptable_modding_postures list must force Warn, not Allow");
  expect(result.reason_code == "acknowledgement_required",
         "empty posture list should fall through to acknowledgement_required");
  std::cout << "safety fix2: empty acceptable_modding_postures -> warn (not allow)\n";
}

// Fix 3: a non-smoke entry that sets allow_offline:true is REJECTED at load time
// (autonomy floor) so the rule set fails to load -> default-block. A loaded
// request against that unloaded set is unknown-blocked.
void runNonSmokeAllowOfflineRejectedFixture() {
  const std::string body =
      "{\n"
      "  \"version\": 1,\n"
      "  \"rule_set_version\": \"test-bad-autonomy\",\n"
      "  \"anti_cheat_indicators\": [\"EasyAntiCheat\"],\n"
      "  \"online_only_flags\": [],\n"
      "  \"storefront_drm_indicators\": [],\n"
      "  \"rules\": [\n"
      "    {\n"
      "      \"game_id\": \"some-commercial-game\",\n"
      "      \"controlled_smoke_target\": false,\n"
      "      \"allow_offline\": true,\n"
      "      \"allow_warn_on_uncertain\": false,\n"
      "      \"adapter_id\": \"some-adapter\",\n"
      "      \"allowed_launch_modes\": [\"offline\"],\n"
      "      \"acceptable_modding_postures\": [\"community-supported\"]\n"
      "    }\n"
      "  ]\n"
      "}\n";
  const auto path =
      writeTempRuleSet("vrclient_safety_bad_autonomy.json", body);
  const auto loaded = vrclient::safety::loadSafetyRuleSet(path);
  std::filesystem::remove(path);
  expect(!loaded.loaded,
         "a non-smoke entry with allow_offline:true must be REJECTED at load "
         "(autonomy floor), but the rule set loaded");
  expect(!loaded.rule_set.loaded(),
         "the rejected rule set must be structurally unloaded (default-block)");

  // Evaluating against the unloaded set is unknown-blocked (default-block).
  const auto result = vrclient::safety::evaluateSafetyVerdict(
      smokeAllowRequest(), loaded.rule_set);
  expect(result.verdict == vrclient::safety::SafetyVerdict::UnknownBlocked,
         "a rejected (unloaded) rule set must default-block");
  expect(result.reason_code == "missing_rule_set",
         "default-block on an unloaded rule set should be missing_rule_set");
  std::cout << "safety fix3: non-smoke allow_offline -> rejected at load -> "
               "default-block\n";
}

}  // namespace

int main() {
  runRuleSetLoadTests();
  runAllowVerdictTest();
  runWarnVerdictTest();
  runBlockVerdictTests();
  runUnknownBlockedTests();
  runModdingPostureRepresentationTest();
  runDetectionClassifierTests();
  runKnownAntiCheatBlockFixture();
  runOfflineSafeAllowFixture();
  runModFriendlyDoesNotBypassFixture();
  runUncertainSignalFixture();
  runEmptyLaunchModesBlocksFixture();
  runEmptyModdingPosturesForcesWarnFixture();
  runNonSmokeAllowOfflineRejectedFixture();
  std::cout << "safety unit tests passed\n";
  return 0;
}

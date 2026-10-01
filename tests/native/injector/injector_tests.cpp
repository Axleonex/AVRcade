#include "injector/bootstrap/bootstrap_smoke.h"
#include "injector/process/process_discovery.h"
#include "injector/safety/safety_preflight.h"
#include "safety/safety_verdict.h"
#include "versioning/game_fingerprint.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

constexpr const char* kSupportedHash =
    "a4a4893f8f2befed4daa6785f736cf32aa588af41553b3730b683b13fdc4efd7";
constexpr const char* kRepoHash =
    "412f7cf79cf16888999e22905ca3bb11a6074857efdf8c754073a47ef872317c";

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class FakeBootstrapLoader : public vrclient::injector::bootstrap::BootstrapRuntimeLoader {
 public:
  vrclient::injector::bootstrap::RuntimeLoadAttempt loadRuntime(
      const vrclient::injector::safety::SafetyPreflightRequest&) override {
    vrclient::injector::bootstrap::RuntimeLoadAttempt attempt;
    attempt.loaded = load_succeeds;
    attempt.partial_load = partial_load;
    attempt.reason_code = "runtime_load_failed";
    attempt.message = "fake controlled smoke load failed";
    return attempt;
  }

  bool probePoseTimingApi() override { return pose_timing_available; }
  bool cleanupPartialLoad() override { return cleanup_succeeds; }

  bool load_succeeds = true;
  bool pose_timing_available = true;
  bool partial_load = false;
  bool cleanup_succeeds = true;
};

std::filesystem::path sampleConfigPath() {
#if defined(VRCLIENT_SOURCE_DIR)
  return std::filesystem::path(VRCLIENT_SOURCE_DIR) / "config" / "games" /
      "sample-game.json";
#else
  return std::filesystem::path("config/games/sample-game.json");
#endif
}

std::filesystem::path repoConfigPath() {
#if defined(VRCLIENT_SOURCE_DIR)
  return std::filesystem::path(VRCLIENT_SOURCE_DIR) / "config" / "games" /
      "repo.json";
#else
  return std::filesystem::path("config/games/repo.json");
#endif
}

vrclient::injector::process::TargetDescriptor supportedTarget() {
  vrclient::injector::process::TargetDescriptor target;
  target.flow = vrclient::injector::process::DiscoveryFlow::DirectLaunch;
  target.executable_path = "C:/VRClientSmoke/vrclient_smoke_host.exe";
  target.install_root = "C:/VRClientSmoke";
  target.game_id_hint = "vrclient-smoke-host";
  target.architecture = "x64";
  target.executable_sha256 = kSupportedHash;
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
  target.executable_sha256 = kRepoHash;
  target.product_version = "2022.3.67f2 (6bedba8691df)";
  target.identity_evidence_trusted = true;
  target.identity_evidence_source = "phase6_repo_file_probe";
  return target;
}

vrclient::versioning::VersionDetectionResult supportedIdentity() {
  const auto loaded = vrclient::versioning::loadGameFingerprintConfig(sampleConfigPath());
  expect(loaded.loaded, loaded.message);
  return vrclient::versioning::detectVersion(supportedTarget(), loaded.config);
}

vrclient::versioning::VersionDetectionResult repoIdentity() {
  const auto loaded = vrclient::versioning::loadGameFingerprintConfig(repoConfigPath());
  expect(loaded.loaded, loaded.message);
  return vrclient::versioning::detectVersion(repoTarget(), loaded.config);
}

vrclient::injector::safety::SafetyPreflightRequest approvedPreflightRequest() {
  const auto identity = supportedIdentity();
  return vrclient::injector::safety::makePreflightRequest(
      supportedTarget(),
      identity,
      true,
      true,
      identity.runtime.sha256);
}

vrclient::injector::safety::SafetyPreflightRequest repoApprovedPreflightRequest() {
  const auto identity = repoIdentity();
  auto request = vrclient::injector::safety::makePreflightRequest(
      repoTarget(),
      identity,
      false,
      true,
      identity.runtime.sha256);
  request.multiplayer_session_scope = "private_modded";
  request.multiplayer_mod_compatibility_confirmed = true;
  return request;
}

// ---------------------------------------------------------------------------
// Phase 7 (T03) — the safety verdict gate. The injector requires an Allow
// verdict before the runtime loader. These helpers reuse the real Phase 7
// evaluation (no fakes): the smoke target with clear, fully-enumerated
// detection signals resolves to Allow; the same machinery produces real
// Block / UnknownBlocked verdicts for the refusal proofs below.
// ---------------------------------------------------------------------------

std::filesystem::path defaultRulesPath() {
#if defined(VRCLIENT_SOURCE_DIR)
  return std::filesystem::path(VRCLIENT_SOURCE_DIR) / "config" / "safety" /
      "default-rules.json";
#else
  return std::filesystem::path("config/safety/default-rules.json");
#endif
}

vrclient::safety::SafetyRuleSet loadSafetyRules() {
  const auto loaded = vrclient::safety::loadSafetyRuleSet(defaultRulesPath());
  expect(loaded.loaded, loaded.message);
  return loaded.rule_set;
}

vrclient::safety::SafetyEvaluationRequest smokeEvaluationRequest() {
  const auto identity = supportedIdentity();
  vrclient::safety::SafetyEvaluationRequest request;
  request.identity = identity;
  request.preflight = approvedPreflightRequest();
  request.adapter_id = "phase3-smoke-adapter";
  request.requested_launch_mode = "offline";
  request.expected_rule_set_version = "2026-06-13.1";
  request.detection.module_enumeration_complete = true;
  request.detection.modding_posture =
      vrclient::safety::ModdingPosture::CommunitySupported;
  return request;
}

// The Allow verdict a passing smoke target produces through the real engine.
vrclient::safety::SafetyVerdictResult smokeAllowVerdict() {
  const auto rules = loadSafetyRules();
  auto verdict = vrclient::safety::evaluateSafetyVerdict(
      smokeEvaluationRequest(), rules);
  expect(verdict.verdict == vrclient::safety::SafetyVerdict::Allow,
         "smoke evaluation should resolve to Allow for the bootstrap gate tests");
  return verdict;
}

// The R.E.P.O. commercial target passes the preflight spine (private modded
// co-op, clear signals) but is NOT a controlled smoke target and is not
// offline-allowed, so it resolves to Warn (explicit acknowledgement required).
vrclient::safety::SafetyEvaluationRequest repoWarnEvaluationRequest() {
  const auto identity = repoIdentity();
  vrclient::safety::SafetyEvaluationRequest request;
  request.identity = identity;
  request.preflight = repoApprovedPreflightRequest();
  request.adapter_id = "vrclient-repo-adapter";
  request.requested_launch_mode = "private_modded";
  request.expected_rule_set_version = "2026-06-13.1";
  request.detection.module_enumeration_complete = true;
  request.detection.modding_posture =
      vrclient::safety::ModdingPosture::CommunitySupported;
  return request;
}

void runDiscoveryChoiceTest() {
  vrclient::injector::process::ProcessDiscoveryRequest request;
  request.flow = vrclient::injector::process::DiscoveryFlow::SteamLaunch;
  request.game_id_hint = "vrclient-smoke-host";
  request.install_candidates = {
      {"D:/Games/Smoke/vrclient_smoke_host.exe", "D:/Games/Smoke", "steam", true},
      {"C:/Games/Smoke/vrclient_smoke_host.exe", "C:/Games/Smoke", "steam", true},
  };

  const auto result = vrclient::injector::process::discoverTargets(request);
  expect(result.status == vrclient::injector::process::DiscoveryStatus::MultipleCandidates,
         "multiple candidates should be reported");
  expect(result.descriptors.size() == 2, "candidate count mismatch");
  expect(result.descriptors[0].executable_path.generic_string().find("C:/") == 0,
         "candidate sorting mismatch");
  expect(result.reason_code == "multiple_install_candidates",
         "multiple candidate reason mismatch");
}

void runAttachRaceTests() {
  vrclient::injector::process::ProcessDiscoveryRequest request;
  request.flow = vrclient::injector::process::DiscoveryFlow::AttachRunning;
  request.expected_process_start_token = "old";
  request.running_process.process_id = 42;
  request.running_process.exists = true;
  request.running_process.process_start_token = "new";
  request.running_process.executable_path = "C:/Games/Smoke/vrclient_smoke_host.exe";
  const auto restarted = vrclient::injector::process::discoverTargets(request);
  expect(restarted.status == vrclient::injector::process::DiscoveryStatus::ProcessRestarted,
         "changed start token should refuse attach");

  request.expected_process_start_token = "new";
  request.expected_running_executable = "D:/Other/other.exe";
  const auto mismatch = vrclient::injector::process::discoverTargets(request);
  expect(mismatch.status ==
             vrclient::injector::process::DiscoveryStatus::RunningProcessMismatch,
         "changed executable should refuse attach");
}

void runMissingExecutableTests() {
  vrclient::injector::process::ProcessDiscoveryRequest request;
  request.flow = vrclient::injector::process::DiscoveryFlow::ManualPath;
  request.install_candidates = {
      {"C:/Missing/vrclient_smoke_host.exe", "C:/Missing", "manual", false},
  };
  auto result = vrclient::injector::process::discoverTargets(request);
  expect(result.status == vrclient::injector::process::DiscoveryStatus::MissingExecutable,
         "missing manual candidate should refuse");

  request = {};
  request.flow = vrclient::injector::process::DiscoveryFlow::DirectLaunch;
  request.executable_path = "C:/Missing/vrclient_smoke_host.exe";
  request.executable_exists = false;
  result = vrclient::injector::process::discoverTargets(request);
  expect(result.status == vrclient::injector::process::DiscoveryStatus::MissingExecutable,
         "missing direct executable should refuse");
}

void runSafetyRefusalTests() {
  auto request = approvedPreflightRequest();

  auto result = vrclient::injector::safety::runSafetyPreflight(request);
  expect(result.verdict == vrclient::injector::safety::SafetyVerdict::Approved,
         "baseline preflight should approve");

  request.anti_cheat_risk = "unknown";
  result = vrclient::injector::safety::runSafetyPreflight(request);
  expect(result.reason_code == "unknown_anti_cheat_risk",
         "unknown anti-cheat reason mismatch");

  request = approvedPreflightRequest();
  request.anti_cheat_risk = "malformed";
  result = vrclient::injector::safety::runSafetyPreflight(request);
  expect(result.reason_code == "unknown_anti_cheat_risk",
         "malformed anti-cheat risk should refuse");

  request = approvedPreflightRequest();
  request.online_risk = "known_online";
  result = vrclient::injector::safety::runSafetyPreflight(request);
  expect(result.reason_code == "online_mode_risk", "online risk reason mismatch");

  request = approvedPreflightRequest();
  request.online_risk = "multiplayer_unknown";
  result = vrclient::injector::safety::runSafetyPreflight(request);
  expect(result.reason_code == "unknown_online_risk",
         "malformed online risk should refuse");

  request = approvedPreflightRequest();
  request.target.architecture = "x86";
  result = vrclient::injector::safety::runSafetyPreflight(request);
  expect(result.reason_code == "architecture_mismatch",
         "architecture mismatch reason mismatch");

  request = approvedPreflightRequest();
  request.expected_runtime_sha256.clear();
  result = vrclient::injector::safety::runSafetyPreflight(request);
  expect(result.reason_code == "runtime_hash_missing",
         "missing expected runtime hash should refuse");

  request = approvedPreflightRequest();
  request.runtime_binary_sha256 = "not-a-sha";
  result = vrclient::injector::safety::runSafetyPreflight(request);
  expect(result.reason_code == "runtime_hash_missing",
         "malformed observed runtime hash should refuse");

  request = approvedPreflightRequest();
  request.runtime_binary_sha256 =
      "57e39ef45d24ba6d9eaf0f4a06f087d342a1f3c6dc8840e3a8b2afd7b902cb9f";
  result = vrclient::injector::safety::runSafetyPreflight(request);
  expect(result.reason_code == "runtime_hash_mismatch",
         "runtime hash mismatch reason mismatch");

  request = approvedPreflightRequest();
  request.target.runtime_already_loaded = true;
  result = vrclient::injector::safety::runSafetyPreflight(request);
  expect(result.reason_code == "runtime_already_loaded",
         "already loaded reason mismatch");
}

void runRepoCommercialSafetyTests() {
  auto request = repoApprovedPreflightRequest();
  auto result = vrclient::injector::safety::runSafetyPreflight(request);
  expect(result.verdict == vrclient::injector::safety::SafetyVerdict::Approved,
         "R.E.P.O. selected commercial target should approve under private modded co-op policy");
  expect(result.reason_code == "approved", "R.E.P.O. approved reason mismatch");
  std::cout << "repo preflight allowed: approved private_modded_coop known_safe selected_game\n";

  request = repoApprovedPreflightRequest();
  request.multiplayer_session_scope = "public_or_unknown";
  result = vrclient::injector::safety::runSafetyPreflight(request);
  expect(result.reason_code == "private_multiplayer_not_confirmed",
         "R.E.P.O. public/unknown session scope should block");
  std::cout << "repo preflight public/unknown scope block: private_multiplayer_not_confirmed\n";

  request = repoApprovedPreflightRequest();
  request.public_matchmaking_detected = true;
  result = vrclient::injector::safety::runSafetyPreflight(request);
  expect(result.reason_code == "public_matchmaking_risk",
         "R.E.P.O. public matchmaking should block");
  std::cout << "repo preflight public matchmaking block: public_matchmaking_risk\n";

  request = repoApprovedPreflightRequest();
  request.multiplayer_mod_compatibility_confirmed = false;
  result = vrclient::injector::safety::runSafetyPreflight(request);
  expect(result.reason_code == "multiplayer_mod_compatibility_unconfirmed",
         "R.E.P.O. unconfirmed mod compatibility should block");
  std::cout
      << "repo preflight compatibility block: multiplayer_mod_compatibility_unconfirmed\n";

  request = repoApprovedPreflightRequest();
  request.online_risk = "offline_only";
  result = vrclient::injector::safety::runSafetyPreflight(request);
  expect(result.reason_code == "offline_policy_multiplayer_scope",
         "offline-only policy should not approve multiplayer scope");
  std::cout << "repo preflight offline policy multiplayer block: offline_policy_multiplayer_scope\n";

  request = repoApprovedPreflightRequest();
  request.online_risk = "known_online";
  result = vrclient::injector::safety::runSafetyPreflight(request);
  expect(result.reason_code == "online_mode_risk",
         "R.E.P.O. known online policy should block");
  std::cout << "repo preflight known_online block: online_mode_risk\n";

  request = repoApprovedPreflightRequest();
  request.online_risk = "unknown";
  result = vrclient::injector::safety::runSafetyPreflight(request);
  expect(result.reason_code == "unknown_online_risk",
         "R.E.P.O. unknown online policy should block");
  std::cout << "repo preflight unknown online block: unknown_online_risk\n";

  request = repoApprovedPreflightRequest();
  request.anti_cheat_risk = "known_risky";
  result = vrclient::injector::safety::runSafetyPreflight(request);
  expect(result.reason_code == "unsafe_anti_cheat_risk",
         "R.E.P.O. known risky anti-cheat policy should block");
  std::cout << "repo preflight known_risky block: unsafe_anti_cheat_risk\n";

  request = repoApprovedPreflightRequest();
  request.anti_cheat_risk = "unknown";
  result = vrclient::injector::safety::runSafetyPreflight(request);
  expect(result.reason_code == "unknown_anti_cheat_risk",
         "R.E.P.O. unknown anti-cheat policy should block");
  std::cout << "repo preflight unknown anti-cheat block: unknown_anti_cheat_risk\n";
}

void runBootstrapTests() {
  FakeBootstrapLoader loader;
  const auto allow = smokeAllowVerdict();

  vrclient::injector::bootstrap::BootstrapSmokeRequest request;
  request.preflight = approvedPreflightRequest();
  request.safety_verdict = allow;
  request.runtime_loader = &loader;
  request.openxr_state = "ready";

  auto result = vrclient::injector::bootstrap::runBootstrapSmoke(request);
  expect(result.code == vrclient::injector::bootstrap::BootstrapResultCode::Loaded,
         "baseline bootstrap should load");
  expect(result.runtime_load_attempted, "runtime load should be attempted");

  request = {};
  request.preflight = approvedPreflightRequest();
  request.safety_verdict = allow;
  request.preflight.identity.status = vrclient::versioning::DetectionStatus::Unknown;
  request.runtime_loader = &loader;
  result = vrclient::injector::bootstrap::runBootstrapSmoke(request);
  expect(result.code ==
             vrclient::injector::bootstrap::BootstrapResultCode::RefusedIdentity,
         "unknown identity should refuse before preflight");
  expect(!result.runtime_load_attempted, "identity refusal must not attempt load");

  request = {};
  request.preflight = approvedPreflightRequest();
  request.safety_verdict = allow;
  request.preflight.online_risk = "unknown";
  request.runtime_loader = &loader;
  result = vrclient::injector::bootstrap::runBootstrapSmoke(request);
  expect(result.code ==
             vrclient::injector::bootstrap::BootstrapResultCode::RefusedPreflight,
         "preflight refusal should stop bootstrap");
  expect(!result.runtime_load_attempted, "preflight refusal must not attempt load");

  request = {};
  request.preflight = approvedPreflightRequest();
  request.safety_verdict = allow;
  result = vrclient::injector::bootstrap::runBootstrapSmoke(request);
  expect(result.code ==
             vrclient::injector::bootstrap::BootstrapResultCode::RuntimeLoadFailed,
         "missing loader should fail");
  expect(result.reason_code == "runtime_loader_missing",
         "missing loader reason mismatch");
  expect(!result.runtime_load_attempted, "missing loader must not fake a load");

  loader.load_succeeds = false;
  loader.partial_load = true;
  loader.cleanup_succeeds = true;
  request = {};
  request.preflight = approvedPreflightRequest();
  request.safety_verdict = allow;
  request.runtime_loader = &loader;
  result = vrclient::injector::bootstrap::runBootstrapSmoke(request);
  expect(result.code ==
             vrclient::injector::bootstrap::BootstrapResultCode::RuntimeLoadFailed,
         "loader failure should fail bootstrap");
  expect(result.runtime_load_attempted, "loader failure should record attempted load");
  expect(result.cleanup_attempted, "partial load should attempt cleanup");
  expect(result.cleanup_succeeded, "cleanup should report success");

  loader.load_succeeds = true;
  loader.pose_timing_available = false;
  request = {};
  request.preflight = approvedPreflightRequest();
  request.safety_verdict = allow;
  request.runtime_loader = &loader;
  result = vrclient::injector::bootstrap::runBootstrapSmoke(request);
  expect(result.code ==
             vrclient::injector::bootstrap::BootstrapResultCode::PoseTimingUnavailable,
         "pose/timing probe failure should fail bootstrap");
}

// ---------------------------------------------------------------------------
// Phase 7 (T03) — prove the safety verdict gate cannot be bypassed and stops
// BOTH the launch-time (DirectLaunch) and attach-to-running-process
// (AttachRunning) paths before any process touch (the loader call).
// ---------------------------------------------------------------------------

bool diagnosticsContains(
    const vrclient::injector::bootstrap::BootstrapSmokeResult& result,
    const std::string& key,
    const std::string& value) {
  for (const auto& field : result.diagnostics_fields) {
    if (field.key == key && field.value == value) {
      return true;
    }
  }
  return false;
}

// A loader that records whether it was ever asked to touch the target. If the
// gate is honored, loadRuntime() must never run for a refused verdict.
class TrackingBootstrapLoader
    : public vrclient::injector::bootstrap::BootstrapRuntimeLoader {
 public:
  vrclient::injector::bootstrap::RuntimeLoadAttempt loadRuntime(
      const vrclient::injector::safety::SafetyPreflightRequest&) override {
    touched = true;
    vrclient::injector::bootstrap::RuntimeLoadAttempt attempt;
    attempt.loaded = true;
    return attempt;
  }
  bool probePoseTimingApi() override { return true; }
  bool cleanupPartialLoad() override { return true; }

  bool touched = false;
};

void runSafetyGateBootstrapTests() {
  const auto rules = loadSafetyRules();

  // ----- Block prevents launch-time injection (DirectLaunch flow). -----
  // A known anti-cheat indicator on the smoke (DirectLaunch) target yields a
  // real Block verdict; the bootstrap must refuse before the loader is touched.
  auto block_eval = smokeEvaluationRequest();
  block_eval.detection.observed_anti_cheat_indicators = {"EasyAntiCheat"};
  const auto block_verdict =
      vrclient::safety::evaluateSafetyVerdict(block_eval, rules);
  expect(block_verdict.verdict == vrclient::safety::SafetyVerdict::Block,
         "known anti-cheat should produce a Block verdict");
  expect(block_verdict.reason_code == "anti_cheat_detected",
         "block verdict reason mismatch");

  TrackingBootstrapLoader launch_loader;
  vrclient::injector::bootstrap::BootstrapSmokeRequest launch;
  launch.preflight = approvedPreflightRequest();  // DirectLaunch target
  expect(launch.preflight.target.flow ==
             vrclient::injector::process::DiscoveryFlow::DirectLaunch,
         "launch-time fixture must use the DirectLaunch flow");
  launch.safety_verdict = block_verdict;
  launch.runtime_loader = &launch_loader;
  auto launch_result =
      vrclient::injector::bootstrap::runBootstrapSmoke(launch);
  expect(launch_result.code ==
             vrclient::injector::bootstrap::BootstrapResultCode::RefusedSafetyVerdict,
         "block verdict must refuse launch-time injection");
  expect(!launch_result.runtime_load_attempted,
         "block verdict must not attempt a launch-time load");
  expect(!launch_loader.touched,
         "block verdict must not touch the target on the launch path");
  expect(launch_result.reason_code == "anti_cheat_detected",
         "block refusal must carry the safety reason code");
  // Safety reason is included in diagnostics (T03-AC3).
  expect(diagnosticsContains(launch_result, "reason_code", "anti_cheat_detected"),
         "block refusal reason must appear in diagnostics");
  expect(diagnosticsContains(launch_result, "safety_verdict", "block"),
         "block refusal must record the safety verdict in diagnostics");
  expect(diagnosticsContains(launch_result, "safety_message_key",
                             "safety.block.anti_cheat_detected"),
         "block refusal must record the safety message key in diagnostics");

  // ----- Block prevents attach-to-running-process injection (AttachRunning). -----
  // The SAME block verdict on an attach-flow target must also stop before any
  // process touch — the gate is flow-independent because it precedes the loader.
  auto attach_eval = smokeEvaluationRequest();
  attach_eval.preflight.target.flow =
      vrclient::injector::process::DiscoveryFlow::AttachRunning;
  attach_eval.detection.observed_anti_cheat_indicators = {"BattlEye"};
  const auto attach_block =
      vrclient::safety::evaluateSafetyVerdict(attach_eval, rules);
  expect(attach_block.verdict == vrclient::safety::SafetyVerdict::Block,
         "attach-path known anti-cheat should produce a Block verdict");

  TrackingBootstrapLoader attach_loader;
  vrclient::injector::bootstrap::BootstrapSmokeRequest attach;
  attach.preflight = approvedPreflightRequest();
  attach.preflight.target.flow =
      vrclient::injector::process::DiscoveryFlow::AttachRunning;
  attach.safety_verdict = attach_block;
  attach.runtime_loader = &attach_loader;
  auto attach_result =
      vrclient::injector::bootstrap::runBootstrapSmoke(attach);
  expect(attach_result.code ==
             vrclient::injector::bootstrap::BootstrapResultCode::RefusedSafetyVerdict,
         "block verdict must refuse attach-to-running-process injection");
  expect(!attach_result.runtime_load_attempted,
         "block verdict must not attempt an attach load");
  expect(!attach_loader.touched,
         "block verdict must not touch the target on the attach path");

  // ----- No bypass: the injector cannot reach the loader without Allow. -----
  // A default-constructed (UnknownBlocked) verdict — i.e. a caller that simply
  // forgot to evaluate safety — must NOT reach the loader.
  TrackingBootstrapLoader default_loader;
  vrclient::injector::bootstrap::BootstrapSmokeRequest defaulted;
  defaulted.preflight = approvedPreflightRequest();
  defaulted.runtime_loader = &default_loader;
  // No safety_verdict set -> default UnknownBlocked.
  auto default_result =
      vrclient::injector::bootstrap::runBootstrapSmoke(defaulted);
  expect(default_result.code ==
             vrclient::injector::bootstrap::BootstrapResultCode::RefusedSafetyVerdict,
         "a missing/default safety verdict must refuse (fail-closed, no bypass)");
  expect(!default_loader.touched,
         "a missing safety verdict must not touch the target");

  // A Warn verdict WITHOUT acknowledgement must not reach the loader either.
  const auto warn_verdict =
      vrclient::safety::evaluateSafetyVerdict(repoWarnEvaluationRequest(), rules);
  expect(warn_verdict.verdict == vrclient::safety::SafetyVerdict::Warn,
         "repo evaluation should resolve to Warn for the gate test");
  TrackingBootstrapLoader warn_loader;
  vrclient::injector::bootstrap::BootstrapSmokeRequest warn;
  warn.preflight = repoApprovedPreflightRequest();
  warn.safety_verdict = warn_verdict;
  warn.acknowledged_risk = false;
  warn.runtime_loader = &warn_loader;
  auto warn_result = vrclient::injector::bootstrap::runBootstrapSmoke(warn);
  expect(warn_result.code ==
             vrclient::injector::bootstrap::BootstrapResultCode::RefusedSafetyVerdict,
         "an unacknowledged warn verdict must refuse before the loader");
  expect(!warn_loader.touched,
         "an unacknowledged warn verdict must not touch the target");

  // A Warn verdict WITH explicit acknowledgement is permitted to proceed.
  TrackingBootstrapLoader ack_loader;
  vrclient::injector::bootstrap::BootstrapSmokeRequest acked;
  acked.preflight = repoApprovedPreflightRequest();
  acked.safety_verdict = warn_verdict;
  acked.acknowledged_risk = true;
  acked.runtime_loader = &ack_loader;
  auto acked_result = vrclient::injector::bootstrap::runBootstrapSmoke(acked);
  expect(acked_result.code ==
             vrclient::injector::bootstrap::BootstrapResultCode::Loaded,
         "an acknowledged warn verdict may proceed to load");
  expect(ack_loader.touched,
         "an acknowledged warn verdict reaches the loader");

  // ----- Allow proceeds and records the safety reason in diagnostics. -----
  TrackingBootstrapLoader allow_loader;
  vrclient::injector::bootstrap::BootstrapSmokeRequest allowed;
  allowed.preflight = approvedPreflightRequest();
  allowed.safety_verdict = smokeAllowVerdict();
  allowed.runtime_loader = &allow_loader;
  auto allow_result = vrclient::injector::bootstrap::runBootstrapSmoke(allowed);
  expect(allow_result.code ==
             vrclient::injector::bootstrap::BootstrapResultCode::Loaded,
         "an allow verdict proceeds to load");
  expect(allow_loader.touched, "an allow verdict reaches the loader");
  expect(diagnosticsContains(allow_result, "safety_reason_code", "approved"),
         "the allow path must record the safety reason in diagnostics");

  std::cout << "safety gate bootstrap: block stops launch + attach before touch; "
               "no-verdict/warn refuse; allow proceeds; reason in diagnostics\n";
}

}  // namespace

int main() {
  runDiscoveryChoiceTest();
  runAttachRaceTests();
  runMissingExecutableTests();
  runSafetyRefusalTests();
  runRepoCommercialSafetyTests();
  runBootstrapTests();
  runSafetyGateBootstrapTests();
  return 0;
}

#include "injector/process/process_discovery.h"
#include "versioning/game_fingerprint.h"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace {

constexpr const char* kSupportedHash =
    "a4a4893f8f2befed4daa6785f736cf32aa588af41553b3730b683b13fdc4efd7";
constexpr const char* kUnsupportedHash =
    "c4455457d2081f1df5f3b6850f849664125cc89a0e58fcfd6ff86ef6d1ad1315";
constexpr const char* kUnknownHash =
    "57e39ef45d24ba6d9eaf0f4a06f087d342a1f3c6dc8840e3a8b2afd7b902cb9f";
constexpr const char* kRepoHash =
    "412f7cf79cf16888999e22905ca3bb11a6074857efdf8c754073a47ef872317c";

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

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

vrclient::injector::process::TargetDescriptor makeTarget() {
  vrclient::injector::process::TargetDescriptor target;
  target.flow = vrclient::injector::process::DiscoveryFlow::DirectLaunch;
  target.executable_path = "C:/VRClientSmoke/vrclient_smoke_host.exe";
  target.game_id_hint = "vrclient-smoke-host";
  target.architecture = "x64";
  target.executable_sha256 = kSupportedHash;
  target.product_version = "0.1.0-smoke";
  target.identity_evidence_trusted = true;
  target.identity_evidence_source = "test_file_probe";
  return target;
}

vrclient::injector::process::TargetDescriptor makeRepoTarget() {
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

void runSupportedHashTest(const vrclient::versioning::GameFingerprintConfig& config) {
  const auto result = vrclient::versioning::detectVersion(makeTarget(), config);
  expect(result.status == vrclient::versioning::DetectionStatus::KnownSupported,
         "supported hash should be known-supported");
  expect(result.build_id == "smoke-2026-06-11", "supported build id mismatch");
  expect(result.confidence == 100, "supported hash confidence mismatch");
  expect(!result.evidence.empty(), "supported hash should include evidence");
}

void runUnsupportedHashTest(const vrclient::versioning::GameFingerprintConfig& config) {
  auto target = makeTarget();
  target.executable_sha256 = kUnsupportedHash;
  target.product_version = "0.0.9-smoke";
  const auto result = vrclient::versioning::detectVersion(target, config);
  expect(result.status == vrclient::versioning::DetectionStatus::KnownUnsupported,
         "unsupported hash should be known-unsupported");
  expect(result.reason_code == "unsupported_sample_build",
         "unsupported reason mismatch");
}

void runUnknownBuildTest(const vrclient::versioning::GameFingerprintConfig& config) {
  auto target = makeTarget();
  target.executable_sha256 = kUnknownHash;
  target.product_version = "9.9.9";
  const auto result = vrclient::versioning::detectVersion(target, config);
  expect(result.status == vrclient::versioning::DetectionStatus::Unknown,
         "unknown hash should be unknown");
  expect(result.reason_code == "unknown_build", "unknown reason mismatch");
  expect(result.refusal_ready, "unknown builds must be refusal-ready");
}

void runSignatureMatchTest(const vrclient::versioning::GameFingerprintConfig& config) {
  auto target = makeTarget();
  target.executable_sha256.clear();
  target.product_version.clear();
  target.signature_tags = {"smoke-main-banner"};
  const auto result = vrclient::versioning::detectVersion(target, config);
  expect(result.status == vrclient::versioning::DetectionStatus::KnownSupported,
         "trusted signature should identify supported build");
  expect(result.build_id == "smoke-2026-06-11", "signature build id mismatch");
  expect(result.confidence == 85, "signature confidence mismatch");
}

void runUntrustedSpoofTest(const vrclient::versioning::GameFingerprintConfig& config) {
  auto target = makeTarget();
  target.identity_evidence_trusted = false;
  target.identity_evidence_source = "caller_supplied";
  const auto result = vrclient::versioning::detectVersion(target, config);
  expect(result.status == vrclient::versioning::DetectionStatus::Unknown,
         "untrusted evidence must not identify a supported build");
  expect(result.reason_code == "untrusted_identity_evidence",
         "untrusted evidence reason mismatch");
}

void runExecutableNameMismatchTest(
    const vrclient::versioning::GameFingerprintConfig& config) {
  auto target = makeTarget();
  target.executable_path = "C:/VRClientSmoke/not_the_smoke_host.exe";
  const auto result = vrclient::versioning::detectVersion(target, config);
  expect(result.status == vrclient::versioning::DetectionStatus::Unknown,
         "wrong executable name must not identify a supported build");
  expect(result.reason_code == "executable_name_mismatch",
         "executable name mismatch reason mismatch");
}

void runAmbiguousBuildTest(vrclient::versioning::GameFingerprintConfig config) {
  auto duplicate = config.builds.front();
  duplicate.build_id = "smoke-duplicate";
  config.builds.push_back(duplicate);
  const auto result = vrclient::versioning::detectVersion(makeTarget(), config);
  expect(result.status == vrclient::versioning::DetectionStatus::Ambiguous,
         "duplicate matching builds should be ambiguous");
  expect(result.reason_code == "ambiguous_build", "ambiguous reason mismatch");
  expect(result.refusal_ready, "ambiguous builds must be refusal-ready");
}

void runRepoSupportedBuildTest(
    const vrclient::versioning::GameFingerprintConfig& config) {
  const auto result = vrclient::versioning::detectVersion(makeRepoTarget(), config);
  expect(result.status == vrclient::versioning::DetectionStatus::KnownSupported,
         "R.E.P.O. pinned hash should be known-supported");
  expect(result.build_id == "steam-3241660-build-23363152",
         "R.E.P.O. build id mismatch");
  expect(result.confidence == 100, "R.E.P.O. hash confidence mismatch");
  expect(!result.refusal_ready, "supported R.E.P.O. build should not be refusal-ready");
  expect(result.support_policy.anti_cheat_risk == "known_safe",
         "R.E.P.O. anti-cheat policy mismatch");
  expect(result.support_policy.online_risk == "private_modded_coop",
         "R.E.P.O. online policy mismatch");
}

void runRepoWrongHashTest(
    const vrclient::versioning::GameFingerprintConfig& config) {
  auto target = makeRepoTarget();
  target.executable_sha256 = kUnknownHash;
  target.product_version = "2022.3.67f2 (6bedba8691df)";
  const auto result = vrclient::versioning::detectVersion(target, config);
  expect(result.status == vrclient::versioning::DetectionStatus::Unknown,
         "R.E.P.O. wrong hash should be unknown");
  expect(result.reason_code == "unknown_build", "R.E.P.O. wrong hash reason mismatch");
  expect(result.refusal_ready, "R.E.P.O. wrong hash must be refusal-ready");
}

void runRepoWrongExecutableTest(
    const vrclient::versioning::GameFingerprintConfig& config) {
  auto target = makeRepoTarget();
  target.executable_path = "H:/SteamLibrary/steamapps/common/REPO/not_REPO.exe";
  const auto result = vrclient::versioning::detectVersion(target, config);
  expect(result.status == vrclient::versioning::DetectionStatus::Unknown,
         "R.E.P.O. wrong executable should be unknown");
  expect(result.reason_code == "executable_name_mismatch",
         "R.E.P.O. wrong executable reason mismatch");
  expect(result.refusal_ready, "R.E.P.O. wrong executable must be refusal-ready");
}

void runRepoUntrustedEvidenceTest(
    const vrclient::versioning::GameFingerprintConfig& config) {
  auto target = makeRepoTarget();
  target.identity_evidence_trusted = false;
  target.identity_evidence_source = "caller_supplied";
  const auto result = vrclient::versioning::detectVersion(target, config);
  expect(result.status == vrclient::versioning::DetectionStatus::Unknown,
         "R.E.P.O. untrusted evidence should be unknown");
  expect(result.reason_code == "untrusted_identity_evidence",
         "R.E.P.O. untrusted evidence reason mismatch");
  expect(result.refusal_ready, "R.E.P.O. untrusted evidence must be refusal-ready");
}

}  // namespace

int main() {
  const auto loaded = vrclient::versioning::loadGameFingerprintConfig(sampleConfigPath());
  expect(loaded.loaded, loaded.message);
  expect(loaded.config.controlled_smoke_target, "sample must be a controlled target");
  expect(!loaded.config.runtime.sha256.empty(), "runtime hash must be present");

  runSupportedHashTest(loaded.config);
  runUnsupportedHashTest(loaded.config);
  runUnknownBuildTest(loaded.config);
  runSignatureMatchTest(loaded.config);
  runUntrustedSpoofTest(loaded.config);
  runExecutableNameMismatchTest(loaded.config);
  runAmbiguousBuildTest(loaded.config);

  const auto loaded_repo =
      vrclient::versioning::loadGameFingerprintConfig(repoConfigPath());
  expect(loaded_repo.loaded, loaded_repo.message);
  expect(!loaded_repo.config.controlled_smoke_target,
         "R.E.P.O. must be a selected commercial target, not controlled smoke");
  expect(loaded_repo.config.game_id == "repo", "R.E.P.O. game id mismatch");
  runRepoSupportedBuildTest(loaded_repo.config);
  runRepoWrongHashTest(loaded_repo.config);
  runRepoWrongExecutableTest(loaded_repo.config);
  runRepoUntrustedEvidenceTest(loaded_repo.config);
  return 0;
}

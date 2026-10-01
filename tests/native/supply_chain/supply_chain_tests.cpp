#include "supply_chain/artifact_signing.h"

#include "injector/process/process_discovery.h"
#include "injector/safety/safety_preflight.h"
#include "safety/safety_verdict.h"
#include "versioning/game_fingerprint.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::filesystem::path repoRoot() {
#if defined(VRCLIENT_SOURCE_DIR)
  return std::filesystem::path(VRCLIENT_SOURCE_DIR);
#else
  return std::filesystem::current_path();
#endif
}

std::filesystem::path defaultRulesPath() {
  return repoRoot() / "config" / "safety" / "default-rules.json";
}

std::filesystem::path sampleConfigPath() {
  return repoRoot() / "config" / "games" / "sample-game.json";
}

vrclient::supply_chain::TrustRoot loadTrustRoot() {
  const auto loaded = vrclient::supply_chain::loadTrustRoot(
      repoRoot() / "config" / "supply-chain" / "trust-root.test.json");
  expect(loaded.loaded, loaded.message);
  return loaded.trust_root;
}

vrclient::supply_chain::ArtifactManifest loadManifest() {
  const auto loaded = vrclient::supply_chain::loadArtifactManifest(
      repoRoot() / "config" / "supply-chain" / "manifest.test.json");
  expect(loaded.loaded, loaded.message);
  return loaded.manifest;
}

vrclient::supply_chain::RevocationList loadRevocations(
    const std::string& filename) {
  const auto loaded = vrclient::supply_chain::loadRevocationList(
      repoRoot() / "config" / "supply-chain" / filename);
  expect(loaded.loaded, loaded.message);
  return loaded.revocations;
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

vrclient::versioning::VersionDetectionResult supportedIdentity() {
  const auto loaded =
      vrclient::versioning::loadGameFingerprintConfig(sampleConfigPath());
  expect(loaded.loaded, loaded.message);
  return vrclient::versioning::detectVersion(supportedTarget(), loaded.config);
}

vrclient::safety::SafetyRuleSet loadSafetyRules() {
  const auto loaded = vrclient::safety::loadSafetyRuleSet(defaultRulesPath());
  expect(loaded.loaded, loaded.message);
  return loaded.rule_set;
}

vrclient::safety::SafetyEvaluationRequest smokeRequest() {
  const auto identity = supportedIdentity();
  vrclient::safety::SafetyEvaluationRequest request;
  request.identity = identity;
  request.preflight = vrclient::injector::safety::makePreflightRequest(
      supportedTarget(), identity, true, true, identity.runtime.sha256);
  request.adapter_id = "phase3-smoke-adapter";
  request.requested_launch_mode = "offline";
  request.expected_rule_set_version = "2026-06-13.1";
  request.detection.module_enumeration_complete = true;
  request.detection.modding_posture =
      vrclient::safety::ModdingPosture::CommunitySupported;
  return request;
}

vrclient::safety::SafetyEvaluationRequest::ArtifactTrustObservation
toSafetyObservation(const vrclient::supply_chain::ArtifactVerificationResult& result) {
  using SafetyState = vrclient::safety::SafetyEvaluationRequest::ArtifactTrustState;
  SafetyState state = SafetyState::InvalidConfig;
  switch (result.state) {
    case vrclient::supply_chain::ArtifactTrustState::Trusted:
      state = SafetyState::Trusted;
      break;
    case vrclient::supply_chain::ArtifactTrustState::Unsigned:
      state = SafetyState::Unsigned;
      break;
    case vrclient::supply_chain::ArtifactTrustState::HashMismatch:
      state = SafetyState::HashMismatch;
      break;
    case vrclient::supply_chain::ArtifactTrustState::SignatureInvalid:
      state = SafetyState::SignatureInvalid;
      break;
    case vrclient::supply_chain::ArtifactTrustState::UntrustedKey:
      state = SafetyState::UntrustedKey;
      break;
    case vrclient::supply_chain::ArtifactTrustState::Revoked:
      state = SafetyState::Revoked;
      break;
    case vrclient::supply_chain::ArtifactTrustState::UnsupportedAlgorithm:
      state = SafetyState::UnsupportedAlgorithm;
      break;
    case vrclient::supply_chain::ArtifactTrustState::UnreadableArtifact:
      state = SafetyState::UnreadableArtifact;
      break;
    case vrclient::supply_chain::ArtifactTrustState::InvalidConfig:
      state = SafetyState::InvalidConfig;
      break;
  }

  vrclient::safety::SafetyEvaluationRequest::ArtifactTrustObservation obs;
  obs.artifact_id = result.artifact_id;
  obs.artifact_type = result.artifact_type;
  obs.state = state;
  obs.reason_code = result.reason_code;
  return obs;
}

void runSignedArtifactsTrustedTest() {
  const auto trust_root = loadTrustRoot();
  const auto manifest = loadManifest();
  const auto revocations = loadRevocations("revocations.empty.test.json");

  bool saw_runtime = false;
  bool saw_adapter = false;
  bool saw_config = false;
  for (const auto& artifact : manifest.artifacts) {
    const auto result = vrclient::supply_chain::verifyArtifact(
        artifact, trust_root, revocations, repoRoot());
    expect(result.state == vrclient::supply_chain::ArtifactTrustState::Trusted,
           "signed artifact should verify as trusted: " + artifact.artifact_id +
               " -> " + result.reason_code + " / " + result.detail);
    saw_runtime = saw_runtime || artifact.artifact_type == "runtime-dll";
    saw_adapter = saw_adapter || artifact.artifact_type == "adapter-dll";
    saw_config = saw_config || artifact.artifact_type == "config-bundle";
  }
  expect(saw_runtime && saw_adapter && saw_config,
         "manifest must cover runtime DLL, adapter DLL, and config bundle fixtures");
  std::cout << "supply-chain: signed runtime/adapter/config fixtures trusted\n";
}

void runUnsignedTamperedRevokedTests() {
  const auto trust_root = loadTrustRoot();
  const auto manifest = loadManifest();
  const auto empty_revocations = loadRevocations("revocations.empty.test.json");
  const auto revoked_runtime =
      loadRevocations("revocations.revoked-runtime.test.json");

  const auto* runtime = manifest.findArtifact("vrclient-runtime-fixture");
  expect(runtime != nullptr, "runtime artifact fixture missing");

  auto unsigned_runtime = *runtime;
  unsigned_runtime.signatures.clear();
  auto result = vrclient::supply_chain::verifyArtifact(
      unsigned_runtime, trust_root, empty_revocations, repoRoot());
  expect(result.state == vrclient::supply_chain::ArtifactTrustState::Unsigned,
         "unsigned artifact must be refused as unsigned");
  expect(result.reason_code == "artifact_unsigned",
         "unsigned reason code mismatch");

  auto tampered_runtime = *runtime;
  tampered_runtime.relative_path =
      "tests/native/supply_chain/fixtures/tampered-runtime.dll.fixture";
  result = vrclient::supply_chain::verifyArtifact(
      tampered_runtime, trust_root, empty_revocations, repoRoot());
  expect(result.state == vrclient::supply_chain::ArtifactTrustState::HashMismatch,
         "tampered artifact must fail the hash layer before signature trust");
  expect(result.reason_code == "artifact_hash_mismatch",
         "tampered reason code mismatch");

  auto bad_signature = *runtime;
  bad_signature.signatures[0].signature_hex[0] =
      bad_signature.signatures[0].signature_hex[0] == '0' ? '1' : '0';
  result = vrclient::supply_chain::verifyArtifact(
      bad_signature, trust_root, empty_revocations, repoRoot());
  expect(result.state ==
             vrclient::supply_chain::ArtifactTrustState::SignatureInvalid,
         "mutated signature must fail trust-root verification");
  expect(result.reason_code == "artifact_signature_invalid",
         "signature-invalid reason code mismatch");

  result = vrclient::supply_chain::verifyArtifact(
      *runtime, trust_root, revoked_runtime, repoRoot());
  expect(result.state == vrclient::supply_chain::ArtifactTrustState::Revoked,
         "revoked runtime artifact must be refused");
  expect(result.reason_code == "artifact_revoked",
         "revoked reason code mismatch");
  std::cout << "supply-chain: unsigned/tampered/revoked fixtures refused\n";
}

void runSafetyIntegrationTests() {
  const auto rules = loadSafetyRules();
  const auto trust_root = loadTrustRoot();
  const auto manifest = loadManifest();
  const auto empty_revocations = loadRevocations("revocations.empty.test.json");
  const auto revoked_runtime =
      loadRevocations("revocations.revoked-runtime.test.json");

  const auto* runtime = manifest.findArtifact("vrclient-runtime-fixture");
  expect(runtime != nullptr, "runtime artifact fixture missing");

  {
    auto unsigned_runtime = *runtime;
    unsigned_runtime.signatures.clear();
    const auto verification = vrclient::supply_chain::verifyArtifact(
        unsigned_runtime, trust_root, empty_revocations, repoRoot());
    auto request = smokeRequest();
    request.artifact_trust.push_back(toSafetyObservation(verification));
    const auto verdict =
        vrclient::safety::evaluateSafetyVerdict(request, rules);
    expect(verdict.verdict == vrclient::safety::SafetyVerdict::Block,
           "unsigned artifact trust observation must block safety verdict");
    expect(verdict.reason_code == "artifact_unsigned",
           "unsigned artifact safety reason mismatch");
  }

  {
    const auto verification = vrclient::supply_chain::verifyArtifact(
        *runtime, trust_root, revoked_runtime, repoRoot());
    auto request = smokeRequest();
    request.artifact_trust.push_back(toSafetyObservation(verification));
    const auto verdict =
        vrclient::safety::evaluateSafetyVerdict(request, rules);
    expect(verdict.verdict == vrclient::safety::SafetyVerdict::Block,
           "revoked artifact trust observation must block safety verdict");
    expect(verdict.reason_code == "artifact_revoked",
           "revoked artifact safety reason mismatch");
  }
  std::cout << "supply-chain: unsigned/revoked artifacts feed Phase 7 block\n";
}

}  // namespace

int main() {
  runSignedArtifactsTrustedTest();
  runUnsignedTamperedRevokedTests();
  runSafetyIntegrationTests();
  std::cout << "supply-chain tests passed\n";
  return 0;
}

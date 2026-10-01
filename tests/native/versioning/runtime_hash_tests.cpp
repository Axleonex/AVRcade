// Phase 3 closure: runtime-binary HASH capture against a REAL built artifact.
//
// Proves the fingerprint/versioning layer identifies a real binary by its
// SHA-256. The project has NO compute-from-bytes SHA-256 API (every existing
// sha256 site CONSUMES a supplied hex string), so this test computes the digest
// of a real built .exe via the Windows BCrypt (CNG) API — the same primitive the
// runtime fingerprinter would use — then:
//   1. Feeds the computed hash through detectVersion() via a TargetDescriptor and
//      an in-memory GameFingerprintConfig built around the SAME hash, asserting
//      status == KnownSupported, confidence == 100, and an evidence entry
//      {type:"sha256", value:<computed>, confidence:100}. -> the versioning layer
//      identifies the REAL binary by hash.
//   2. Independently recomputes the SHA-256 of the SAME file via a second,
//      separate BCrypt pass and asserts equality (in-process cross-check), and
//      prints `runtime_sha256=<hex>` so the Python validator
//      (validate_runtime_binary_hash.py) can cross-check with hashlib (a fully
//      independent implementation) — see that script.
//
// The artifact path is passed by CMake as argv[1] = $<TARGET_FILE:vrclient_smoke_host>
// (a REAL .exe built in the DEFAULT OpenXR-OFF config). No fixture, no constant.

#include "injector/process/process_discovery.h"
#include "versioning/game_fingerprint.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <windows.h>
#include <bcrypt.h>

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

constexpr NTSTATUS kStatusSuccess = 0;

// Compute the SHA-256 of a file's bytes using the Windows CNG (BCrypt) API and
// return the lowercase 64-hex digest. This is a from-bytes hash of the REAL
// artifact, computed by the test (the project ships no such API). Throws on any
// CNG/file error so a broken hash can never silently false-green.
std::string computeFileSha256(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::in | std::ios::binary);
  expect(static_cast<bool>(input), "artifact must be readable for hashing: " + path.string());

  BCRYPT_ALG_HANDLE algorithm = nullptr;
  expect(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) ==
             kStatusSuccess,
         "BCryptOpenAlgorithmProvider(SHA256) failed");

  DWORD object_length = 0;
  DWORD copied = 0;
  expect(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                           reinterpret_cast<PUCHAR>(&object_length), sizeof(object_length),
                           &copied, 0) == kStatusSuccess,
         "BCryptGetProperty(OBJECT_LENGTH) failed");

  DWORD hash_length = 0;
  expect(BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                           reinterpret_cast<PUCHAR>(&hash_length), sizeof(hash_length),
                           &copied, 0) == kStatusSuccess,
         "BCryptGetProperty(HASH_LENGTH) failed");
  expect(hash_length == 32, "SHA-256 digest length must be 32 bytes");

  std::vector<UCHAR> hash_object(object_length);
  BCRYPT_HASH_HANDLE hash = nullptr;
  expect(BCryptCreateHash(algorithm, &hash, hash_object.data(), object_length, nullptr, 0, 0) ==
             kStatusSuccess,
         "BCryptCreateHash failed");

  std::array<char, 64 * 1024> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize read_count = input.gcount();
    if (read_count > 0) {
      expect(BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()),
                            static_cast<ULONG>(read_count), 0) == kStatusSuccess,
             "BCryptHashData failed");
    }
  }

  std::vector<UCHAR> digest(hash_length);
  expect(BCryptFinishHash(hash, digest.data(), hash_length, 0) == kStatusSuccess,
         "BCryptFinishHash failed");

  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);

  static constexpr char kHex[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(hash_length * 2);
  for (UCHAR byte : digest) {
    hex.push_back(kHex[byte >> 4]);
    hex.push_back(kHex[byte & 0x0F]);
  }
  return hex;
}

void runRuntimeBinaryHashIdentifiesRealArtifact(const std::filesystem::path& artifact_path) {
  expect(std::filesystem::exists(artifact_path),
         "built artifact must exist: " + artifact_path.string());

  // Compute the SHA-256 of the REAL built binary.
  const std::string runtime_sha256 = computeFileSha256(artifact_path);
  expect(runtime_sha256.size() == 64, "computed SHA-256 must be 64 hex chars");

  // Independent in-process cross-check: a second, separate BCrypt pass over the
  // same file must agree byte-for-byte. (The Python validator provides a fully
  // independent hashlib cross-check as well.)
  const std::string runtime_sha256_recheck = computeFileSha256(artifact_path);
  expect(runtime_sha256 == runtime_sha256_recheck,
         "two independent BCrypt passes over the artifact must agree");

  const std::string artifact_filename = artifact_path.filename().string();

  // Build an in-memory fingerprint config keyed on the COMPUTED hash. Fill every
  // field configValidForPhase3() requires so the config is genuinely valid (not a
  // degenerate shortcut).
  vrclient::versioning::GameFingerprintConfig config;
  config.version = 1;
  config.game_id = "vrclient-runtime-hash-probe";
  config.display_name = "VRClient Runtime Hash Probe";
  config.executable_names = {artifact_filename};
  config.support_policy.allowed_sources = {"controlled_smoke_target"};
  config.support_policy.target_architecture = "x64";
  config.support_policy.anti_cheat_risk = "none";
  config.support_policy.online_risk = "offline_only";
  config.runtime.path = artifact_path;
  config.runtime.sha256 = runtime_sha256;
  config.runtime.architecture = "x64";

  vrclient::versioning::BuildFingerprint build;
  build.build_id = "runtime-hash-probe-build";
  build.supported = true;
  build.confidence_threshold = 80;
  build.file_hashes.push_back({"sha256", runtime_sha256});
  config.builds.push_back(build);

  // Describe the REAL binary as a trusted target carrying the computed hash.
  vrclient::injector::process::TargetDescriptor target;
  target.executable_path = artifact_path;
  target.executable_sha256 = runtime_sha256;
  target.identity_evidence_trusted = true;
  target.identity_evidence_source = "runtime_hash_probe";

  const auto result = vrclient::versioning::detectVersion(target, config);

  expect(result.status == vrclient::versioning::DetectionStatus::KnownSupported,
         std::string("versioning must identify the real binary as KnownSupported (got ") +
             vrclient::versioning::detectionStatusName(result.status) + ")");
  expect(result.confidence == 100,
         "sha256 exact match must yield confidence 100, got " +
             std::to_string(result.confidence));
  expect(result.build_id == "runtime-hash-probe-build",
         "matched build id must be the supported build");

  bool found_sha256_evidence = false;
  for (const auto& evidence : result.evidence) {
    if (evidence.type == "sha256" && evidence.value == runtime_sha256 &&
        evidence.confidence == 100) {
      found_sha256_evidence = true;
    }
  }
  expect(found_sha256_evidence,
         "detection evidence must include the computed sha256 at confidence 100");

  // Negative control: a target whose hash is altered must NOT match (proves the
  // identification is the hash, not the filename/path).
  std::string altered = runtime_sha256;
  altered[0] = (altered[0] == 'a') ? 'b' : 'a';
  vrclient::injector::process::TargetDescriptor mismatched = target;
  mismatched.executable_sha256 = altered;
  const auto mismatch_result = vrclient::versioning::detectVersion(mismatched, config);
  expect(mismatch_result.status != vrclient::versioning::DetectionStatus::KnownSupported,
         "a binary with a different hash must not be identified as the supported build");

  // Emit the computed hash + artifact path for the independent Python cross-check.
  std::printf("runtime_sha256=%s\n", runtime_sha256.c_str());
  std::printf("runtime_artifact=%s\n", artifact_path.string().c_str());
  std::fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr,
                 "usage: vr_runtime_hash_tests <path-to-built-artifact>\n"
                 "  (CMake passes $<TARGET_FILE:vrclient_smoke_host>)\n");
    return 2;
  }
  try {
    runRuntimeBinaryHashIdentifiesRealArtifact(std::filesystem::path(argv[1]));
  } catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
  return 0;
}

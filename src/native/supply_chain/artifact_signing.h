#pragma once

// Bolt-on Phase B6: supply-chain artifact signing and revocation.
//
// Phase 8's package backend does not exist yet, so this module is a standalone
// client-side verifier over schema-guarded manifest fixtures. It layers a
// cryptographic RSA/SHA-256 signature check on top of the manifest's SHA-256
// entry and a config-backed revocation list. It performs no network I/O and does
// not load the artifact; callers pass the resulting trust state into the Phase 7
// safety verdict model.

#include <filesystem>
#include <string>
#include <vector>

namespace vrclient::supply_chain {

struct SigningKey {
  std::string key_id;
  std::string algorithm;
  std::string issuer_root_id;
  std::string rsa_modulus_hex;
  std::string rsa_public_exponent_hex;
  std::vector<std::string> usage;
};

struct TrustRoot {
  int version = 0;
  std::string root_id;
  std::string root_version;
  std::vector<SigningKey> trusted_keys;

  [[nodiscard]] bool loaded() const { return version >= 1; }
  [[nodiscard]] const SigningKey* findKey(const std::string& key_id) const;
};

struct ArtifactSignature {
  std::string key_id;
  std::string algorithm;
  std::string signature_hex;
};

struct ManifestArtifact {
  std::string artifact_id;
  std::string artifact_type;
  std::string artifact_version;
  std::filesystem::path relative_path;
  std::string sha256;
  std::vector<ArtifactSignature> signatures;
};

struct ArtifactManifest {
  int version = 0;
  std::string manifest_id;
  std::string bundle_version;
  std::vector<ManifestArtifact> artifacts;

  [[nodiscard]] bool loaded() const { return version >= 1; }
  [[nodiscard]] const ManifestArtifact* findArtifact(
      const std::string& artifact_id) const;
};

struct RevokedArtifact {
  std::string artifact_id;
  std::string sha256;
  std::string reason;
};

struct RevocationList {
  int version = 0;
  std::string list_id;
  std::vector<std::string> revoked_key_ids;
  std::vector<RevokedArtifact> revoked_artifacts;

  [[nodiscard]] bool loaded() const { return version >= 1; }
  [[nodiscard]] bool isKeyRevoked(const std::string& key_id) const;
  [[nodiscard]] const RevokedArtifact* findRevokedArtifact(
      const std::string& artifact_id,
      const std::string& sha256) const;
};

struct ConfigLoadResult {
  bool loaded = false;
  std::string message;
  TrustRoot trust_root;
  ArtifactManifest manifest;
  RevocationList revocations;
};

enum class ArtifactTrustState {
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

const char* artifactTrustStateName(ArtifactTrustState state);

struct ArtifactVerificationResult {
  ArtifactTrustState state = ArtifactTrustState::InvalidConfig;
  std::string reason_code = "artifact_invalid_config";
  std::string artifact_id;
  std::string artifact_type;
  std::string signing_key_id;
  std::filesystem::path artifact_path;
  std::string expected_sha256;
  std::string observed_sha256;
  std::string detail;

  [[nodiscard]] bool trusted() const {
    return state == ArtifactTrustState::Trusted;
  }
};

ConfigLoadResult loadTrustRoot(const std::filesystem::path& path);
ConfigLoadResult loadArtifactManifest(const std::filesystem::path& path);
ConfigLoadResult loadRevocationList(const std::filesystem::path& path);

ArtifactVerificationResult verifyArtifact(
    const ManifestArtifact& artifact,
    const TrustRoot& trust_root,
    const RevocationList& revocations,
    const std::filesystem::path& artifact_base_directory);

std::string canonicalArtifactSigningPayload(
    const ManifestArtifact& artifact,
    const std::string& signing_key_id);

}  // namespace vrclient::supply_chain

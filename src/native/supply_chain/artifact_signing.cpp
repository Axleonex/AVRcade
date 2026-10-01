#include "supply_chain/artifact_signing.h"

#include "config/config_versioning.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <utility>

#if defined(_WIN32)
#  include <windows.h>
#  include <bcrypt.h>
#endif

namespace vrclient::supply_chain {
namespace {

std::string readText(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::in | std::ios::binary);
  if (!input) {
    return {};
  }
  return std::string(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::in | std::ios::binary);
  if (!input) {
    return {};
  }
  return std::vector<std::uint8_t>(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
}

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

bool isHex(std::string_view value) {
  return !value.empty() &&
      std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isxdigit(ch) != 0;
      });
}

std::vector<std::uint8_t> hexToBytes(std::string_view value) {
  auto nibble = [](char ch) -> int {
    if (ch >= '0' && ch <= '9') {
      return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
      return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
      return ch - 'A' + 10;
    }
    return -1;
  };

  if ((value.size() % 2) != 0 || !isHex(value)) {
    return {};
  }
  std::vector<std::uint8_t> out;
  out.reserve(value.size() / 2);
  for (std::size_t i = 0; i < value.size(); i += 2) {
    const int hi = nibble(value[i]);
    const int lo = nibble(value[i + 1]);
    if (hi < 0 || lo < 0) {
      return {};
    }
    out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
  }
  return out;
}

std::string bytesToHex(const std::vector<std::uint8_t>& bytes) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 2);
  for (std::uint8_t byte : bytes) {
    out.push_back(kHex[(byte >> 4) & 0x0f]);
    out.push_back(kHex[byte & 0x0f]);
  }
  return out;
}

#if defined(_WIN32)
constexpr NTSTATUS kStatusSuccess = 0;

std::vector<std::uint8_t> sha256Bytes(
    const std::uint8_t* data,
    std::size_t size) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  if (BCryptOpenAlgorithmProvider(
          &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != kStatusSuccess) {
    return {};
  }

  DWORD object_length = 0;
  DWORD result_length = 0;
  if (BCryptGetProperty(
          algorithm, BCRYPT_OBJECT_LENGTH,
          reinterpret_cast<PUCHAR>(&object_length), sizeof(object_length),
          &result_length, 0) != kStatusSuccess) {
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return {};
  }

  DWORD hash_length = 0;
  if (BCryptGetProperty(
          algorithm, BCRYPT_HASH_LENGTH,
          reinterpret_cast<PUCHAR>(&hash_length), sizeof(hash_length),
          &result_length, 0) != kStatusSuccess ||
      hash_length != 32) {
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return {};
  }

  std::vector<std::uint8_t> hash_object(object_length);
  BCRYPT_HASH_HANDLE hash = nullptr;
  if (BCryptCreateHash(
          algorithm, &hash, hash_object.data(), object_length, nullptr, 0, 0) !=
      kStatusSuccess) {
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return {};
  }
  const bool hash_ok =
      BCryptHashData(
          hash, reinterpret_cast<PUCHAR>(const_cast<std::uint8_t*>(data)),
          static_cast<ULONG>(size), 0) == kStatusSuccess;
  std::vector<std::uint8_t> digest(hash_length);
  const bool finish_ok = hash_ok &&
      BCryptFinishHash(hash, digest.data(), hash_length, 0) == kStatusSuccess;
  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  return finish_ok ? digest : std::vector<std::uint8_t>{};
}

bool verifyRsaPkcs1Sha256(
    const SigningKey& key,
    const std::string& payload,
    const ArtifactSignature& signature) {
  const std::vector<std::uint8_t> modulus = hexToBytes(key.rsa_modulus_hex);
  const std::vector<std::uint8_t> exponent =
      hexToBytes(key.rsa_public_exponent_hex);
  const std::vector<std::uint8_t> signature_bytes =
      hexToBytes(signature.signature_hex);
  if (modulus.empty() || exponent.empty() || signature_bytes.empty()) {
    return false;
  }

  std::vector<std::uint8_t> payload_bytes(payload.begin(), payload.end());
  const std::vector<std::uint8_t> digest =
      sha256Bytes(payload_bytes.data(), payload_bytes.size());
  if (digest.empty()) {
    return false;
  }

  BCRYPT_ALG_HANDLE algorithm = nullptr;
  if (BCryptOpenAlgorithmProvider(
          &algorithm, BCRYPT_RSA_ALGORITHM, nullptr, 0) != kStatusSuccess) {
    return false;
  }

  BCRYPT_RSAKEY_BLOB header{};
  header.Magic = BCRYPT_RSAPUBLIC_MAGIC;
  header.BitLength = static_cast<ULONG>(modulus.size() * 8);
  header.cbPublicExp = static_cast<ULONG>(exponent.size());
  header.cbModulus = static_cast<ULONG>(modulus.size());

  std::vector<std::uint8_t> blob(sizeof(header));
  std::memcpy(blob.data(), &header, sizeof(header));
  blob.insert(blob.end(), exponent.begin(), exponent.end());
  blob.insert(blob.end(), modulus.begin(), modulus.end());

  BCRYPT_KEY_HANDLE imported = nullptr;
  const NTSTATUS import_status = BCryptImportKeyPair(
      algorithm, nullptr, BCRYPT_RSAPUBLIC_BLOB, &imported,
      blob.data(), static_cast<ULONG>(blob.size()), 0);
  if (import_status != kStatusSuccess) {
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return false;
  }

  BCRYPT_PKCS1_PADDING_INFO padding{};
  padding.pszAlgId = BCRYPT_SHA256_ALGORITHM;
  const NTSTATUS verify_status = BCryptVerifySignature(
      imported, &padding, reinterpret_cast<PUCHAR>(
                              const_cast<std::uint8_t*>(digest.data())),
      static_cast<ULONG>(digest.size()),
      reinterpret_cast<PUCHAR>(
          const_cast<std::uint8_t*>(signature_bytes.data())),
      static_cast<ULONG>(signature_bytes.size()), BCRYPT_PAD_PKCS1);
  BCryptDestroyKey(imported);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  return verify_status == kStatusSuccess;
}
#else
std::vector<std::uint8_t> sha256Bytes(
    const std::uint8_t*,
    std::size_t) {
  return {};
}

bool verifyRsaPkcs1Sha256(
    const SigningKey&,
    const std::string&,
    const ArtifactSignature&) {
  return false;
}
#endif

std::string sha256FileHex(const std::filesystem::path& path) {
  const std::vector<std::uint8_t> bytes = readBytes(path);
  if (bytes.empty()) {
    return {};
  }
  return bytesToHex(sha256Bytes(bytes.data(), bytes.size()));
}

const config::JsonValue* requiredArray(
    const config::JsonValue& value,
    const std::string& field,
    std::string& message) {
  const config::JsonValue* array = value.find(field);
  if (array == nullptr || !array->isArray()) {
    message = "missing array field: " + field;
    return nullptr;
  }
  return array;
}

bool readRequiredInt(
    const config::JsonValue& value,
    const std::string& field,
    int& out,
    std::string& message) {
  const config::JsonValue* item = value.find(field);
  if (item == nullptr || !item->isInt()) {
    message = "missing integer field: " + field;
    return false;
  }
  out = static_cast<int>(item->asInt());
  return true;
}

bool readRequiredString(
    const config::JsonValue& value,
    const std::string& field,
    std::string& out,
    std::string& message) {
  const config::JsonValue* item = value.find(field);
  if (item == nullptr || !item->isString() || item->asString().empty()) {
    message = "missing string field: " + field;
    return false;
  }
  out = item->asString();
  return true;
}

std::vector<std::string> readStringArray(const config::JsonValue& array) {
  std::vector<std::string> out;
  if (!array.isArray()) {
    return out;
  }
  for (const config::JsonValue& item : array.elements()) {
    if (item.isString()) {
      out.push_back(item.asString());
    }
  }
  return out;
}

ArtifactTrustState stateForReason(const std::string& reason_code) {
  if (reason_code == "artifact_unsigned") {
    return ArtifactTrustState::Unsigned;
  }
  if (reason_code == "artifact_hash_mismatch") {
    return ArtifactTrustState::HashMismatch;
  }
  if (reason_code == "artifact_signature_invalid") {
    return ArtifactTrustState::SignatureInvalid;
  }
  if (reason_code == "artifact_signing_key_untrusted") {
    return ArtifactTrustState::UntrustedKey;
  }
  if (reason_code == "artifact_revoked" ||
      reason_code == "artifact_signing_key_revoked") {
    return ArtifactTrustState::Revoked;
  }
  if (reason_code == "artifact_unsupported_signature_algorithm") {
    return ArtifactTrustState::UnsupportedAlgorithm;
  }
  if (reason_code == "artifact_unreadable") {
    return ArtifactTrustState::UnreadableArtifact;
  }
  return ArtifactTrustState::InvalidConfig;
}

ArtifactVerificationResult verificationFailure(
    const ManifestArtifact& artifact,
    std::string reason_code,
    std::string detail) {
  ArtifactVerificationResult result;
  result.state = stateForReason(reason_code);
  result.reason_code = std::move(reason_code);
  result.artifact_id = artifact.artifact_id;
  result.artifact_type = artifact.artifact_type;
  result.expected_sha256 = artifact.sha256;
  result.detail = std::move(detail);
  return result;
}

}  // namespace

const SigningKey* TrustRoot::findKey(const std::string& key_id) const {
  for (const SigningKey& key : trusted_keys) {
    if (key.key_id == key_id) {
      return &key;
    }
  }
  return nullptr;
}

const ManifestArtifact* ArtifactManifest::findArtifact(
    const std::string& artifact_id) const {
  for (const ManifestArtifact& artifact : artifacts) {
    if (artifact.artifact_id == artifact_id) {
      return &artifact;
    }
  }
  return nullptr;
}

bool RevocationList::isKeyRevoked(const std::string& key_id) const {
  return std::find(revoked_key_ids.begin(), revoked_key_ids.end(), key_id) !=
      revoked_key_ids.end();
}

const RevokedArtifact* RevocationList::findRevokedArtifact(
    const std::string& artifact_id,
    const std::string& sha256) const {
  const std::string normalized_hash = lower(sha256);
  for (const RevokedArtifact& artifact : revoked_artifacts) {
    if (artifact.artifact_id == artifact_id &&
        lower(artifact.sha256) == normalized_hash) {
      return &artifact;
    }
  }
  return nullptr;
}

const char* artifactTrustStateName(ArtifactTrustState state) {
  switch (state) {
    case ArtifactTrustState::Trusted:
      return "trusted";
    case ArtifactTrustState::Unsigned:
      return "unsigned";
    case ArtifactTrustState::HashMismatch:
      return "hash-mismatch";
    case ArtifactTrustState::SignatureInvalid:
      return "signature-invalid";
    case ArtifactTrustState::UntrustedKey:
      return "untrusted-key";
    case ArtifactTrustState::Revoked:
      return "revoked";
    case ArtifactTrustState::UnsupportedAlgorithm:
      return "unsupported-algorithm";
    case ArtifactTrustState::UnreadableArtifact:
      return "unreadable-artifact";
    case ArtifactTrustState::InvalidConfig:
      return "invalid-config";
  }
  return "invalid-config";
}

ConfigLoadResult loadTrustRoot(const std::filesystem::path& path) {
  ConfigLoadResult result;
  const std::string text = readText(path);
  if (text.empty()) {
    result.message = "trust root is missing or empty";
    return result;
  }
  config::JsonValue root;
  std::string error;
  if (!config::parseJson(text, root, error) || !root.isObject()) {
    result.message = "trust root is not valid JSON: " + error;
    return result;
  }

  TrustRoot trust_root;
  if (!readRequiredInt(root, "version", trust_root.version, result.message) ||
      !readRequiredString(root, "root_id", trust_root.root_id, result.message) ||
      !readRequiredString(
          root, "root_version", trust_root.root_version, result.message)) {
    return result;
  }

  const config::JsonValue* keys =
      requiredArray(root, "trusted_keys", result.message);
  if (keys == nullptr || keys->elements().empty()) {
    result.message = result.message.empty()
        ? "trust root must contain at least one trusted key"
        : result.message;
    return result;
  }
  for (const config::JsonValue& key_value : keys->elements()) {
    if (!key_value.isObject()) {
      result.message = "trusted key entry is not an object";
      return result;
    }
    SigningKey key;
    if (!readRequiredString(key_value, "key_id", key.key_id, result.message) ||
        !readRequiredString(
            key_value, "algorithm", key.algorithm, result.message) ||
        !readRequiredString(
            key_value, "issuer_root_id", key.issuer_root_id, result.message) ||
        !readRequiredString(
            key_value, "rsa_modulus_hex", key.rsa_modulus_hex, result.message) ||
        !readRequiredString(
            key_value, "rsa_public_exponent_hex",
            key.rsa_public_exponent_hex, result.message)) {
      return result;
    }
    const config::JsonValue* usage =
        requiredArray(key_value, "usage", result.message);
    if (usage == nullptr) {
      return result;
    }
    key.usage = readStringArray(*usage);
    if (lower(key.algorithm) != "rsa-pkcs1-sha256" ||
        key.issuer_root_id != trust_root.root_id ||
        key.rsa_modulus_hex.empty() ||
        key.rsa_public_exponent_hex.empty() ||
        key.usage.empty()) {
      result.message = "trusted key entry is invalid";
      return result;
    }
    trust_root.trusted_keys.push_back(std::move(key));
  }

  result.loaded = true;
  result.message = "trust root loaded";
  result.trust_root = std::move(trust_root);
  return result;
}

ConfigLoadResult loadArtifactManifest(const std::filesystem::path& path) {
  ConfigLoadResult result;
  const std::string text = readText(path);
  if (text.empty()) {
    result.message = "artifact manifest is missing or empty";
    return result;
  }
  config::JsonValue root;
  std::string error;
  if (!config::parseJson(text, root, error) || !root.isObject()) {
    result.message = "artifact manifest is not valid JSON: " + error;
    return result;
  }

  ArtifactManifest manifest;
  if (!readRequiredInt(root, "version", manifest.version, result.message) ||
      !readRequiredString(
          root, "manifest_id", manifest.manifest_id, result.message) ||
      !readRequiredString(
          root, "bundle_version", manifest.bundle_version, result.message)) {
    return result;
  }

  const config::JsonValue* artifacts =
      requiredArray(root, "artifacts", result.message);
  if (artifacts == nullptr || artifacts->elements().empty()) {
    result.message = result.message.empty()
        ? "manifest must contain at least one artifact"
        : result.message;
    return result;
  }
  for (const config::JsonValue& artifact_value : artifacts->elements()) {
    if (!artifact_value.isObject()) {
      result.message = "artifact manifest entry is not an object";
      return result;
    }
    ManifestArtifact artifact;
    std::string relative_path;
    if (!readRequiredString(
            artifact_value, "artifact_id", artifact.artifact_id,
            result.message) ||
        !readRequiredString(
            artifact_value, "artifact_type", artifact.artifact_type,
            result.message) ||
        !readRequiredString(
            artifact_value, "artifact_version", artifact.artifact_version,
            result.message) ||
        !readRequiredString(
            artifact_value, "relative_path", relative_path, result.message) ||
        !readRequiredString(
            artifact_value, "sha256", artifact.sha256, result.message)) {
      return result;
    }
    artifact.relative_path = relative_path;
    artifact.sha256 = lower(artifact.sha256);

    const config::JsonValue* signatures =
        requiredArray(artifact_value, "signatures", result.message);
    if (signatures == nullptr) {
      return result;
    }
    for (const config::JsonValue& signature_value : signatures->elements()) {
      if (!signature_value.isObject()) {
        result.message = "signature entry is not an object";
        return result;
      }
      ArtifactSignature signature;
      if (!readRequiredString(
              signature_value, "key_id", signature.key_id, result.message) ||
          !readRequiredString(
              signature_value, "algorithm", signature.algorithm,
              result.message) ||
          !readRequiredString(
              signature_value, "signature", signature.signature_hex,
              result.message)) {
        return result;
      }
      signature.signature_hex = lower(signature.signature_hex);
      artifact.signatures.push_back(std::move(signature));
    }
    manifest.artifacts.push_back(std::move(artifact));
  }

  result.loaded = true;
  result.message = "artifact manifest loaded";
  result.manifest = std::move(manifest);
  return result;
}

ConfigLoadResult loadRevocationList(const std::filesystem::path& path) {
  ConfigLoadResult result;
  const std::string text = readText(path);
  if (text.empty()) {
    result.message = "revocation list is missing or empty";
    return result;
  }
  config::JsonValue root;
  std::string error;
  if (!config::parseJson(text, root, error) || !root.isObject()) {
    result.message = "revocation list is not valid JSON: " + error;
    return result;
  }

  RevocationList list;
  if (!readRequiredInt(root, "version", list.version, result.message) ||
      !readRequiredString(root, "list_id", list.list_id, result.message)) {
    return result;
  }
  const config::JsonValue* revoked_keys =
      requiredArray(root, "revoked_key_ids", result.message);
  if (revoked_keys == nullptr) {
    return result;
  }
  list.revoked_key_ids = readStringArray(*revoked_keys);

  const config::JsonValue* revoked_artifacts =
      requiredArray(root, "revoked_artifacts", result.message);
  if (revoked_artifacts == nullptr) {
    return result;
  }
  for (const config::JsonValue& artifact_value : revoked_artifacts->elements()) {
    if (!artifact_value.isObject()) {
      result.message = "revoked artifact entry is not an object";
      return result;
    }
    RevokedArtifact artifact;
    if (!readRequiredString(
            artifact_value, "artifact_id", artifact.artifact_id,
            result.message) ||
        !readRequiredString(
            artifact_value, "sha256", artifact.sha256, result.message) ||
        !readRequiredString(
            artifact_value, "reason", artifact.reason, result.message)) {
      return result;
    }
    artifact.sha256 = lower(artifact.sha256);
    list.revoked_artifacts.push_back(std::move(artifact));
  }

  result.loaded = true;
  result.message = "revocation list loaded";
  result.revocations = std::move(list);
  return result;
}

std::string canonicalArtifactSigningPayload(
    const ManifestArtifact& artifact,
    const std::string& signing_key_id) {
  std::ostringstream payload;
  payload << "vrclient-artifact-signature-v1\n"
          << "artifact_id=" << artifact.artifact_id << "\n"
          << "artifact_type=" << artifact.artifact_type << "\n"
          << "artifact_version=" << artifact.artifact_version << "\n"
          << "sha256=" << lower(artifact.sha256) << "\n"
          << "signing_key_id=" << signing_key_id << "\n";
  return payload.str();
}

ArtifactVerificationResult verifyArtifact(
    const ManifestArtifact& artifact,
    const TrustRoot& trust_root,
    const RevocationList& revocations,
    const std::filesystem::path& artifact_base_directory) {
  if (!trust_root.loaded()) {
    return verificationFailure(
        artifact, "artifact_invalid_config", "trust root is not loaded");
  }
  if (artifact.artifact_id.empty() || artifact.artifact_type.empty() ||
      artifact.relative_path.empty() || artifact.sha256.empty()) {
    return verificationFailure(
        artifact, "artifact_invalid_config", "manifest artifact entry is incomplete");
  }
  if (artifact.signatures.empty()) {
    return verificationFailure(
        artifact, "artifact_unsigned", "artifact manifest entry has no signatures");
  }
  if (!isHex(artifact.sha256) || artifact.sha256.size() != 64) {
    return verificationFailure(
        artifact, "artifact_invalid_config", "artifact SHA-256 is malformed");
  }

  const std::filesystem::path artifact_path =
      artifact_base_directory / artifact.relative_path;
  const std::string observed_sha256 = sha256FileHex(artifact_path);
  if (observed_sha256.empty()) {
    auto result = verificationFailure(
        artifact, "artifact_unreadable", "artifact file is missing or unreadable");
    result.artifact_path = artifact_path;
    return result;
  }
  if (lower(observed_sha256) != lower(artifact.sha256)) {
    auto result = verificationFailure(
        artifact, "artifact_hash_mismatch",
        "artifact bytes do not match the manifest SHA-256");
    result.artifact_path = artifact_path;
    result.observed_sha256 = observed_sha256;
    return result;
  }

  if (const RevokedArtifact* revoked =
          revocations.findRevokedArtifact(artifact.artifact_id, artifact.sha256)) {
    auto result = verificationFailure(
        artifact, "artifact_revoked", revoked->reason);
    result.artifact_path = artifact_path;
    result.observed_sha256 = observed_sha256;
    return result;
  }

  bool saw_trusted_key = false;
  for (const ArtifactSignature& signature : artifact.signatures) {
    const SigningKey* key = trust_root.findKey(signature.key_id);
    if (key == nullptr) {
      continue;
    }
    saw_trusted_key = true;
    if (revocations.isKeyRevoked(signature.key_id)) {
      auto result = verificationFailure(
          artifact, "artifact_signing_key_revoked",
          "artifact signing key is revoked");
      result.signing_key_id = signature.key_id;
      result.artifact_path = artifact_path;
      result.observed_sha256 = observed_sha256;
      return result;
    }
    if (lower(signature.algorithm) != "rsa-pkcs1-sha256" ||
        lower(key->algorithm) != "rsa-pkcs1-sha256") {
      auto result = verificationFailure(
          artifact, "artifact_unsupported_signature_algorithm",
          "signature algorithm is unsupported");
      result.signing_key_id = signature.key_id;
      result.artifact_path = artifact_path;
      result.observed_sha256 = observed_sha256;
      return result;
    }
    const std::string payload =
        canonicalArtifactSigningPayload(artifact, signature.key_id);
    if (!verifyRsaPkcs1Sha256(*key, payload, signature)) {
      auto result = verificationFailure(
          artifact, "artifact_signature_invalid",
          "artifact signature did not verify against the trust root");
      result.signing_key_id = signature.key_id;
      result.artifact_path = artifact_path;
      result.observed_sha256 = observed_sha256;
      return result;
    }

    ArtifactVerificationResult result;
    result.state = ArtifactTrustState::Trusted;
    result.reason_code = "artifact_trusted";
    result.artifact_id = artifact.artifact_id;
    result.artifact_type = artifact.artifact_type;
    result.signing_key_id = signature.key_id;
    result.artifact_path = artifact_path;
    result.expected_sha256 = lower(artifact.sha256);
    result.observed_sha256 = observed_sha256;
    result.detail = "artifact hash and signature verified";
    return result;
  }

  return verificationFailure(
      artifact,
      saw_trusted_key ? "artifact_signature_invalid"
                      : "artifact_signing_key_untrusted",
      saw_trusted_key ? "trusted key signature did not verify"
                      : "no signature chains to a trusted key");
}

}  // namespace vrclient::supply_chain

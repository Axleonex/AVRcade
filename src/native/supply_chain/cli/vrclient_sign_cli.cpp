// M5 UNIFY-05: verify a client-produced pack's signature via the existing B6
// vr_supply_chain verifier (verifyArtifact over trust-root + manifest + revocation
// list + SHA-256). Verify-only, no network, no injection — the client shells to
// this before install and refuses fail-closed on anything but ok=true.
//
// Contract (M5 plan, Task 3.1):
//   vrclient_sign_cli --verify --manifest <path> --trust-root <path>
//                     --revocations <path> --artifact-id <id> --base-dir <dir>
//   -> SIGN ok=<true|false> reason=<reason_code> state=<state-name>
//   exit 0 always.

#include "supply_chain/artifact_signing.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::string Arg(const std::vector<std::string>& args, const std::string& key) {
  for (std::size_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i] == key) return args[i + 1];
  }
  return std::string();
}

}  // namespace

int main(int argc, char** argv) {
  using namespace vrclient::supply_chain;
  const std::vector<std::string> args(argv, argv + argc);

  const std::string manifestPath = Arg(args, "--manifest");
  const std::string trustRootPath = Arg(args, "--trust-root");
  const std::string revocationsPath = Arg(args, "--revocations");
  const std::string artifactId = Arg(args, "--artifact-id");
  const std::string baseDir = Arg(args, "--base-dir");

  const ConfigLoadResult trust = loadTrustRoot(std::filesystem::path(trustRootPath));
  const ConfigLoadResult manifest = loadArtifactManifest(std::filesystem::path(manifestPath));
  const ConfigLoadResult revocations = loadRevocationList(std::filesystem::path(revocationsPath));

  if (!trust.loaded || !manifest.loaded || !revocations.loaded) {
    std::cout << "SIGN ok=false reason=config_load_failed state=InvalidConfig\n";
    return 0;
  }

  const ManifestArtifact* artifact = manifest.manifest.findArtifact(artifactId);
  if (artifact == nullptr) {
    std::cout << "SIGN ok=false reason=artifact_not_in_manifest state=InvalidConfig\n";
    return 0;
  }

  const ArtifactVerificationResult result = verifyArtifact(
      *artifact, trust.trust_root, revocations.revocations, std::filesystem::path(baseDir));

  std::cout << "SIGN ok=" << (result.trusted() ? "true" : "false")
            << " reason=" << result.reason_code
            << " state=" << artifactTrustStateName(result.state) << "\n";
  return 0;
}

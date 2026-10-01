// Bolt-on Phase B2 — vrclient_hookdisc CLI.
//
// Thin front-end over the hookdisc library. All real logic lives in the lib
// (inspection.*, observe.*, safety_gate.*, hook_surface_doc.*). Subcommands:
//   inspect-self                 read-only RE-01 inspection of the current process
//   inspect <slug>               run the RE-05 safety gate for config/games/<slug>.json
//                                (only the controlled smoke target passes autonomously),
//                                then demonstrate read-only inspection on approval
//   doc-validate <path>          load + schema-validate a hook-surface document
//   doc-roundtrip <in> <out>     load, validate, and re-emit a hook-surface document
//   doc-emit-candidate <game_id> <build_id> <out>   write a minimal candidate doc

#include "diagnostics/diagnostics_system.h"
#include "injector/process/process_discovery.h"
#include "tooling/hookdisc/hook_surface_doc.h"
#include "tooling/hookdisc/inspection.h"
#include "tooling/hookdisc/safety_gate.h"
#include "versioning/game_fingerprint.h"

#include <ctime>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

namespace hookdisc = vrclient::tooling::hookdisc;
namespace process = vrclient::injector::process;
namespace versioning = vrclient::versioning;

std::filesystem::path repoRoot() {
#if defined(VRCLIENT_SOURCE_DIR)
  return std::filesystem::path(VRCLIENT_SOURCE_DIR);
#else
  return std::filesystem::current_path();
#endif
}

std::string nowTimestamp() {
  const std::time_t now = std::time(nullptr);
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &now);
#else
  gmtime_r(&now, &utc);
#endif
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return std::string(buffer);
}

int usage() {
  std::cerr << "usage: vrclient_hookdisc <command> [args]\n"
            << "  inspect-self\n"
            << "  inspect <slug>\n"
            << "  doc-validate <path>\n"
            << "  doc-roundtrip <in> <out>\n"
            << "  doc-emit-candidate <game_id> <build_id> <out>\n";
  return 2;
}

// Model a target descriptor from a fingerprint config's configured identity
// evidence. This is a dry-run model (no real process is read); it lets the CLI
// exercise the same identity + safety chain the live harness will use. The
// safety gate still refuses anything but the controlled smoke target.
//
// The config->descriptor identity logic lives in the hookdisc library
// (modeledTargetFromConfig) so the CLI and the obs host share ONE implementation.
// The CLI's source priority is steam -> direct -> manual; trusted modeled
// evidence is acceptable ONLY because inspectGame marks the gate run
// dry_run_model = true and reports the verdict as a dry run. A live attach must
// instead supply real observed evidence; it must never reuse this descriptor.
process::TargetDescriptor modeledTarget(const versioning::GameFingerprintConfig& config) {
  return hookdisc::modeledTargetFromConfig(
      config,
      {{"steam", process::DiscoveryFlow::SteamLaunch},
       {"direct", process::DiscoveryFlow::DirectLaunch},
       {"manual", process::DiscoveryFlow::ManualPath}},
      "hookdisc_cli_modeled_dry_run");
}

int inspectSelf(vrclient::diagnostics::AsyncLogger* logger) {
  const auto modules = hookdisc::enumerateCurrentProcessModules(logger);
  if (!modules.ok) {
    std::cerr << "module enumeration failed: " << modules.message << "\n";
    return 1;
  }
  std::cout << "modules loaded in current process: " << modules.modules.size() << "\n";

  int rc = 0;

  const hookdisc::ModuleInfo* kernel32 = hookdisc::findModule(modules, "kernel32.dll");
  if (kernel32 != nullptr) {
    const auto exports = hookdisc::enumerateCurrentProcessExports(*kernel32, logger);
    if (!exports.ok) {
      // A broken inspection must surface as a nonzero exit, not a false green.
      std::cerr << "kernel32.dll export enumeration failed: " << exports.message << "\n";
      rc = 1;
    } else {
      std::cout << "kernel32.dll exports: " << exports.exports.size() << "\n";
      const auto hits = hookdisc::searchExportSymbols(exports, {"GetProcAddress"});
      for (const auto& hit : hits) {
        std::cout << "  found export " << hit.symbol << " @ 0x" << std::hex
                  << hit.address << std::dec << "\n";
      }
    }
  }

  if (!modules.modules.empty()) {
    const auto rtti = hookdisc::scanCurrentProcessRtti(modules.modules.front(), {}, logger);
    if (!rtti.ok) {
      std::cerr << "RTTI scan failed for " << rtti.module_name << ": " << rtti.message << "\n";
      rc = 1;
    } else {
      std::cout << "RTTI type descriptors in " << rtti.module_name << ": "
                << rtti.type_descriptors.size() << " (scanned " << rtti.bytes_scanned
                << " bytes)\n";
    }
  }
  return rc;
}

int inspectGame(const std::string& slug, vrclient::diagnostics::AsyncLogger* logger) {
  const std::filesystem::path config_path =
      repoRoot() / "config" / "games" / (slug + ".json");
  const auto loaded = versioning::loadGameFingerprintConfig(config_path);
  if (!loaded.loaded) {
    std::cerr << "fingerprint config invalid: " << loaded.message << "\n";
    return 1;
  }

  const process::TargetDescriptor target = modeledTarget(loaded.config);
  hookdisc::HookSurfaceGateOptions options;  // autonomous: no multiplayer confirmation
  // This CLI path inspects no real external process: it MODELS the identity from
  // config to exercise the gate chain. Mark it a dry run so the verdict can never
  // be mistaken for (or reused as) a live approval, and so the modeled expected
  // hash only stands in for the controlled smoke host.
  options.dry_run_model = true;
  const auto outcome =
      hookdisc::runHookSurfaceSafetyGate(config_path, target, options, logger);

  const char* run_kind = outcome.dry_run ? " (DRY-RUN MODEL, no real process)" : "";
  std::cout << "safety gate for game_id=" << outcome.game_id
            << " build_id=" << outcome.build_id
            << " controlled_smoke_target=" << (outcome.controlled_smoke_target ? "true" : "false")
            << "\n  verdict=" << (outcome.approved ? "APPROVED" : "REFUSED")
            << run_kind
            << " reason=" << outcome.reason_code << "\n";

  if (!outcome.approved) {
    std::cerr << "harness refuses: target not approved by safety preflight\n";
    return 3;
  }

  std::cout << "approved (controlled smoke target, DRY-RUN MODEL). Demonstrating "
               "read-only inspection against the current process; a live external "
               "attach to the smoke host runs under this same gate with REAL "
               "observed identity/hash evidence in a later phase.\n";
  return inspectSelf(logger);
}

int docValidate(const std::string& path) {
  const auto loaded = hookdisc::loadHookSurfaceDoc(path);
  if (!loaded.loaded) {
    std::cerr << "INVALID: " << loaded.message << "\n";
    return 1;
  }
  std::cout << "valid hook-surface: game_id=" << loaded.doc.game_id
            << " build_id=" << loaded.doc.build_id
            << " hooks=" << loaded.doc.hooks.size() << "\n";
  return 0;
}

int docRoundtrip(const std::string& in_path, const std::string& out_path) {
  const auto loaded = hookdisc::loadHookSurfaceDoc(in_path);
  if (!loaded.loaded) {
    std::cerr << "INVALID input: " << loaded.message << "\n";
    return 1;
  }
  std::string error;
  if (!hookdisc::saveHookSurfaceDoc(loaded.doc, out_path, error)) {
    std::cerr << "save failed: " << error << "\n";
    return 1;
  }
  std::cout << "round-tripped " << loaded.doc.hooks.size() << " hooks to " << out_path << "\n";
  return 0;
}

int docEmitCandidate(
    const std::string& game_id,
    const std::string& build_id,
    const std::string& out_path) {
  hookdisc::HookSurfaceDoc doc;
  doc.version = 1;
  doc.game_id = game_id;
  doc.build_id = build_id;
  doc.generated_by = "vrclient_hookdisc doc-emit-candidate";
  doc.generated_at = nowTimestamp();

  hookdisc::HookEntry hook;
  hook.name = "CandidatePlaceholder";
  hook.area = hookdisc::HookArea::Camera;
  hook.module = "unknown";
  hook.symbol = "CandidatePlaceholder";
  hook.status = hookdisc::HookStatus::Candidate;
  hook.confidence = hookdisc::HookConfidence::Low;
  doc.hooks.push_back(hook);

  std::string error;
  if (!hookdisc::saveHookSurfaceDoc(doc, out_path, error)) {
    std::cerr << "emit failed: " << error << "\n";
    return 1;
  }
  std::cout << "wrote candidate hook-surface to " << out_path << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    return usage();
  }
  const std::string command = argv[1];

  vrclient::diagnostics::DiagnosticsSystem diagnostics;
  diagnostics.initialize("vrclient-hookdisc", nullptr);
  diagnostics.setRuntimeIds("hookdisc", "tooling", "hookdisc");
  vrclient::diagnostics::AsyncLogger* logger = &diagnostics.logger();

  int rc = 2;
  if (command == "inspect-self" && argc == 2) {
    rc = inspectSelf(logger);
  } else if (command == "inspect" && argc == 3) {
    rc = inspectGame(argv[2], logger);
  } else if (command == "doc-validate" && argc == 3) {
    rc = docValidate(argv[2]);
  } else if (command == "doc-roundtrip" && argc == 4) {
    rc = docRoundtrip(argv[2], argv[3]);
  } else if (command == "doc-emit-candidate" && argc == 5) {
    rc = docEmitCandidate(argv[2], argv[3], argv[4]);
  } else {
    rc = usage();
  }

  diagnostics.shutdown();
  return rc;
}

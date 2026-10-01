#pragma once

// Bolt-on B8: anti-cheat-aware launch-mode routing.
//
// This layer is deliberately pure decision logic. It consumes already-collected
// Phase 7 detection observations and config-backed launch variant records, then
// decides which product path the manager may offer:
//   * injector-capable only when anti-cheat is positively clear;
//   * launch-only when anti-cheat is detected and a legitimate non-injection
//     variant is configured;
//   * blocked when the signal is uncertain or no sanctioned launch-only path is
//     available.
//
// It does not launch a process, inspect a process, attach, inject, hook, patch,
// disable, or evade anything. Anti-cheat interaction remains detection-only.

#include "diagnostics/logging/diagnostic_logger.h"
#include "safety/detection_inputs.h"

#include <filesystem>
#include <string>
#include <vector>

namespace vrclient::safety {

enum class AntiCheatRouteKind {
  Blocked,
  LaunchOnly,
  InjectorCapable
};

enum class RequestedLaunchMode {
  Auto,
  LaunchOnly,
  Inject
};

const char* antiCheatRouteKindName(AntiCheatRouteKind route);
const char* requestedLaunchModeName(RequestedLaunchMode mode);

struct AntiCheatRouteReasonCodes {
  std::string launch_only;
  std::string block;
  std::string injection_disabled;
  std::string injector_capable;
};

struct AntiCheatLaunchVariant {
  std::string game_id;
  std::string build_id;
  std::string variant_id;
  std::string variant_type;
  std::string display_name;
  std::string source;
  std::string launch_mode;
  std::string anti_cheat_policy;
  bool legitimate_non_injection_path = false;
  bool injection_capable = false;
  std::string sanctioned_path_label;
  std::string manager_message_key;
  AntiCheatRouteReasonCodes reason_codes;
};

struct AntiCheatCompatibilitySet {
  int version = 0;
  std::string compatibility_set_version;
  std::vector<AntiCheatLaunchVariant> variants;

  [[nodiscard]] bool loaded() const { return version >= 1; }
};

struct AntiCheatCompatibilityLoadResult {
  bool loaded = false;
  std::string message;
  AntiCheatCompatibilitySet compatibility_set;
};

AntiCheatCompatibilityLoadResult loadAntiCheatCompatibilitySet(
    const std::filesystem::path& path);

struct AntiCheatLaunchRequest {
  std::string game_id;
  std::string build_id;
  std::string selected_variant_id;
  RequestedLaunchMode requested_mode = RequestedLaunchMode::Auto;
  DetectionObservations detection;
};

struct AntiCheatLaunchRoute {
  AntiCheatRouteKind route = AntiCheatRouteKind::Blocked;
  bool injection_allowed = false;
  std::string variant_id;
  std::string variant_type;
  std::string reason_code = "anti_cheat_route_not_evaluated";
  std::string message_key = "safety.unknown.anti_cheat.route_not_evaluated";
  std::string message;
  DetectionSignals detection_signals;
  std::vector<diagnostics::LogField> diagnostics_fields;
};

AntiCheatLaunchRoute routeAntiCheatLaunch(
    const AntiCheatLaunchRequest& request,
    const AntiCheatCompatibilitySet& compatibility_set,
    const DetectionIndicatorCatalog& catalog);

}  // namespace vrclient::safety

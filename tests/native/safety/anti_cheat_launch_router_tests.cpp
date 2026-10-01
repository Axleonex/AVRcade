#include "safety/anti_cheat_launch_router.h"
#include "safety/safety_verdict.h"

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

std::filesystem::path compatibilityPath() {
  return repoRoot() / "config" / "safety" / "anti-cheat-compatibility.json";
}

std::filesystem::path safetyRulesPath() {
  return repoRoot() / "config" / "safety" / "default-rules.json";
}

vrclient::safety::AntiCheatCompatibilitySet loadCompatibility() {
  const auto loaded =
      vrclient::safety::loadAntiCheatCompatibilitySet(compatibilityPath());
  expect(loaded.loaded, loaded.message);
  return loaded.compatibility_set;
}

vrclient::safety::DetectionIndicatorCatalog loadCatalog() {
  const auto loaded = vrclient::safety::loadSafetyRuleSet(safetyRulesPath());
  expect(loaded.loaded, loaded.message);
  return loaded.rule_set.indicators;
}

vrclient::safety::AntiCheatLaunchRequest acProtectedRequest() {
  vrclient::safety::AntiCheatLaunchRequest request;
  request.game_id = "example-anti-cheat-protected";
  request.build_id = "steam-ac-build-1";
  request.requested_mode = vrclient::safety::RequestedLaunchMode::Inject;
  request.detection.module_enumeration_complete = true;
  request.detection.observed_anti_cheat_indicators = {"EasyAntiCheat_x64.dll"};
  return request;
}

void runLoadConfigTest() {
  const auto set = loadCompatibility();
  expect(set.version == 1, "compatibility schema version must be 1");
  expect(!set.compatibility_set_version.empty(),
         "compatibility set must carry a content version");
  expect(set.variants.size() >= 4,
         "fixture compatibility set should carry the B8 route matrix");
  std::cout << "anti-cheat compatibility config loaded\n";
}

void runAcPositiveLaunchOnlyTest() {
  const auto set = loadCompatibility();
  const auto catalog = loadCatalog();
  const auto route =
      vrclient::safety::routeAntiCheatLaunch(acProtectedRequest(), set, catalog);

  expect(route.route == vrclient::safety::AntiCheatRouteKind::LaunchOnly,
         "AC-positive target with sanctioned non-injection path must route launch-only");
  expect(!route.injection_allowed,
         "AC-positive launch-only target must not allow injection");
  expect(route.variant_id == "example-ac-launch-only",
         "AC launch-only variant id mismatch");
  expect(route.reason_code == "anti_cheat_launch_only_injection_disabled",
         "AC launch-only reason code mismatch");
  expect(route.detection_signals.anti_cheat == vrclient::safety::RiskSignal::Risky,
         "AC-positive fixture must classify anti-cheat as risky");
  std::cout << "anti-cheat route: AC-positive -> launch-only, injection disabled\n";
}

void runAcPositiveNoPathBlocksTest() {
  const auto set = loadCompatibility();
  const auto catalog = loadCatalog();
  auto request = acProtectedRequest();
  request.game_id = "example-ac-no-launch-path";
  request.build_id = "any-build";

  const auto route =
      vrclient::safety::routeAntiCheatLaunch(request, set, catalog);
  expect(route.route == vrclient::safety::AntiCheatRouteKind::Blocked,
         "AC-positive target with no sanctioned path must block");
  expect(!route.injection_allowed,
         "blocked AC target must never allow injection");
  expect(route.reason_code == "anti_cheat_no_sanctioned_launch_path",
         "no-path block reason mismatch");
  std::cout << "anti-cheat route: AC-positive no path -> block\n";
}

void runUncertainSignalBlocksTest() {
  const auto set = loadCompatibility();
  const auto catalog = loadCatalog();
  auto request = acProtectedRequest();
  request.detection.observed_anti_cheat_indicators.clear();
  request.detection.module_enumeration_complete = false;

  const auto route =
      vrclient::safety::routeAntiCheatLaunch(request, set, catalog);
  expect(route.route == vrclient::safety::AntiCheatRouteKind::Blocked,
         "uncertain AC signal must block over launch-only");
  expect(!route.injection_allowed,
         "uncertain AC signal must not allow injection");
  expect(route.reason_code == "anti_cheat_signal_uncertain",
         "uncertain AC signal reason mismatch");
  std::cout << "anti-cheat route: uncertain signal -> block\n";
}

void runClearSignalInjectorCapableTest() {
  const auto set = loadCompatibility();
  const auto catalog = loadCatalog();
  vrclient::safety::AntiCheatLaunchRequest request;
  request.game_id = "vrclient-smoke-host";
  request.build_id = "smoke-2026-06-11";
  request.requested_mode = vrclient::safety::RequestedLaunchMode::Inject;
  request.detection.module_enumeration_complete = true;

  const auto route =
      vrclient::safety::routeAntiCheatLaunch(request, set, catalog);
  expect(route.route == vrclient::safety::AntiCheatRouteKind::InjectorCapable,
         "clear controlled smoke fixture should resolve injector-capable");
  expect(route.injection_allowed,
         "clear controlled smoke injector route should allow injection routing");
  expect(route.reason_code == "injector_capable_no_anti_cheat_detected",
         "clear injector-capable reason mismatch");
  std::cout << "anti-cheat route: clear smoke target -> injector-capable\n";
}

void runAcPositiveCannotFlipToSelectedInjectorTest() {
  const auto set = loadCompatibility();
  const auto catalog = loadCatalog();
  auto request = acProtectedRequest();
  request.selected_variant_id = "example-ac-launch-only";
  request.requested_mode = vrclient::safety::RequestedLaunchMode::Inject;

  const auto route =
      vrclient::safety::routeAntiCheatLaunch(request, set, catalog);
  expect(route.route == vrclient::safety::AntiCheatRouteKind::LaunchOnly,
         "selected AC launch-only variant must stay launch-only");
  expect(!route.injection_allowed,
         "selected AC launch-only variant cannot flip to injected");
  expect(route.variant_type == "external-vr-mod-launch-only",
         "selected AC variant type should remain launch-only");
  std::cout << "anti-cheat route: selected AC launch-only cannot flip to injection\n";
}

}  // namespace

int main() {
  runLoadConfigTest();
  runAcPositiveLaunchOnlyTest();
  runAcPositiveNoPathBlocksTest();
  runUncertainSignalBlocksTest();
  runClearSignalInjectorCapableTest();
  runAcPositiveCannotFlipToSelectedInjectorTest();
  std::cout << "anti-cheat launch router tests passed\n";
  return 0;
}

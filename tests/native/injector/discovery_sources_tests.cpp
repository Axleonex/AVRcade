// INJ-05 closure test (ADDITIVE — new harness, no module source touched).
//
// Process discovery must handle direct launch, Steam, Epic, multiple installs,
// and custom paths. The existing injector_tests.cpp already proves:
//   * DirectLaunch (runMissingExecutableTests / runDiscoveryChoiceTest helper),
//   * SteamLaunch with MULTIPLE installs (runDiscoveryChoiceTest),
//   * AttachRunning races (runAttachRaceTests),
//   * ManualPath missing-executable refusal (runMissingExecutableTests).
//
// This test fills the remaining INJ-05 gaps with real assertions that can fail:
//   * EpicLaunch resolves a single deterministic Ready descriptor and carries
//     the Epic storefront id through to the descriptor,
//   * a CUSTOM PATH (ManualPath with a user-chosen candidate) resolves Ready,
//   * DirectLaunch resolves Ready for a present executable,
//   * the deterministic multi-install choice ordering holds for Epic too.

#include "injector/process/process_discovery.h"

#include <stdexcept>
#include <string>

namespace {

namespace proc = vrclient::injector::process;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

// Epic launch with a single resolvable install -> exactly one Ready descriptor,
// storefront id preserved, source hint propagated.
void runEpicLaunchReadyTest() {
  proc::ProcessDiscoveryRequest request;
  request.flow = proc::DiscoveryFlow::EpicLaunch;
  request.game_id_hint = "vrclient-smoke-host";
  request.storefront_id = "epic:smoke-namespace";
  request.install_candidates = {
      {"D:/Epic/Smoke/vrclient_smoke_host.exe", "D:/Epic/Smoke", "epic", true},
  };

  const auto result = proc::discoverTargets(request);
  expect(result.status == proc::DiscoveryStatus::Ready,
         "single Epic install should resolve to a Ready descriptor");
  expect(result.descriptors.size() == 1, "Epic Ready should yield one descriptor");
  expect(result.descriptors[0].flow == proc::DiscoveryFlow::EpicLaunch,
         "descriptor must carry the Epic flow");
  expect(result.descriptors[0].storefront_id == "epic:smoke-namespace",
         "Epic storefront id must propagate to the descriptor");
  expect(std::string(proc::discoveryFlowName(proc::DiscoveryFlow::EpicLaunch)) ==
             "epic_launch",
         "Epic flow must have a stable diagnostic name");
}

// Epic launch with multiple installs -> MultipleCandidates, deterministic order.
void runEpicMultipleInstallsTest() {
  proc::ProcessDiscoveryRequest request;
  request.flow = proc::DiscoveryFlow::EpicLaunch;
  request.game_id_hint = "vrclient-smoke-host";
  request.install_candidates = {
      {"D:/Epic/Smoke/vrclient_smoke_host.exe", "D:/Epic/Smoke", "epic", true},
      {"C:/Epic/Smoke/vrclient_smoke_host.exe", "C:/Epic/Smoke", "epic", true},
  };

  const auto result = proc::discoverTargets(request);
  expect(result.status == proc::DiscoveryStatus::MultipleCandidates,
         "two Epic installs should report multiple candidates");
  expect(result.descriptors.size() == 2, "Epic multi-install descriptor count mismatch");
  expect(result.descriptors[0].executable_path.generic_string().rfind("C:/", 0) == 0,
         "Epic candidate ordering should be deterministic (lexical)");
  expect(result.reason_code == "multiple_install_candidates",
         "Epic multi-install reason mismatch");
}

// Custom path: a user-added install pointing at an arbitrary, present executable
// (ManualPath flow) resolves to a Ready descriptor.
void runCustomPathReadyTest() {
  proc::ProcessDiscoveryRequest request;
  request.flow = proc::DiscoveryFlow::ManualPath;
  request.game_id_hint = "vrclient-smoke-host";
  request.install_candidates = {
      {"X:/Custom Modded Install/vrclient_smoke_host.exe",
       "X:/Custom Modded Install", "manual", true},
  };

  const auto result = proc::discoverTargets(request);
  expect(result.status == proc::DiscoveryStatus::Ready,
         "a present custom-path install should resolve Ready");
  expect(result.descriptors.size() == 1, "custom path should yield one descriptor");
  expect(result.descriptors[0].source_hint == "manual",
         "custom path descriptor should carry the manual source hint");
}

// Direct launch (e.g. user double-clicks the exe) with a present executable
// resolves Ready — the remaining INJ-05 launch source.
void runDirectLaunchReadyTest() {
  proc::ProcessDiscoveryRequest request;
  request.flow = proc::DiscoveryFlow::DirectLaunch;
  request.executable_path = "C:/Games/Smoke/vrclient_smoke_host.exe";
  request.install_root = "C:/Games/Smoke";
  request.executable_exists = true;

  const auto result = proc::discoverTargets(request);
  expect(result.status == proc::DiscoveryStatus::Ready,
         "present direct-launch executable should resolve Ready");
  expect(result.descriptors.size() == 1, "direct launch should yield one descriptor");
  expect(result.descriptors[0].source_hint == "direct",
         "direct launch descriptor should carry the direct source hint");
}

}  // namespace

int main() {
  runEpicLaunchReadyTest();
  runEpicMultipleInstallsTest();
  runCustomPathReadyTest();
  runDirectLaunchReadyTest();
  return 0;
}

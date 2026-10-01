// CORE-06 closure test.
//
// Hardened against NDEBUG: this file previously asserted its entire result via
// the C `assert()` macro, which is compiled out under any Release/NDEBUG build
// (build-and-test.ps1 auto-promotes to Release for -OpenXR on, and this target
// is registered unconditionally). Under that config every assertion vanished
// and the test became a vacuous pass. The checks now use a runtime `expect()`
// helper that throws (escaping main -> std::terminate -> non-zero exit, caught
// by CTest) so the CORE-06 evidence is genuine in BOTH Debug and Release.

#include "perf/dynamic_resolution.h"
#include "perf/foveation.h"
#include "perf/runtime_profile.h"

#include <stdexcept>
#include <string>

int runStateTransitionTests();

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

int runRuntimeProfileTests() {
  const std::string profile_path =
      std::string(VRCLIENT_SOURCE_DIR) + "/config/defaults/runtime-profile.json";
  const auto loaded =
      vrclient::runtime::perf::loadRuntimeProfileFromFile(profile_path.c_str());
  expect(loaded.result == VR_RUNTIME_OK,
         "runtime-profile.json must load (CORE-06: tuning kept in config)");
  expect(loaded.profile.dynamic_resolution.enabled,
         "dynamic resolution must be enabled from config");
  expect(loaded.profile.dynamic_resolution.min_scale <=
             loaded.profile.dynamic_resolution.initial_scale,
         "min_scale <= initial_scale from config");
  expect(loaded.profile.dynamic_resolution.initial_scale <=
             loaded.profile.dynamic_resolution.max_scale,
         "initial_scale <= max_scale from config");
  expect(loaded.profile.foveation.preset == VR_RUNTIME_FOVEATION_MEDIUM,
         "foveation preset must come from config (expected MEDIUM)");

  vrclient::runtime::perf::DynamicResolutionController dynamic_resolution;
  dynamic_resolution.reset(loaded.profile.dynamic_resolution);
  const float starting_scale = dynamic_resolution.scale();
  dynamic_resolution.recordFrame(20.0f, 20.0f, true);
  dynamic_resolution.recordFrame(20.0f, 20.0f, true);
  expect(dynamic_resolution.scale() < starting_scale,
         "over-budget frames must drive the config-applied scale down");

  const auto foveation =
      vrclient::runtime::perf::makeFoveationSettings(loaded.profile.foveation);
  expect(foveation.preset == VR_RUNTIME_FOVEATION_MEDIUM,
         "foveation settings must be applied from the config preset");
  expect(foveation.inner_radius <= foveation.outer_radius,
         "foveation inner_radius <= outer_radius");

  return 0;
}

}  // namespace

int main() {
  runRuntimeProfileTests();
  runStateTransitionTests();
  return 0;
}

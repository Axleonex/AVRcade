// M6 Half A CTest: the read-only managed-locator enumerates the controlled
// fixture assembly and finds FixtureType.FixtureMethod as a mono_method candidate.
// Fixture path is argv[1] (wired by CMake).

#include "tooling/hookdisc/managed_locators.h"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void expect(bool cond, const char* msg) {
  if (!cond) throw std::runtime_error(msg);
}
}  // namespace

int main(int argc, char** argv) {
  using namespace vrclient::tooling::hookdisc;
  try {
    expect(argc >= 2, "usage: vr_managed_locator_tests <fixture.dll>");
    const std::string fixture = argv[1];

    std::vector<ManagedMethodCandidate> methods;
    std::string error;
    const bool ok = EnumerateMonoMethods(fixture, methods, error);
    expect(ok, ("enumeration failed: " + error).c_str());
    expect(!methods.empty(), "no methods enumerated");

    bool foundFixtureMethod = false;
    bool foundSecondMethod = false;
    bool typeAssociated = false;
    for (const auto& m : methods) {
      expect(m.locator_kind == "mono_method", "locator_kind must be mono_method");
      if (m.method_name == "FixtureMethod") {
        foundFixtureMethod = true;
        if (m.type_name.find("FixtureType") != std::string::npos) typeAssociated = true;
      }
      if (m.method_name == "SecondMethod") foundSecondMethod = true;
    }
    expect(foundFixtureMethod, "FixtureMethod not enumerated");
    expect(foundSecondMethod, "SecondMethod not enumerated");
    expect(typeAssociated, "FixtureMethod not associated with FixtureType");

    // Negative: a non-CLI file returns false with an error (graceful, read-only).
    std::vector<ManagedMethodCandidate> none;
    std::string err2;
    const bool bad = EnumerateMonoMethods(std::string(argv[0]) + ".does-not-exist", none, err2);
    expect(!bad, "expected failure on a missing file");
    expect(!err2.empty(), "expected an error message on failure");

    // IL2CPP path (AAA-02): enumerate methods from a v24 global-metadata.dat fixture.
    if (argc >= 3) {
      std::vector<ManagedMethodCandidate> il2;
      std::string ilErr;
      const bool ilOk = EnumerateIl2CppMethods(argv[2], il2, ilErr);
      expect(ilOk, ("il2cpp enumeration failed: " + ilErr).c_str());
      bool foundIl2 = false;
      for (const auto& m : il2) {
        expect(m.locator_kind == "il2cpp_method", "il2cpp locator_kind must be il2cpp_method");
        if (m.method_name == "Il2CppFixtureMethod") foundIl2 = true;
      }
      expect(foundIl2, "Il2CppFixtureMethod not enumerated from the il2cpp fixture");
      std::printf("  il2cpp: %zu methods, Il2CppFixtureMethod found\n", il2.size());
    }

    std::printf("vr_managed_locator_tests OK: %zu mono methods, FixtureType.FixtureMethod found\n",
                methods.size());
    return 0;
  } catch (const std::exception& e) {
    std::printf("vr_managed_locator_tests FAILED: %s\n", e.what());
    return 1;
  }
}

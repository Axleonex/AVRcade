#pragma once

// Bolt-on Phase B2 — byte-patching pass-through detour: INTENTIONALLY DISABLED.
//
// v1 hook validation is READ-ONLY: it proves a candidate site is hit using
// hardware-breakpoint observation (observe.h), which never modifies a single
// code byte. A byte-patching pass-through detour (a trampoline that writes over
// the prologue, calls the original, and returns) is a fundamentally more
// invasive technique. It is declared here ONLY as an interface so a future,
// human-reviewed phase has a defined seam to fill — it is NOT compiled into any
// shipping or tested target.
//
// The entire implementation is gated behind VRCLIENT_HOOKDISC_ENABLE_BYTE_PATCH_DETOUR,
// which is NOT defined by any CMake target in this repository. Building it is a
// deliberate, reviewed act.
//
// !!! HUMAN SIGN-OFF REQUIRED !!!
// Do not define VRCLIENT_HOOKDISC_ENABLE_BYTE_PATCH_DETOUR without:
//   1. Explicit written sign-off from a human maintainer, AND
//   2. Confirmation the target is in the safety-approved set (controlled smoke
//      host, or an anti-cheat-free / sanctioned-modded game that already passed
//      the safety preflight). It must NEVER be enabled for an anti-cheat target.
// A code path that patches bytes to reach a site only reachable by defeating an
// integrity check is BLOCKED by policy and must not be implemented here.

#include <cstdint>

namespace vrclient::tooling::hookdisc {

// Result of attempting to install a read-only pass-through detour. Present even
// when the implementation is compiled out, so callers can reference the type.
struct DetourInstallResult {
  bool installed = false;
  const char* reason = "byte_patch_detour_compile_time_disabled";
};

// Pure interface for a pass-through (non-transforming) detour. A pass-through
// detour observes that a site is hit and then calls the original unchanged; it
// applies NO transform. Implementations are out of scope for v1 and gated out.
class BytePatchDetour {
 public:
  virtual ~BytePatchDetour() = default;

  // Install a pass-through detour at the given code address. The observer
  // callback is invoked on each hit; the original code path then runs unchanged.
  virtual DetourInstallResult install(const void* address) = 0;

  // Remove the detour and restore the original bytes exactly.
  virtual bool remove() = 0;
};

#if defined(VRCLIENT_HOOKDISC_ENABLE_BYTE_PATCH_DETOUR)
// Deliberately not implemented in v1. This stub exists so the disabled branch is
// syntactically present for reviewers; enabling the macro yields an unresolved
// reference on purpose, forcing a real, reviewed implementation rather than an
// accidental build.
class PassThroughBytePatchDetour final : public BytePatchDetour {
 public:
  DetourInstallResult install(const void* address) override;  // NOT DEFINED in v1
  bool remove() override;                                     // NOT DEFINED in v1
};
#endif  // VRCLIENT_HOOKDISC_ENABLE_BYTE_PATCH_DETOUR

}  // namespace vrclient::tooling::hookdisc

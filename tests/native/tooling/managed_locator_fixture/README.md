# Managed-locator controlled fixture (M6 Half A)

`FixtureType.cs` defines a tiny managed type `VrClient.ManagedLocatorFixture.FixtureType`
with a known method `FixtureMethod`. It is a **controlled test fixture — NEVER a
real game** (mirrors the B2 controlled-smoke-target discipline).

The M6 managed-locator CTest (`vr_managed_locator_tests`, wired in the root
`CMakeLists.txt` via the M6 plan, Task 2.1) builds this source to a small Mono
assembly, runs the read-only `managed_locators` enumeration against it, and
asserts `FixtureMethod` is enumerated as a `mono_method` candidate. A negative
control (requiring a wrong method name) must FAIL, then be restored.

The locator is READ-ONLY: it enumerates metadata, it does not patch, inject, or
modify anything (M6D4).

Env note: the native build + this CTest require a C++ toolchain. On a machine
without one they are env-deferred (see DEVIATIONS-M6.md); this fixture source +
the schema extension + docs are the verifiable Half-A deliverables there.

# VorpX-free completion plan

Requested 2026-09-06. Target: native OpenXR with no VorpX dependency.
This is not completion evidence. Required: native controller gameplay and aim,
stereo lifecycle regression tests, independent x86 packaging, native preflight,
two automated verification passes, and a final human headset test protocol.
Preserve unrelated work, mods, saves, and Git history.

## Checkpoint: 2026-09-06

Execution routing: canonical prime-code-execute classification exited 1 with
REMOTE_EXECUTOR_REQUIRED. No recommended/actual lane or policy fingerprint was
returned. Local continuation follows the recoverable-infrastructure rule in
STATIC-NATIVE-EXECUTION-POLICY; no paid provider was invoked.

Implemented: reject invalid/non-unit recenter and eye poses independently;
validate rendered camera rotation, projection, scale and frustum as well as
position before capturing either eye. Added regression cases in
tools/native/fnv-runtime-check.cpp.

Verification: fresh MSVC x86 Release build at build/fnv-native-20260906.
The original recenter regression failed before the fix. Both CTest tests
passed after the fixes. This is math/observer coverage, not runtime stereo
or motion-controller integration proof. The older build/fnv-x86 cache points
at an unavailable Z: drive and was preserved.

Completed since this checkpoint: independent OpenXR-loader packaging; native
launch/preflight integration; FNVR Tracker and `FNVR.esp` gameplay/weapon-aim
protocol inspection; isolated MO2 profile preparation; launch cleanup; mod
compatibility reporting; runtime/failure-path tests; and a final headset protocol.
The adapter now accepts the pristine supported executable or the same verified
1.4.0.525 x86 build after its required large-address-aware patch, while retaining
fail-closed render-prologue validation in the injected DLL.

The user supplied the official FNVR V2 Nexus archive. Its SHA-256 is recorded in
the dependency manifest, and the embedded game scripts were inspected directly.
They consume the tracker pose protocol, attach and rotate the weapon model, and
raycast shot/interaction direction independently of the headset. Remaining gates
are a physical headset/gameplay verification pass. VRClient uses a process-scoped
SteamVR OpenXR override, so the user's global VDXR selection remains unchanged. Do not call
the integration headset-verified until the human protocol passes.

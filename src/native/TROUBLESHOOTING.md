# Phases 1–5 Troubleshooting & Bug-Checking — Index (for LLMs)

Audience: an LLM (or human) working in `src/native/` who needs to find bugs,
avoid hollow "green" results, and verify changes honestly. Each subsystem has a
co-located `TROUBLESHOOTING.md` with phase-specific war-stories, a bug-checking
checklist, known never-run paths, and the exact verification commands.

These notes are grounded in **real bugs hit while building this tree**, not
hypotheticals. When you fix something here, add the new war-story to the
relevant subsystem file so the next agent inherits it.

## Phase → folder map

| Phase | Name | Folder | Subsystem doc |
|------|------|--------|---------------|
| 1 | Core VR runtime | `src/native/runtime/` | [runtime/TROUBLESHOOTING.md](runtime/TROUBLESHOOTING.md) |
| 2 | Diagnostics & operations | `src/native/diagnostics/` | [diagnostics/TROUBLESHOOTING.md](diagnostics/TROUBLESHOOTING.md) |
| 3 | Bootstrap injector | `src/native/injector/` (+ `safety/`, `tooling/hookdisc/`, `config/`, `versioning/`) | [injector/TROUBLESHOOTING.md](injector/TROUBLESHOOTING.md) |
| 4 | Plugin system / adapter SDK | `src/native/plugins/` | [plugins/TROUBLESHOOTING.md](plugins/TROUBLESHOOTING.md) |
| 5 | Input / comfort / HUD | `src/native/shared/` | [shared/TROUBLESHOOTING.md](shared/TROUBLESHOOTING.md) |

## Cross-cutting traps (these bit us across multiple phases)

### 1. `assert()` is a no-op under NDEBUG — it makes tests lie
The C `assert()` macro compiles to nothing under `NDEBUG` (any Release build).
`scripts/ci/build-and-test.ps1` **auto-promotes to Release for the `-OpenXR on`
leg**, and most test targets are registered unconditionally — so a test that
asserts its result via `assert()` becomes a **vacuous pass**: it runs, asserts
nothing, exits 0, and looks green. We hit this in `runtime_profile_tests.cpp`,
`state_transition_tests.cpp`, and `shared_system_tests.cpp` (now converted to a
throwing `expect()` helper; the in-file comments document the trap).
- **Check:** `grep -rn 'assert(' tests/native` — every match should be in a
  comment or a `static_assert`, never a load-bearing runtime check.
- **Prove fail-ability:** build the test in Release/NDEBUG, break a check on
  purpose, confirm the binary exits non-zero. If it still passes, the check is
  hollow. (A throwing `expect()` → uncaught exception → `std::terminate` →
  non-zero exit → CTest FAIL, which survives NDEBUG.)

### 2. Two build legs must both stay green
- **Default OpenXR-OFF** (`-OpenXR auto` defaults OFF) is **the gate**: fast,
  no SDK, runs the full CTest + Python validator suite.
- **OpenXR-ON** (`-OpenXR on`) is the compile+link check against the
  bootstrapped `external/openxr` + `external/vulkan`.
A change can pass OFF and break ON (e.g. include-order: `<unknwn.h>` must
precede `<openxr/openxr_platform.h>` under `XR_USE_PLATFORM_WIN32`). Run both
legs for any change that touches code inside an `if(VRCLIENT_BUILD_OPENXR_RUNTIME)`
block or graphics/platform headers.

### 3. No false green — compile/link/headless ≠ headset-verified
Compiling, linking, and even headless Vulkan validation-layer cleanliness do
**not** verify the headset render. Never tick a `CORE-*` / `DIAG-*` box in
`.planning/REQUIREMENTS.md` without the real evidence (e.g. CORE-04 = test scene
correct in BOTH eyes requires the user's `tools/run-headset-smoke.ps1` run on a
Quest + their eyes). Record honest deferrals in `.planning/STATE.md`.

### 4. Matrix convention is a system-wide contract
`src/native/runtime/frame/stereo_math.*` uses **column-vector, column-major**
matrices (`m[col*4 + row]`); the renderer uploads `viewProjection = P*V` and the
shader applies `clip = viewProjection * v` (no transpose) + the GL→Vulkan
`clip.y=-clip.y` / `clip.z=(clip.z+clip.w)*0.5` corrections. **HUD transforms
(Phase 5) are ROW-major** (see `docs/sdk/adapter-authoring.md`). Mixing
conventions produces bugs that are **invisible to validation layers but wrong on
a real headset** — exactly the projection bug fixed in `464796c`. Whenever a
matrix crosses a subsystem boundary, state and test its convention.

### 5. Integer narrowing in parsers silently accepts bad input
`config_versioning` once narrowed an `int64` version to `int`, so a too-new doc
truncated into an in-range value and **loaded silently**. Range-check the full
width. Audit every version/size/count parser for `int64 → int` narrowing.

### 6. The DLL-injection primitive is user-supplied (policy-blocked)
The actual injection core (`OpenProcess` / `VirtualAllocEx` /
`WriteProcessMemory` / `CreateRemoteThread` / `LoadLibraryW`, ~30 lines) is
**blocked from AI code generation by the assistant usage-policy filter** and is
supplied by the user. `INJ-01` / `INJ-06` stay open. Build everything *around*
it (discovery, safety preflight, refusal matrix, bootstrap smoke); do not loop
trying to generate the primitive. See `injector/TROUBLESHOOTING.md`.

### 7. Open-source / no-authorization constraint
Product law: no per-user auth IDs, no license keys, no required authorizations
for users (max = a Gmail). Don't reintroduce an authorization/verification gate
to "unlock" functionality. Engine-modding licenses (BepInEx/UE4SS/etc.) are a
modder-side concept, not a user gate. See `docs/adapters/engine-hooking-strategy.md`.

## Verification command cheat-sheet

```powershell
# Default OFF gate (the green bar): builds, runs CTest + Python validators,
# writes a run JSON to .planning/evidence/runs/ and appends .planning/evidence/LEDGER.md
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\ci\build-and-test.ps1 -BuildDir build\ci

# OpenXR-ON compile+link (bootstraps external/openxr + external/vulkan if needed)
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\ci\build-and-test.ps1 -OpenXR on -BuildDir build\ci-on

# Dev-only headless Vulkan render-path validation (validation layers active, 0 findings expected)
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run-vulkan-render-validate.ps1

# Headset smoke (REQUIRES a Quest + active OpenXR runtime; first run is bring-up)
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run-headset-smoke.ps1 -Seconds 10
```

Run a single CTest target: `ctest --test-dir build\ci -R <name> --output-on-failure`
(target names per subsystem are listed in each subsystem doc).

## Evidence discipline

Every verified-green increment: a run JSON under `.planning/evidence/runs/`, a
row in `.planning/evidence/LEDGER.md`, and a commit on the working branch (the
commit hash makes the ledger reference meaningful). Headset-smoke evidence lives
in a **separate lane** (`artifacts/headset/<ts>/session-evidence.json`) and is
**never** the OFF green gate.

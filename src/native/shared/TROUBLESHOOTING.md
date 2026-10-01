# Phase 5 — Input / Comfort / HUD: Troubleshooting & Bug-Checking

Covers `src/native/shared/`: `input/` (input_system), `comfort/`
(comfort_system), `hud/` (hud_system), plus `game_profile`. These are exposed to
adapters as optional, versioned services (`VrClientInputService`,
`VrClientComfortService`, `VrClientHudService`) — see
`docs/sdk/adapter-authoring.md`.

Read the [cross-cutting index](../TROUBLESHOOTING.md) first.

## Real bugs / lessons here

### `assert()` → NDEBUG (converted)
`shared_system_tests.cpp` (CTest `vr_shared_unit_tests`) previously asserted via
`assert()` → vacuous under Release; now throwing `expect()`. See cross-cutting
trap #1.

### HUD matrix convention is ROW-major — the cross-phase trap
HUD transforms are **row-major 4×4 matrices in meters**, right-handed,
forward = −Z (per `docs/sdk/adapter-authoring.md`). The Phase 1 runtime
(`stereo_math`) is **column-major / column-vector**. Crossing this boundary
without an explicit transpose/convert places HUD elements wrong — and like the
Phase 1 projection bug, **unit tests and validation layers won't catch it**;
it's only visibly wrong in the headset. Whenever HUD transforms meet runtime
matrices, state and test the convention at the seam. See Phase 1
[runtime/TROUBLESHOOTING.md](../runtime/TROUBLESHOOTING.md) projection war-story.

### Services are optional — adapters must degrade, not crash
Input/comfort/HUD service handles are **optional**, with `size` + `version`
fields, host-owned. An adapter must query availability and **degrade cleanly if
a service is absent** (so Phase-5 services can be added without breaking SDK v1
adapters).
- **Check:** test the **absent-service** path, not just present. A change that
  assumes a service is always there breaks older adapters.

### Semantic actions, not raw buttons
Input services return semantic actions (`interact`, `recenter`, comfort toggles).
Adapters consume semantics, **not** device-specific controller buttons. Don't
add raw-button passthrough that bypasses the semantic layer.

### Comfort is profile-backed and runtime-changeable
Comfort snapshot (snap/smooth turn, vignette, seated/standing, world scale,
height offset) is profile-backed and changeable **without recompiling adapters**.
Per-game data belongs in game profiles + adapter manifests, not hard-coded in
shared/runtime/manager code.

## Verification

```powershell
# OFF gate runs: vr_shared_unit_tests, shared_static_checks, game_profile_schema
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\ci\build-and-test.ps1 -BuildDir build\ci
ctest --test-dir build\ci -R "shared|game_profile" --output-on-failure
```

## Bug-checking checklist

- [ ] HUD↔runtime matrix seams convert between row-major and column-major
      explicitly, and are tested.
- [ ] Absent-service path tested; adapters degrade cleanly.
- [ ] Input stays semantic (no raw-button bypass).
- [ ] Per-game values live in profiles/manifests, not shared/runtime code.
- [ ] Service structs stay `size`/`version`-gated and host-owned.
- [ ] Tests use throwing `expect()`, not `assert()`.

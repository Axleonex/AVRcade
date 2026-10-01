# RDR2 Release Candidate Matrix

## Scope

Applies to Steam-owned Story Mode only. No Red Dead Online support.

## Required scenarios

- Clean install + safety preflight
- Profile-backed mod install/disable/recover
- Unknown build/refuse behavior
- Live Story Mode evidence capture (read-only diagnostic layer)
- Headset smoke preconditions and blocker handling
- User-visible safety messages for unsupported states

## Hard requirements for release state

- `rdr2-readiness` must report a specific blocker until the native bridge
  services and one Story Mode smoke run are validated; no generic Phase 13
  placeholder is used.
- Repair/recover workflow exists for partial transactions and never mutates
  unverified state.
- No upload behavior for diagnostics.
- Any uninstall path is explicit and non-destructive to saves.

## Incomplete items (current status)

- Native RAGE adapter contract and an optional in-process bridge payload are
  implemented; live stereo/camera/input evidence is not complete.
- The bridge artifact is staged externally, but ScriptHookRDR2 and the applied
  ASI remain user-owned prerequisites on the X: game root.
- One hardware Story Mode smoke run is not yet complete.
- HUD/cutscene/input comfort behavior has not yet been validated in-device.

This matrix is therefore blocked from release-state completion until those items are
evidenced.

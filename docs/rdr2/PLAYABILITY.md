# RDR2 VR Playability and Mod Workflow

## Scope

- Headset yaw/pitch with keyboard/mouse interaction fallback
- Headset comfort presets
- Cutscene/HUD/menu transitions
- Mod-profile lifecycle in Story Mode
- Recovery and disable/recover operations

## Non-goals in this phase

- Any claim of production-quality VR playability without a verified native RAGE
  adapter and measured user smoke evidence.
- Any bypass of Steam ownership flow or process safety gates.

## Required managed checks (to be run before user-facing playability claims)

- `rdr2-profile` can safely list/create/apply/disable/recover only after
  reviewed preflight readiness.
- Transaction rollback remains available after interrupted operations.
- No unmanaged write occurs outside `%LOCALAPPDATA%/VRClient/games/...` or the
  installed game mod roots.

## Launch path

`rdr2-launch --mode story` performs the pinned Steam/build checks and then starts
`steam.exe -applaunch 1174180 -dx12 -vrclient-rdr2-story`. It never starts
`RDR2.exe` directly and refuses
Online, unknown builds, missing bridge prerequisites, or missing Steam. Pass
`--profile baseline` (or another profile ID) to apply a validated profile (including the bridge ASI) before
launch; an automatically applied profile is rolled back if a later launch gate
fails. The bridge must be staged with:

```powershell
dotnet run --project client/VrClient.Cli/VrClient.Cli.csproj -c Release --no-build -- `
  rdr2-profile stage-bridge baseline
```

and both `ScriptHookRDR2.dll` and its `dinput8.dll` ASI loader must be installed
by the user in the RDR2 root. The
guarded bridge launch passes `-dx12`, because this first bridge implementation
captures the D3D12 swap chain; `--renderer vulkan` is refused until a Vulkan
capture path is implemented.

## Current rendering and input boundary

The current bridge is suitable for the first hardware smoke only. It copies the
game's monoscopic D3D12 output into both OpenXR eye targets and forwards headset
yaw/pitch to the gameplay camera. It does not yet render separate RAGE camera
views per eye, apply headset translation, or read OpenXR controller actions.
Keyboard/mouse input continues to reach RDR2 normally; the adapter's RAGE input
service is plumbing for later controller injection and does not currently write
controls into the game.

Consequently, `ready_for_smoke` means the exact build, bridge, live D3D12 path,
camera hook, and OpenXR submission were observed. It is not a claim of native
stereo, 6DoF, motion controls, comfort, or full playability.

## Open questions

- Which OpenXR action mappings provide acceptable default ergonomics for horse
  controls and cutscene transitions.
- Whether default comfort stack should include vignette, snap/continuous turn, and
  seated-only fallback for user classes.

## Evidence requirements

- At least one managed regression run of profile lifecycle scenarios.
- A documented blocker or pass for each transition class in this doc.
- A user-observed smoke report tied to this phase's readiness state.

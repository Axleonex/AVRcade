# RDR2 Story Mode VR Smoke Test Protocol

Purpose: one bounded hardware verification pass that transitions the work from
implementation assumptions to observed evidence.

## Prerequisites

- `rdr2-preflight --mode story` must report a reviewed Story Mode readiness path
  for the installed AppID 1174180 installation.
- The optional `vrclient_rdr2_bridge` payload must be built and staged in a
  profile, and the user-provided `ScriptHookRDR2.dll` plus `dinput8.dll` ASI
  loader must be in the RDR2 root.
- `rdr2-readiness --mode story` may remain evidence-blocked before this first
  run; the guarded launch still refuses if the bridge prerequisites are absent.
- OpenXR runtime is active and your headset is connected.
- Steam ownership chain remains normal (no direct `RDR2.exe` launch path).
- This pass must stay in Story Mode.

## Step sequence (exactly one run)

1. Run the guarded Steam-owned launch path:
   ```powershell
   dotnet run --project client/VrClient.Cli/VrClient.Cli.csproj -c Release --no-build -- rdr2-launch --mode story --profile baseline
   ```
2. Enter Story Mode.
3. Run live diagnostics once while stable:
   ```powershell
   dotnet run --project client/VrClient.Cli/VrClient.Cli.csproj -c Release --no-build -- rdr2-live-diagnostics --mode story --run-id smoke-story-mode-01
   ```
4. Observe OpenXR presentation, yaw/pitch head tracking, keyboard/mouse
   look/aim/fire/interaction/menu transitions, and no immediate instability for
   5-15 minutes. Treat identical-eye output as the expected limitation of this
   first relay smoke, not as proof of engine-native stereo.
5. Stop after a controlled period and report pass/fail for this one run.

## Output

The command above must point at the exact run output location and should produce
`RDR2-READINESS`-like fields describing the evidence that is available.

Failure classes remain blocking for release:

- Runtime cannot launch/attach as expected in Story Mode
- No OpenXR presentation or yaw/pitch head tracking despite an active headset
- Persistent keyboard/mouse control regressions
- Immediate crash, heavy motion instability, or uncontained state mutation

OpenXR motion-controller input, positional 6DoF, and independently rendered eye
views are not implemented by this smoke bridge and therefore cannot pass a
full-conversion acceptance test yet.

This protocol is intentionally one-pass and hardware-owned; repeated local smoke
attempts are allowed only after each blocker is fixed and documented.

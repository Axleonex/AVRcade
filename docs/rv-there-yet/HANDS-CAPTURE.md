# RV tracked-hands investigation

The imported d_rey86 profile supplies camera configuration, not a hand rig or
controller-to-animation mapping. Headset stereo and Xbox input emulation do
not drive the character's hand bones. Do not advertise motion-controlled hands
until a game-specific implementation has passed a headset test.

## Read-only capture

With RV and UEVR closed, copy `scripts/rv-there-yet/inspect-hands.lua` into
`%APPDATA%/UnrealVRMod/Ride-Win64-Shipping/scripts/vrclient_inspect_hands.lua`.
Launch in VR and enter a solo/private session. The probe samples at most once
every five seconds and stops after one skeletal pawn capture, an error, or ten
minutes. If it captures a menu pawn, reload scripts once in actual gameplay.
Read `[VRCLIENT-HANDS]` records from the profile's `log.txt`. Remove the probe after
capture; normal VRClient launches never deploy this developer diagnostic.

The probe reads only class/property names, component names and bone names on
the local pawn. It does not change camera, input, transforms or game state.
Lua error handling cannot guarantee native engine calls are crash-free; the
probe itself still requires a live validation run.

## Implementation gate

Use the captured mesh/animation classes to identify independent left/right
hand targets and the animation update seam. Confirm controller pose space,
character scale, handedness and mesh visibility. Never attach the entire pawn
or a shared full-body mesh to one controller. If no usable IK inputs exist,
implement a separate hand rig with an explicit scope before changing gameplay
interactions. Visible tracked hands and physical grabbing are separate features.

Validate both Quest Touch and Valve Index: raise/lower each hand independently,
rotate wrists, turn head without moving hands, snap-turn/recenter, lose/regain
tracking, enter/exit RV, use an item, and change levels. Preserve the original
animation on disable or unsupported builds. Keep the profile's motion-control
capability unverified until this evidence exists.

API references: [UEVR Lua callbacks](https://docs.uevr.io/plugins/lua/callbacks.html),
[UObject reflection](https://docs.uevr.io/plugins/lua/types/UObject.html), and
[UStruct properties](https://docs.uevr.io/plugins/lua/types/UStruct.html).

## 2026-09-27 handoff

- Quest/Index controller maps and front-page runtime notices are implemented.
- RV's on-demand Script UI reference is generated from the same controller map
  on launch; no input or pose changes are made by the guide.
- Core tests: 266 passed. App/CLI debug builds passed with no warnings or errors.
  Controller JSON schema validation and RV CLI launch dry-run passed.
- Release app/CLI built and staged; staging-window smoke check passed. The
  canonical C: install was **not updated**: directory replacement failed because
  Windows reported it in use. A subsequent backed-up file-update attempt was
  rejected by execution policy and did not run. Existing installed app retained.
- Read-only probe installed at
  `%APPDATA%/UnrealVRMod/Ride-Win64-Shipping/scripts/vrclient_inspect_hands.lua`.
  It uses `uevr.params.functions.log_info` to persist capture records in `log.txt`.
  The first live run failed because this shipping build does not reflect
  `Actor.GetComponentsByClass`. The probe now uses UEVR's class-instance
  lookup and component outer ownership; its revised version awaits a headset
  run in gameplay. Remove it after capture.
- Actual tracked hands, camera conflict resolution, and seamless SteamVR startup
  remain unfinished. Do not claim the controller-reference work implements them.

## 2026-09-27 live gameplay capture

The corrected probe completed in `JungleLevel` on `BP_FirstPersonCharacter_C_0`.
The pawn owns `FirstPersonMesh` (84 bones) and
`SK_BoxyRider_FPP_Arms_01` (87 bones). Both contain `hand_l`, `hand_r`, arm,
finger, and `ik_hand_l`/`ik_hand_r` bones. The first-person animation instances
are `BPA_BoxyRider_FPP_C` and `BPA_BoxyRider_FPP_Arms_C` respectively.
Reflected animation fields include `Use Arm IK`, `IKTarget`, `IKActor`, and
left/right hand target properties on the arms instance. The full-body
`BPA_BoxyRider_C` also exposes `RightArmIK`, `LeftArmIK`, and `Ik Target`.
These names establish a plausible rig path, not that any particular target is
safe to write or which space it expects. The probe was moved to a `.bak` file
outside UEVR's `*.lua` load glob after capture; it no longer runs on startup.

Next, inspect target property types and live values in a solo session, then
prototype controller grip pose to left/right IK target with a reversible
enable switch. Validate world/component-space conversion, shoulder/elbow
behavior, camera decoupling, held items, menu transitions, and Quest/Index
handedness before packaging tracked hands for ordinary users.

The one-shot `scripts/rv-there-yet/inspect-ik-targets.lua` is currently staged
in the live UEVR profile as `vrclient_inspect_ik_targets.lua`. Its first live
run identified the first-person arms instance `BPA_BoxyRider_FPP_Arms_C`,
two left/right `HandLTarget`/`HandRTarget` transform pairs, `LeftArmIK` and
`RightArmIK` transforms, and disabled `Use Arm IK`/`UseArmIK` flags. The
character was in `JungleLevel`; controller indices were left=1 and right=2.
This establishes a possible independent-hand IK seam, but not the target
coordinate space, which transform pair the graph consumes, or whether enabling
it preserves normal animations and interaction.

The first probe attempted `get_grip_pose`, which this pinned UEVR nightly does
not expose. It logged HMD pose but no controller poses. The installed probe
now uses the available `vr.get_pose` for both controllers and tries to read
the nested transform values. Its corrected version has not yet run in the
headset. It takes two read-only gameplay snapshots five seconds apart and does
not enable tracked hands. The next solo gameplay run must capture the two
controller poses and target defaults, then a separate reversible prototype can
test pose-to-IK mapping. Remove or rename the probe to `.bak` after capture.

## 2026-09-30 opt-in prototype

The corrected probe subsequently ran in solo gameplay on 2026-09-29. The live
UEVR log at `%APPDATA%/UnrealVRMod/Ride-Win64-Shipping/log.txt` contains two
snapshots of the first-person arms animation instance, left/right IK target
transforms, disabled arm-IK flags, and controller pose indices (left 1, right
2). This establishes that the required rig objects exist on this game build;
it does **not** establish which target pair the animation graph consumes or
the target space and wrist offsets.

AVRcade now has an off-by-default **Controller-driven hands (experimental)**
setting in the source build on the RV page. The preference persists. On an AVRcade RV VR launch,
its own UEVR Lua script is synced into the per-game `scripts` directory and a
small data file turns it on. Disabling the setting immediately writes `false`
for a running script. The driver is scoped to the local
`BP_FirstPersonCharacter_C` and `BPA_BoxyRider_FPP_Arms_C`; it uses separate
UEVR controller anchors, converts their world transforms to the arm mesh's
component space, and writes the reflected hand IK targets. It does not touch
game input, grabbing, other avatars, or the imported community profile. A
rig/API failure stops the driver for that run and clears its IK-enable flags.

The code and preference/file-deployment tests build and pass, but **the hand
motion itself has not yet passed a headset test**. The next validation is a
solo gameplay run with the option enabled: check left/right mapping, moving
both controllers independently, elbow behavior, item holding, menu transitions,
and return to normal animation when switched off. Check `[AVRCADE-HANDS]` lines
in the UEVR log if hands remain at the sides or look displaced. Do not present
this as finished tracked hands to ordinary users until those checks pass.
The C: desktop installation still needs to receive both the updated app and
CLI executables; updating only one will not activate the option correctly.

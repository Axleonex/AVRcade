# Cyberpunk Opt-In Launch, Global Controller Profiles, and Tracked Hands

**Status:** Plan 1 implemented; three-path human launch verification pending. Plans 2 and 3 not started.  
**Order:** Launch isolation -> controller profiles -> tracked hands  
**Target:** Cyberpunk 2077 first, reusable VRClient contracts where appropriate

## Outcome

Cyberpunk launched from Steam, REDlauncher, or its executable remains a normal
flat-screen game. VR starts only from an explicit VRClient action. VRClient also
gains reusable controller profiles with Quest Touch and Valve Index bindings,
and Cyberpunk gains an optional tracked-hands tier after the launch boundary is
proven safe.

## Locked product decisions

- Outside-VRClient launches are flat by default. Installed VR support must not
  make every game launch enter VR.
- The primary VRClient action is **Launch in VR**. A secondary **Launch flat**
  action is available for convenience and verification.
- Activation is per process, not a persistent global switch. An interrupted or
  crashed VRClient session must not make the next Steam launch enter VR.
- The current comfort behavior is preserved in VR and bypassed in flat mode.
- Controller settings are global defaults with per-game overrides.
- Presets are **Native VR**, **Xbox-style**, **PlayStation-style**, and
  **Custom**. For XInput-only games, PlayStation-style changes the physical
  layout preference; it cannot change the game's Xbox glyphs by itself.
- Quest Touch and Valve Index are first-class interaction profiles. Other
  OpenXR controllers use supported bindings or a clearly labeled fallback.
- Tracked hands are optional. Failure of hand/avatar IK must not take down
  stereo rendering, head tracking, or flat play.
- Full-body/weapon features are promoted in stages. Basic controller-following
  hands come before weapon aim, holsters, smoking, or motion melee.

## Architecture

### 1. Per-process VR activation

VRClient launches Cyberpunk with `VRCLIENT_VR_ACTIVE=1` and the selected
`XR_RUNTIME_JSON`. A normal launch has no activation variable and is therefore
flat. The installed RED4ext stereo plugin still loads, but it remains passive
before applying settings, installing D3D12/OpenXR hooks, or starting worker
threads.

The native plugin always registers one read-only function,
`IsVRClientVrActive()`, so redscript wrappers can preserve vanilla behavior in
flat mode. At load it also writes the current `0` or `1` state to the existing
CET bridge directory; the CET VRCAM selector reads that state before enabling a
camera component. This makes stale files harmless because every process rewrites
the state before CET initializes.

No plugin-directory rename or copy-on-launch scheme is used. Those schemes can
leave VR enabled after a launcher crash and create avoidable file-lock and
repair problems.

### 2. Semantic controller profiles

OpenXR physical inputs bind to semantic VRClient actions first, such as
`move`, `turn`, `interact`, `confirm`, `cancel`, `pause`, `hub`, `recenter`,
`overlay`, `primary_use`, and `secondary_use`. A game adapter then translates
those actions to its game-facing API. Cyberpunk's current adapter translates to
XInput, while a native Unity or Unreal adapter may consume the same semantics
directly.

Precedence is:

1. per-game custom override;
2. selected global preset;
3. detected OpenXR interaction-profile default;
4. safe fallback with an explicit missing-binding warning.

`Pause`, `recenter`, and `overlay` are reserved navigation actions. Custom
profiles must reject collisions that make any of them unreachable.

### 3. Optional Cyberpunk tracked hands

The pinned MIT CyberpunkVR Port source already contains the separate
`CyberpunkVR_Hands.dll` plugin, shared pose bridge, VRIK module, and weapon-aim
modules. The current VRClient install intentionally excludes them, which is why
the visible hands do not yet follow controller positions.

Hands are enabled in tiers:

- **Tier H0:** existing stereo/head tracking and gamepad translation;
- **Tier H1:** controller pose telemetry and a diagnostic skeleton, no avatar
  mutation;
- **Tier H2:** wrist/arm IK follows both controllers with calibration, weapon
  aim still vanilla;
- **Tier H3:** controller-directed weapon aim, opt-in and separately verified;
- **Tier H4:** optional holsters, melee, and other immersion modules.

Cutscenes default to a comfort-safe policy: head tracking stays active, while
hand IK is hidden or relaxed when the game owns the cinematic body pose. A
future immersive cutscene mode may be opt-in after comfort testing.

## Execution plan

### Plan 1 — Flat-by-default launch contract

**Wave:** 1  
**Goal:** Prove that installed Cyberpunk VR support is inert unless VRClient
explicitly activates it.

Files expected to change:

- `client/VrClient.Core/Launch/GameLauncher.cs`
- `client/VrClient.Core/Redengine/CyberpunkLaunchSession.cs` (new)
- `client/VrClient.Cli/Program.cs`
- `client/VrClient.Core/App/AppController.cs`
- `client/VrClient.App/ViewModels/MainWindowViewModel.cs`
- `client/VrClient.App/ViewModels/GameItemViewModel.cs`
- `client/VrClient.App/Views/MainWindow.axaml`
- `patches/cyberpunk-vr-port/f5e59d7-activation-gate.patch` (new)
- `config/redengine/native/cyberpunk-2077.json`
- launch, preflight, patch, and UI tests under `client/VrClient.Core.Tests/`
  and `tests/native/adapters/`

Tasks:

1. Add failing tests for the three launch cases: environment absent -> flat,
   `VRCLIENT_VR_ACTIVE=0` -> flat, exact value `1` -> VR. Also test that
   runtime selection is applied only to the launched child process.
2. Patch the stereo plugin so passive mode registers
   `IsVRClientVrActive()` and writes CET state, but does not call
   `ApplyFirstLaunchGameSettings`, `CyberpunkVRPort_InitStereo`,
   `CyberpunkVRPort_PluginBootstrap`, or `StartWorkerThread`.
3. Gate `CyberpunkVRPort_Stereo` VRCAM activation on the native bridge state.
   On a flat launch it must disable any authored `vrcam_*` component and publish
   an empty active-camera value.
4. Gate every behavior-changing `CyberpunkVRPort_NoAnims` wrapper with
   `IsVRClientVrActive()`. Flat mode must call the wrapped vanilla method with
   its original arguments and values.
5. Add explicit **Launch in VR** and **Launch flat** app/CLI routes. Do not
   expose an ambiguous persistent toggle on the game card.
6. Record launch mode, activation source, runtime, and process ID in local
   diagnostics without uploading anything.

Acceptance gates:

- Steam/executable launch with VR files installed shows the normal flat game,
  does not create an OpenXR instance, leaves all VRCAM components disabled, and
  preserves vanilla camera/equip/sprint/recoil behavior.
- VRClient **Launch in VR** produces the already verified stereo, head tracking,
  controller input, and comfort behavior.
- VRClient **Launch flat** produces the same result as an external flat launch.
- Killing VRClient after the game starts cannot alter the next external launch;
  activation lives only in the child process environment.
- Unsupported Cyberpunk builds still fail preflight before VR activation.

### Plan 2 — Global controller configuration

**Wave:** 2, after Plan 1  
**Goal:** Replace Cyberpunk's hard-coded gameplay map with reusable semantic
profiles while preserving the working default.

Files expected to change:

- `config/schemas/controller-profile.schema.json` (new)
- `config/controllers/native-vr.json` (new)
- `config/controllers/xbox-style.json` (new)
- `config/controllers/playstation-style.json` (new)
- `config/profiles/cyberpunk-2077-game-profile.json`
- `src/native/shared/input/input_system.*`
- `src/native/shared/game_profile.*`
- `client/VrClient.Core/Config/ControllerProfile*.cs` (new)
- `client/VrClient.App/ViewModels/ControllerSettingsViewModel.cs` (new)
- `client/VrClient.App/Views/ControllerSettingsView.axaml` (new)
- the pinned Cyberpunk input patch and associated tests

Tasks:

1. Define a versioned schema for semantic actions, OpenXR interaction-profile
   paths, output bindings, chords, thresholds, handedness, and context
   (`gameplay`, `menu`, `vehicle`, `always`).
2. Add Quest Touch and Valve Index physical defaults. Retain Vive, WMR, and KHR
   simple-controller fallbacks with capability warnings.
3. Add Native VR, Xbox-style, and PlayStation-style presets. Make Xbox-style
   the Cyberpunk default because its supported controller API is XInput.
4. Add a settings screen with preset selection, live conflict validation,
   restore-defaults, and per-game override controls. Custom editing cannot save
   if pause, recenter, or overlay becomes unreachable.
5. Generate the Cyberpunk XInput translation from the resolved profile instead
   of hard-coded button constants. Keep the confirmed menu correction and a
   reachable Hub/Map fallback.
6. Log the active interaction profile, selected preset, override source, and
   unresolved actions for support diagnostics.

Acceptance gates:

- Quest Touch and Valve Index each pass a table-driven action test for gameplay
  and menus.
- The opening intro, Pause, Hub/Map, confirm/cancel, movement, sprint, crouch,
  interact/reload, jump, aim, and fire are reachable without a keyboard.
- A global preset applies to a second synthetic adapter without Cyberpunk code.
- A per-game override changes only Cyberpunk.
- An invalid custom profile is rejected with the exact conflicting actions.
- Existing physical gamepads continue to merge with VR input.

### Plan 3 — Cyberpunk tracked hands

**Wave:** 2 for audit/H1; H2-H4 follow successful H1 evidence  
**Goal:** Make the avatar's hands follow controller position and orientation
without regressing stereo, comfort, flat mode, or ordinary controller play.

Files expected to change:

- pinned `src/red4ext_plugin/` hands source and build recipe
- `patches/cyberpunk-vr-port/f5e59d7-hands-activation.patch` (new)
- selected `CyberpunkVRPort_VRIK` CET/redscript modules
- `config/redengine/native/cyberpunk-2077.json`
- `config/redengine/native/cyberpunk-2077-installed-files.json`
- `config/profiles/cyberpunk-2077-game-profile.json`
- dependency, manifest, static, and controlled plugin tests

Tasks:

1. Audit and build `CyberpunkVR_Hands.dll` separately from the stereo plugin,
   pin all inputs, scan the result, and enumerate the minimum VRIK dependencies.
   Do not install holster, smoking, melee, HUD, or weapon modules in H1.
2. Apply the same `VRCLIENT_VR_ACTIVE=1` gate before any hands hook or mutation.
   Flat mode must register only the harmless status surface and remain inert.
3. Run H1 pose telemetry and a diagnostic skeleton first. Verify left/right
   identity, position/orientation validity, scale, handedness, and stale-pose
   handling without touching avatar bones.
4. Enable H2 wrist/arm IK with per-user calibration for height, arm length,
   shoulder offsets, dominant hand, and seated/standing mode. Clamp impossible
   reach instead of pulling the camera or torso into the controller.
5. Add state policies for sprint, melee, ladders, vehicles, menus, photo mode,
   and cutscenes. Losing tracking must blend safely to a neutral pose.
6. Promote H3 weapon aim only after H2 passes. Keep ballistic/gameplay logic
   separate from presentation and provide a one-click fallback to vanilla aim.
7. Treat H4 immersion modules as optional packages with independent toggles and
   rollback records.

Acceptance gates:

- H1 shows correct left/right controller poses for at least two controller
  interaction profiles without avatar mutation.
- H2 hands remain visually attached to controllers during idle, walking,
  running, crouching, and turning, with no camera pull into the body.
- Tracking loss and controller sleep recover without a crash or stuck limb.
- Flat launches do not run hands hooks or alter animation.
- Disabling tracked hands returns to the current working H0 experience.
- H3 firearm direction follows the dominant controller while the view remains
  HMD-controlled; melee and holsters remain off until their own gates pass.

## Verification sequence

1. Headless/unit tests and schema validation.
2. Controlled RED4ext load tests for active and passive modes.
3. External flat launch, then VRClient flat launch.
4. VRClient VR launch on Quest Touch using VirtualDesktopXR.
5. VRClient VR launch on Valve Index/SteamVR hardware when available; until
   then, interaction-profile binding tests are evidence, not headset proof.
6. H1 diagnostics, H2 avatar IK, then H3 weapon aim. Do not skip tiers.

## Definition of done

- VR is opt-in per launch and never forced by installation state.
- A non-technical user can choose **Launch in VR** or **Launch flat** without
  moving files, editing Steam options, or opening CET.
- Controller mappings are reusable global data with safe per-game overrides.
- Quest Touch and Valve Index have usable defaults and all navigation actions
  remain reachable.
- Cyberpunk hands can follow controllers as an optional, calibrated feature;
  disabling or failing that feature leaves the proven H0 VR path intact.

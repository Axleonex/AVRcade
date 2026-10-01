# Final human verification protocol

Do not mark the integration headset-verified until both route sections and the restoration section pass. Capture exact versions, local hashes, headset/runtime versions, logs, screenshots where safe, and each result as PASS/FAIL/BLOCKED. Never attach game files, account data, or saves containing personal data.

## Preparation

1. Start from a legitimately owned Steam or GOG installation. Verify normal non-VR launch and make a fresh manual save in a disposable test slot.
2. Update the isolated `New Vegas VR` MO2 profile only. Confirm the normal non-VR profile and its `modlist.txt`, `plugins.txt`, INIs, and save selection are unchanged.
3. Install user-selected official packages: FNV 4GB Patcher, xNVSE, JIP PP LN 57.54 or newer, ShowOff xNVSE, FNVR V2/`FNVR.esp`, FNVR Tracker V2, MO2, and SteamVR. Build the VRClient x86 native adapter.
4. Confirm the launch-plan JSON sets `XR_RUNTIME_JSON` to SteamVR on the MO2/xNVSE component. FNVR Tracker uses OpenVR, so the game-local OpenXR override must resolve to the same SteamVR coordinate space. The global runtime may remain VDXR.
5. Run CLI `preflight`; require no failures. Review every warning, especially MO2-virtualized files and unknown versions.
6. Generate compatibility and comfort reports. Review every warned/unknown mod; do not silently disable anything. Pick a small representative set: one quest/content ESP, one xNVSE plugin, one HUD/UI mod, and one weapon/animation mod if available.
7. Run `launch ... --dry-run`. Confirm order is Virtual Desktop when selected, SteamVR, FNVR Tracker, MO2 with the isolated profile and `nvse_loader.exe`, then the VRClient native OpenXR adapter.

## Route A — wired/native SteamVR

1. Connect the headset by its native wired/PCVR route. Start SteamVR and confirm headset/controller icons are green.
2. Start the VRClient launch command without `--virtual-desktop`. Confirm visible SteamVR and FNVR Tracker windows remain user-controllable.
3. In FNVR Tracker, assign Primary and Secondary by moving each physical controller and observing the corresponding pose values.
4. Launch the game through MO2/xNVSE. Confirm `FNVR.esp`, JIP PP LN, and ShowOff load without missing-dependency messages. Record component logs and exit codes.
5. Stereo/per-eye: view near and far geometry, close one eye at a time, and verify distinct per-eye images with stable depth. Reject duplicated mono, swapped eyes, extreme disparity, or one-eye rendering.
6. Projection: rotate and translate the head around high-contrast vertical/horizontal edges. Verify correct per-eye projection without shear, asymmetric scale, eye mismatch, or world-locked wobble.
7. 6DoF head tracking: test yaw, pitch, roll, left/right, up/down, and forward/back. Verify one-to-one direction, acceptable scale, and no double positional tracking. Press F8 once and confirm native recentering is stable.
8. Both motion controllers: verify Primary weapon hand and Secondary hotkey hand, including trigger, grip, sticks, menu actions, and recenter. Swap handedness and repeat if left-handed play is supported for release.
9. Weapon alignment and shot direction: test pistol, rifle, scoped weapon, automatic weapon, thrown item, and melee. Calibrate using FNVR's documented X-key offset (keyboard or an explicit SteamVR binding); confirm muzzle/impact direction follows the controller, not the headset. Reset and recalibrate once.
10. Locomotion and turning: walk, strafe, backpedal, sprint if provided, crouch, stairs, slopes, snap turn, and smooth turn where the selected input stack supports them. Confirm controller-centric orientation and assess nausea/acceleration.
11. HUD and dialogue: verify compass, health/AP, crosshair/interaction prompts, subtitles, dialogue choices, barter/container menus, pause/ESC menu, terminals, and any HUD mod at readable scale/depth.
12. Pip-Boy and gestures: calibrate every intended gesture in FNVR Tracker; verify Pip-Boy, ESC, and hotkeys 1–8 trigger once without accidental activation. Test sensitivity at normal standing/seated posture.
13. VATS boundary: confirm the user-facing warning that FNVR upstream does not support VATS. If a bullet-time alternative is selected, test it separately and do not represent it as native VATS support.
14. Save/load: create a new VR test save, reload it in the same session, exit, relaunch, and load again. Never overwrite the baseline non-VR save. Record any cosave/plugin warning.
15. Representative mods: exercise the selected content ESP, xNVSE plugin, HUD mod, and weapon/animation mod. Update the compatibility report with observed verified/warned/incompatible results; one successful scene is not blanket compatibility.
16. Clean shutdown: exit to desktop normally. Confirm game/MO2 and native-adapter exit codes, tracker cleanup only if VRClient started it, and that pre-existing SteamVR remains under user control.

## Route B — Quest through Virtual Desktop to SteamVR

1. Wire the PC to a 5 GHz AC/AX router as the Virtual Desktop official guidance recommends. Connect the Quest to Virtual Desktop Streamer and verify controller/headset tracking before launch.
2. Start SteamVR through the Virtual Desktop PCVR path. Do not select an Oculus-only path for this test; the required route is Virtual Desktop to SteamVR.
3. Run launch with `--virtual-desktop --virtual-desktop-exe <Streamer path>`. Confirm VRClient distinguishes this route in JSON output and does not start a second Streamer when one is already running.
4. Repeat Route A steps 3–16 in full. In addition, record Wi-Fi conditions, streaming bitrate/codec, reprojection, network latency, controller latency, image artifacts, and any disconnect/reconnect behavior.
5. Suspend or disconnect the headset once during a safe menu state. Confirm the game/helper failure is actionable, no unrelated process is killed, and a subsequent repair/preflight/launch succeeds.

## Restore and non-VR regression

1. Run `restore ... --dry-run`, review that only the VRClient-managed marker is targeted, then repeat with `--acknowledge`.
2. Confirm the isolated VR profile still exists for forensic/recovery purposes and no mods or cloned profile files were deleted.
3. Launch the original non-VR profile through MO2. Confirm its mod order, enabled ESP/ESM set, xNVSE plugins, INIs, and baseline save are unchanged.
4. Load the baseline non-VR save, play for several minutes, save to a new slot, exit cleanly, and compare profile files with the pre-test backup.
5. Mark restoration PASS only when the normal non-VR profile works and no VR helper is required.

## Release verdict

- PASS: both routes, all stereo/per-eye/6DoF/controller/gameplay/save/cleanup gates, representative mods, and restoration pass with retained evidence.
- CONDITIONAL: core play works but a named hardware/mod/comfort item remains warned; publish the exact limitation.
- FAIL: mono/incorrect eyes, broken projection, missing 6DoF/controller, weapon/shot mismatch, corrupted save/profile, uncontrolled process cleanup, license bypass, or failed non-VR restoration.

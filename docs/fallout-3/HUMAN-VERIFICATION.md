# Fallout 3 Final Human Verification

Run these procedures only with a legitimate Fallout 3/GOTY installation and a connected headset. Preserve the normal MO2 profile and saves first. Capture the generated readiness report, compatibility report, launch plan, VRClient logs, ReShade log, Osiris log and `fallout3-native.log` when applicable.

## Vortex in-app window check

1. With Vortex installed on any connected drive, open Fallout 3 in AVRcade and choose **Manage Fallout 3 mods in Vortex**. Confirm Vortex appears inside the panel and remains fully interactive; test navigation, dropdowns, mod enable/disable, deployment, profiles, and file dialogs. If it stays in a separate window, record the status message and display scaling/DPI of both apps.
2. Close the panel. Confirm Vortex returns to a normal independent window and is not terminated. Reopen the panel and confirm it can attach again. Test Vortex already running before AVRcade.
3. Disconnect and reconnect the Vortex drive. Confirm detection fails clearly while disconnected and succeeds after **Detect again**. For a portable install, choose **Find Vortex.exe**, restart AVRcade, and confirm the selected path is remembered.
4. In Vortex, select Fallout 3. If the staging folder is on a different drive from the game, use Vortex's own supported move-staging flow to place it on the game's NTFS volume before deploying; do not move Vortex-managed folders manually. Deploy a representative harmless mod to the intended game directory and confirm the active profile. Launch AVRcade's native VR test and verify the mod loads without AVRcade changing Vortex's deployment or profile data. This does not establish universal mod compatibility or headset VR correctness.
5. Verify all three installed-app actions separately: **Native VR** and **VR with Vortex mods** must activate the exact-build native hook only for their FOSE child process; **Desktop with deployed mods (VR off)** must start Fallout 3 through FOSE without native VR hooks while retaining the selected Vortex-deployed content. Test Vortex's own **Play** action as a second desktop route. Confirm game/plugin logs and an unmistakable representative mod in-game; the presence of buttons or staged files alone is insufficient. Both VR actions share the installed game directory rather than isolating Vortex profiles.

## A. SteamVR — Depth VR - Free

1. In SteamVR settings, set SteamVR as the active OpenXR runtime. Connect/wake the headset and confirm `vrserver`/`vrmonitor` are running.
2. Run `fallout3-vr discover`, then `check --backend depth` with the real game, MO2, user-supplied ReShade/SuperDepth3D, Osiris and SteamVR manifest paths. Resolve every failure; review every warning.
3. Run `plan --backend depth`. Confirm the plan names **Depth VR - Free**, creates only `VRClient Fallout 3 VR`, preserves the source profile/load order, and lists no unexpected path.
4. Run `apply ... --backend depth --acknowledge`. Repeat it once and confirm the second run is idempotent. Generate the compatibility report and review every unknown/conflict.
5. In ReShade, select `VRClient-ReShadePreset.ini`. Use DisplayDepth first. Confirm geometry is represented, depth is not reversed/upside-down, hands/weapons do not destroy the useful range, and menus/HUD do not contaminate the chosen buffer.
6. Enable SuperDepth3D full-SBS. Start Osiris in Full-SBS desktop-capture mode. Launch through the CLI/MO2 plan with all helper windows visible.
7. Verify stereo correctness with a near object, mid-distance character and far landmark. Check eye order explicitly; swap eyes if depth is inverted. Check convergence, scale, halos and weapon-depth artifacts.
8. Verify head rotation in yaw/pitch/roll and recenter. Record whether positional movement is absent, simulated, screen-relative or usable. Do not mark 6DoF or tracked weapons passed from controller buttons alone.
9. Verify gamepad/controller mapping, dominant hand guidance, snap/smooth turning if configured, sensitivity, seated/standing comfort, vignette if viewer-supported, FOV, HUD scale, head bob, blur and forced camera motion.
10. Exercise the Pip-Boy, dialogue, terminals, lockpicking, VATS, inventory/map, pause/settings, combat, weapon switching, indoor/outdoor transitions, water, shadows and weather.
11. Save, load, change cells, fast travel, die/reload, and return to the main menu. Confirm existing saves are not overwritten unexpectedly.
12. Repeat with representative FOSE DLL, camera/animation, skeleton/body, HUD, weather/lighting, texture/LOD and input mods from the compatibility report. Record each result rather than promoting a whole category.
13. Observe frame pacing, missed frames, latency and GPU/CPU load. Test at least 15 minutes in combat and a dense exterior.
14. Exit the game normally. Confirm Osiris started by VRClient is cleaned up, SteamVR remains user-controlled, the normal MO2 profile is unchanged, and `restore` removes only managed files.

## B. Virtual Desktop — Depth VR - Free

1. In Virtual Desktop Streamer, first select VirtualDesktopXR as the OpenXR runtime. Connect the headset wirelessly and confirm the streamer process is running.
2. Repeat the Depth VR readiness, plan and launch steps with the 32-bit VirtualDesktopXR manifest. Confirm no custom/proprietary integration is used.
3. Verify stereo, eye order, convergence, head rotation, positional behavior, controller mapping, recenter and the complete gameplay/menu checklist from section A.
4. Measure wireless latency, encoder/network spikes and frame pacing in a dense exterior and combat. Record codec, bitrate, refresh rate and network conditions.
5. Disconnect/reconnect the headset, recenter, sleep/wake the headset and recover the session. Record any stereo-source or head-output failure.
6. Switch Virtual Desktop Streamer to SteamVR, set SteamVR as OpenXR runtime, and repeat launch as the **Virtual Desktop -> SteamVR** transport fallback. Confirm the selected runtime shown in diagnostics matches the actual path.
7. Exit cleanly and verify only VRClient-started helpers are stopped.

## C. Native VR - Experimental

The reviewed experimental profile for Steam/GOTY 1.7.0.3 is `fallout3-native-profile-steam-1.7.0.3.ini`. It applies only to executable SHA-256 `03CF5ADA02FCF789FCF4639E05127D545FF3F69363B926709B8F0C3F8AD29107`. The backend must automatically fall back to **Depth VR - Free** when the profile, x86 OpenXR runtime, native DLL, FOSE/build check or proof is missing.

1. Recompute Fallout3.exe SHA-256 and confirm it exactly matches the profile. For any other executable, stop; never infer addresses from this build.
2. Place `vrclient_fallout3_native.dll`, the reviewed profile and the project-built x86 `openxr_loader.dll` together in the isolated MO2 mod's `Data\FOSE\Plugins` directory. Confirm FOSE reports the plugin loaded, and confirm a changed hash or entry byte refuses before MinHook activation.
3. Record left and right Gamebryo view and projection matrices for the same simulation frame. Prove independent per-eye rendering: the matrices must be independently applied and produce different eye viewpoints; copying one completed desktop frame twice is a failure.
4. Capture the OpenXR projection-layer submission and verify left/right array slices contain the corresponding renders. Inspect eye order, asymmetric FOV, IPD scale, culling and near/far clipping.
5. Verify six-degree-of-freedom camera behavior: yaw/pitch/roll plus X/Y/Z translation. Press F8 and confirm recenter. Record head/body decoupling behavior. Do not claim motion-controlled weapons unless controller pose controls the weapon independently from the head.
6. Repeat Pip-Boy, dialogue, terminals, lockpicking, VATS, combat, save/load and representative-mod checks. Compare artifacts and compatibility against Depth VR.
7. Alt-tab, resize/change resolution, trigger a safe D3D9 reset, sleep/wake the headset, restart the OpenXR runtime and exercise a device loss path. Confirm resources are released, diagnostics identify the loss, and recovery or Depth VR fallback is graceful.
8. Measure frame timing for both game renders, D3D9 readback, D3D11 upload and OpenXR submission. Record dropped/rejected frames and visible judder.
9. Only after all evidence passes, create a proof artifact with `exact_build_match=true`, `independent_per_eye=true`, `headset_verified=true`, and `executable_sha256` equal to the tested `Fallout3.exe` SHA-256. Until then every UI, CLI, log and support report must remain **Native VR - Experimental**.

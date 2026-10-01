# RV There Yet? headset and gamepad protocol

Status: human execution required. Do not promote the catalog while any required
row is blank or failed.

Record the Steam build ID, SHA-256 of `Ride-Win64-Shipping.exe`, UEVR nightly
tag, profile commit, headset, GPU/driver, and selected runtime before testing.
Use `--runtime vdxr` or `--runtime steamvr` to make runtime intent explicit.

Run in order and stop at the first failure:

1. Injector attaches to `Ride-Win64-Shipping.exe`.
2. The headset receives distinct left/right eye images—not a floating desktop.
3. Rotation and translation tracking both update the game view.
4. Scale, horizon, height, and first-person camera are credible without a duplicate head/body camera.
5. HUD, menus, pause, and recenter are readable and operable.
6. Gamepad covers on-foot movement, interaction, item use, enter/exit RV, driving, winch, pause, recovery/death, and reconnect.
7. A cold relaunch reproduces the behavior and exits without orphan processes.

Test VDXR and SteamVR/OpenXR separately. A VDXR pass does not imply a SteamVR
pass, and Virtual Desktop streaming with SteamVR selected is a SteamVR route,
not a VDXR route. Store logs and observations outside the repository's shipped
profile until reviewed.

Passing this protocol proves only the headset + gamepad milestone. It does not
prove motion controls or private co-op.

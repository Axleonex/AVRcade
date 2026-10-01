# RV There Yet? headset menu diagnosis — 2026-09-27

Status: main menu is now visible in the headset via OpenXR/VDXR, per the user's
2026-09-27 test. Its initial 3D viewpoint is too low (in the sand); the menu
itself is working. A frontend-only 145 cm camera lift has been installed for
the next headset test, but its height and transition to gameplay are unverified.

The user sees RV's main menu on the PC monitor but only the 3D frontend
environment in the headset. Flat mode works normally. In the latest OpenVR
session (`13:22` in the UEVR log), RV's own log shows `WG_MainMenu_C_0`
receiving input focus. UEVR selected `openvr_api.dll`, located a UE 5.5 Slate
renderer through its `SlateOutputTexture` fallback, eventually hooked
`FSlateRHIRenderer::DrawWindow_RenderThread`, and created a game UI texture.
This proves menu creation and some UI capture initialization, not that the
headset overlay contained the correct menu pixels.

One profile experiment changed only `UI_FollowView=false` to `true`, backed up
as `config.before-ui-follow-20260927.txt`. After the next launch UEVR saved
`false` again, so the headset failure does **not** establish that follow-view
was active during the test. The profile is back to its prior value.

The successful experiment changed the injector's saved backend from OpenVR to
OpenXR. The 64-bit active OpenXR runtime is Virtual Desktop under HKCU;
SteamVR can be running independently. Original injector settings are backed up
as `user.before-rv-openxr-20260927.config` beside `user.config` in the
`UEVRInjector_Path_qlssrsimndl2w3yzlaoob3xwc4tb5fz0/1.0.0.0` directory.
The latest UEVR log reports `Requested runtime: openxr_loader.dll`, and the user
can see the menu in the headset. SteamVR is not established as a requirement
for this RV route. The menu-height script at
`scripts/rv-there-yet/menu-height.lua` watches the frontend controller and
temporarily changes only `VR_CameraUpOffset`; it restores the prior value on a
gameplay-controller transition. It does not change the pawn, menu widget, or
saved game. Test in the headset: verify view starts above sand, menu remains
clickable, and gameplay height returns to normal. If the view is still too low
or too high, calibrate the script's `menu_lift` against the actual scene rather
than changing the profile's global camera offset. Remove
`vrclient_menu_height.lua` from the live profile to revert this test.

Relevant upstream report: [OpenVR no-HUD case](https://github.com/praydog/UEVR/issues/426).
UEVR's [runtime guide](https://docs.uevr.io/usage/overview.html) distinguishes
OpenVR from OpenXR and notes Virtual Desktop should use OpenXR.

## Injector window interruption

On the later gameplay run, UEVR displayed a separate modal `VD Warning`
window: “Virtual Desktop has been detected running. Make sure you use OpenXR
for the least issues.” This was the window the user had to close with X before
entering the game; minimizing the main injector window could not dismiss it.
The selected backend was already OpenXR/VDXR. In the UEVR dialog, `Hide future
warnings` was checked and `OK` pressed. The injector's per-user `user.config`
now persists `IgnoreFutureVDWarnings=True`. This should suppress the warning
on subsequent launches, but a fresh headset launch has not yet confirmed it.
The VRClient CLI's successful-attach minimization remains a separate behavior.

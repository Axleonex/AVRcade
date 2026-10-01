# Grand Theft Auto: San Andreas — legacy VR integration

## What the project analysis found

VRClient currently has three conversion patterns:

- Unity ports install a verified community VR mod through the mod/profile layer and leave the mod manager's profile in control of the game mods.
- Unreal ports orchestrate the user's separately fetched UEVR runtime and a game-specific profile.
- Native ports own the engine bridge, stereo/camera/input/submission seams, and exact build gate; they are not considered playable until live headset evidence exists.

Classic GTA San Andreas matches none of the first two routes. The user install is a 32-bit Windows executable using the RenderWare/D3D9-era mod surface. It is not a Unity or Unreal title, so a UEVR profile cannot convert it. The classic mod ecosystem is ASI/loader-oriented; existing ASI plugins must remain in place and VRClient must not install a competing loader behind a mod manager's back.

## Current profile

`config/legacy/gta-san-andreas.json` pins the observed executable identity:

- executable: `gta_sa.exe`
- architecture: x86 / PE32
- renderer family: D3D9 RenderWare
- observed SHA-256: `a559aa772fd136379155efa71f00c47aad34bbfeae6196b0fe1047d0645cbd26`
- source posture: manually selected, offline-only

The profile records the ASI/config/directory surface as advisory evidence. The converter never copies, removes, replaces, or rewrites those files.

Patch-sensitive native data is kept separately in
`config/legacy/gta-san-andreas-native.json`. The bridge reads that versioned
profile at startup (or the path supplied by `VRCLIENT_GTASA_HOOK_CONFIG`) and
refuses to patch if it is missing, malformed, or does not match the profile
SHA-256 compiled into the reviewed bridge. Every active camera, projection,
menu-state, render-tail, and D3D9 hook address is sourced from this profile;
fixed hook sites are byte-signature checked against the pinned executable.

## What is implemented

The new `GtaSanAndreasPreflight` service and CLI commands:

```text
vrclient gta-sa-preflight --game-dir "W:\Grand Theft Auto San Andreas + Utilities\GTA San Andreas"
vrclient gta-sa-convert --game-dir "W:\Grand Theft Auto San Andreas + Utilities\GTA San Andreas"
```

They verify the executable's PE architecture and pinned hash, list observed mod artifacts, and fail closed until a bridge is present. `gta-sa-convert` is non-mutating by default. Passing `--install` explicitly stages only `vrclient_gtasa_theater.asi`, the matching x86 `openxr_loader.dll`, and the external native hook profile; it refuses to overwrite existing managed destinations. The desktop catalog honors `VRCLIENT_GTASA_DIR` and also probes mounted drive roots for the known `Grand Theft Auto San Andreas + Utilities\GTA San Andreas` layout, so changing an external drive letter does not orphan the install.

The experimental bridge now contains the native stereo path for the observed
build: D3D9 device capture, exact-build RenderWare render-seam hooks, OpenXR
view-pose lookup, per-eye camera-matrix/FOV/aspect injection, two world passes,
an OpenXR array swapchain, and a first-person gameplay anchor read from the
updated player head bone. The camera sits 0.12 game units forward of the bone
to keep the eye point outside the face mesh. Native asymmetric RenderWare
projection now keeps each complete eye viewport, preventing the HUD from being
cropped out at the asymmetric OpenXR edges. Menus and fades retain the
view-space panel path. Full animated cutscenes are detected from GTA's pinned
`CCutsceneMgr` running/processing flags and shown intact on a smaller head-locked
cinematic panel; GTA's own fades remain visible and the opaque compositor supplies
the black surround. If the player or head bone is temporarily unavailable,
the bridge falls back to GTA's current camera for that frame instead of failing.
Set `VRCLIENT_GTASA_FIRST_PERSON=0` only as a troubleshooting opt-out. The bridge
accepts the observed
`D3DFMT_A2R10G10B10` 10-bit backbuffer and converts it to the D3D11 upload
format before submission. If the game enables D3D9 MSAA, the bridge resolves
the multisampled backbuffer into a non-MSAA surface before readback, because
Direct3D 9 does not permit `GetRenderTargetData` to read an MSAA render target.
VRClient's explicit GTA launch path sets
`VRCLIENT_GTASA_STEREO=1` and still leaves the existing ASI/config/script
surface in place. The explicit VRClient launch also sets
`VRCLIENT_GTASA_INPUT=1`; this enables the experimental OpenXR action set and
keyboard/mouse fallback without replacing GInputSA or emulating a new gamepad
device. The same action set binds standard left/right OpenXR grip poses. Each
predicted display frame samples both controller spaces once, maps them through
the recentered first-person basis, and solves constrained two-bone arm chains.
An exact-build `CPed::PreRender` seam applies the immutable result to the local
player's upper-arm, forearm, and hand matrices for each eye, then restores the
native matrices byte-for-byte. Menus, fades, cutscenes, vehicles, tracking
loss, invalid skeletons, and unsupported builds keep GTA's native animation.
This is implementation evidence plus HMD-less live-process evidence,
not live headset evidence; the app therefore keeps the game `Profile
unverified` until the physical-headset checklist is completed.

The D3D9 path is guarded at two levels: the exact executable identity is
verified before any process patch, and the observed compatibility-wrapper
`CreateDevice` implementation is hooked only when its complete hot-patchable
x86 prologue matches. A first confirmed D3D9 `CreateDevice` and `Present` were
observed on the W: install. The first live OpenXR attempt exposed a
compatibility issue: the bridge requested the current 1.1.43 header version and
the active 32-bit runtime returned `XR_ERROR_API_VERSION_UNSUPPORTED` (`-4`).
The bridge now requests the OpenXR 1.0 baseline for older runtimes, releases
any partial XR state, and retries the probe on a bounded interval, so activating
the runtime after launch does not require a game restart. A follow-up live run
successfully created the OpenXR instance and reached `xrGetSystem`, where
Virtual Desktop returned `XR_ERROR_FORM_FACTOR_UNAVAILABLE` (`-35`): no HMD
form factor is currently available on this host. Session `STOPPING`,
`LOSS_PENDING`, and `EXITING` events now release the old session and retry
initialization instead of permanently disabling recovery. The bridge also
handles `XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING`, `XR_ERROR_INSTANCE_LOST`,
and the normal `XR_EVENT_UNAVAILABLE` poll result explicitly, so runtime
instance loss cannot leave stale XR handles active.

## Remaining validation

The code, W: deployment, and HMD-less live runtime gate are complete. Remaining
work is human headset verification rather than a known implementation gap:

1. Confirm perceived stereo scale, IPD, head pose, recenter behavior, and
   comfort on foot and in vehicles.
2. Confirm menus/cutscenes remain readable as view-space quads and gameplay
   returns to projection layers after each transition.
3. Confirm left/right arms follow the matching controllers, bend naturally,
   keep right-hand weapons attached, recover from tracking loss, and coexist
   with GInputSA, CLEO, widescreen, and script mods.
4. Exercise HUD, aiming, camera shake, save/load, resolution changes, device
   reset, runtime loss/recovery, and clean shutdown while wearing the headset.

The app deliberately retains `Profile unverified` until that independent human
gate passes. That label no longer means the machine-side conversion path is
missing: the production W: executable has completed both-eye capture and
submitted live stereo projection layers to the test OpenXR runtime.

## Human verification gate (W:)

The machine-verifiable gate is green. The catalog, built bridge, and installed
bridge all match SHA-256
`5e4991bd07cbb047bfbbaf6fd1821dc3e06c63206f0aaad23c9a80054f1bab62`;
the source and installed native profiles both match SHA-256
`3b1350122396e2f953fb4f261ae5e26b24e8ea23545997b6a3983eae6b12405e`.
`gta-sa-launch --dry-run` is accepted. A production launch against the test-only
x86 OpenXR runtime reached the initial menu quad, observed the real RenderWare
tail, captured both gameplay eyes, completed the first gameplay frame, and
submitted stereo projection layers while `gta_sa.exe` remained responsive.

With the headset active, run:

```powershell
dotnet .\client\VrClient.Cli\bin\Release\net8.0\vrclient.dll gta-sa-launch --game-dir "W:\Grand Theft Auto San Andreas + Utilities\GTA San Andreas" --xr-runtime vd
```

In the headset, verify: stereo view and head pose; left-stick movement and
right-stick look; trigger/grip/face/menu input; on-foot and vehicle cameras;
matching left/right arm motion, near/crossed/extended poses, weapon attachment,
tracking-loss recovery; HUD, menus, cutscenes, save/load, resolution changes,
and device reset; then
confirm `GInputSA`, the widescreen fix, `wshps`, and the installed scripts still
load. After exit, inspect
`W:\Grand Theft Auto San Andreas + Utilities\GTA San Andreas\vrclient_gtasa.log`
for the current W: profile path, the pinned executable hash, `bridge active`,
and successful OpenXR frame submission without repeated initialization errors.

## External compatibility boundary

The research pass separates useful architecture evidence from drop-in
compatibility. The public `gta-reversed` source exposes the classic 1.0 US
RenderWare seams, but its README requires a different 5,189,632-byte compact
executable, so it is not a replacement for this user's 14,383,616-byte build.
The public Vice City VR project demonstrates a native reVC/librw/OpenXR route,
but that route requires a separately ported engine and does not make original
ASI/CLEO plugins automatically compatible. The Quest San Andreas source kit is
Android ARM64. `gtaRenderHook` is useful RenderWare/DX11/Vulkan hook research,
but it is not an OpenXR stereo bridge by itself. These projects informed the
seam selection; none is copied into or silently installed by VRClient.

## Implemented experimental bridge

`adapters/gta_sa/vrclient_gtasa_theater.cpp` is a separate x86 ASI target, built
with `tools/build-gtasa-bridge.ps1` under permanent MSVC `/W4 /WX` enforcement.
It verifies the pinned executable and native-profile identities, hooks the
imported D3D9 creation path in-process, and can run either a monoscopic theater
fallback or the stereo scene path. Stereo mode hooks the exact-build render
tail after simulation, applies OpenXR per-eye poses and native asymmetric frusta
to the RenderWare camera, replays the complete drawing tail once per eye, captures
the two full native backbuffers without cropping HUD edges, and submits an OpenXR projection backed by a D3D11
array swapchain. It never rewrites `gta_sa.exe` and does not replace or unload
the existing ASI/mod loader surface. Startup-movie `Present` calls remain plain
desktop swaps; the initial frontend menu may initialize OpenXR as a quad before
gameplay, and projection submission starts only after the verified RenderWare
tail runs. `VRCLIENT_GTASA_SEAMS=off` disables the stereo-tail diagnostic path
without changing installed mod files.

When `VRCLIENT_GTASA_INPUT=1` is present, OpenXR controller actions are merged
into GTA's native CPad state after the game's own input update, preserving
GInputSA and other mod input. In San Andreas's game-details panel, **VR turning**
is a Snap/Smooth switch. Snap offers 15, 30, 45, 60, or 90 degrees per push;
Smooth offers 45, 90, 135, or 180 degrees per second. This per-user choice is
saved at `%LOCALAPPDATA%\VRClient\gta-san-andreas-turn.json`
and applies to both VR launch buttons on the next launch. The CLI launch reads
the same preference. Desktop launch remains unaffected. The bridge accepts
`VRCLIENT_GTASA_TURN_MODE=snap|smooth`,
`VRCLIENT_GTASA_SNAP_TURN_DEGREES=15..90`, and
`VRCLIENT_GTASA_SMOOTH_TURN_DPS=30..180` for direct launches, defaulting to
Snap/30 degrees/90 degrees per second when absent or invalid. Snap moves the
selected angle for each right-stick
push; release the stick to center before the next turn. Smooth continuously
turns while the stick is held, with a dead zone and stalled-frame guard.
The bridge rotates one head-anchored rig basis shared by
both eyes and both controller targets, then turns CJ's heading once at the next
pad-update boundary. As GTA's camera follows CJ, the temporary rig yaw is
removed to prevent a second visual turn. Physical headset turning remains
independent. Both stick-turn modes are gated during menus, fades, cutscenes, vehicle use,
and lost input focus. Menus retain native A/confirm and B/back; in gameplay
A jumps. During on-foot stereo gameplay, the left stick is rotated into the
horizontal direction actually seen through the headset, including physical
head turns and any remaining snap-turn yaw. Looking up or down does not change
walking speed. Menu navigation and vehicle controls keep their native axes.
The bridge logs action synchronization, head-relative stick dispatch, and
committed body turns. Headset verification is still required for movement
alignment, comfort, and mod compatibility.

The game page has four launch choices: **VR Only** (ModLoader add-ons off),
**VR With Mods** (the ModLoader VRClientMods profile), **Desktop with installed
mods** (current installation, VR off), and **Vanilla** (VR off, from a separately
selected clean folder). These are game launches, not launches
inside GGMM or ModLoader. The optional **Mod setup** panel holds GGMM and
ModLoader setup actions. The VR With Mods button is disabled until ModLoader is
installed. GGMM edits and root-level mods in the VR game folder remain shared
between both VR choices and desktop play from the current folder; "VR Only" does
not remove them. Vanilla selection is
saved under `%LOCALAPPDATA%\VRClient\gta-san-andreas-vanilla.json`. AVRcade
requires a separate x86 game folder, rejects common mod and VR hooks, and
rechecks at launch. It cannot prove game assets have never been modified, so
users should select an independently installed, genuinely clean copy.

## Free/open-source distribution boundary

The arm implementation is original VRClient code and adds no paid or closed
SDK. It uses the standard OpenXR API already vendored under its existing
Apache-2.0 notices and Windows system graphics libraries. A distributable must
not contain `gta_sa.exe`, Rockstar/RenderWare assets or binaries, game archives,
keys, cracks, DRM bypasses, or multiplayer services; users supply a legitimate
supported game installation. VRClient's exact-build facts do not transfer any
rights in Rockstar content. The repository currently has no root `LICENSE`, so
public release must not be called open source until the owner selects an
OSI-approved license and completes legal review. Apache-2.0 is the recommended
project-license candidate because it is permissive and includes an explicit
patent grant; it is intentionally not applied here without owner selection.

Virtual Desktop may expose an `R8G8B8A8_UNORM` OpenXR swapchain through typeless
D3D11 backing textures. The bridge creates render-target views with the
negotiated typed swapchain format instead of copying the resource's typeless
format; this avoids `E_INVALIDARG` during headset startup. The fake-runtime
integration fixture reproduces that Virtual Desktop behavior.

The launch path requires an x86 `openxr_loader.dll` and the matching
`vrclient_gtasa_theater.json` native hook profile beside the ASI (or the
`VRCLIENT_GTASA_OPENXR_LOADER` / `VRCLIENT_GTASA_HOOK_CONFIG` environment
variables); the repository's existing
OpenXR import library is x64 and is intentionally not linked to this target.
When Virtual Desktop is selected, VRClient pins its sibling
`virtualdesktop-openxr-32.json` manifest for this PE32 game; the generic x64
manifest is not passed to the GTA process. When SteamVR is selected, VRClient
uses `steamxr_win32.json` when that manifest exists; if SteamVR only exposes
the x64 manifest, VRClient refuses to pass it to GTA and leaves runtime
selection to the 32-bit OpenXR registry entry instead.
For automatic selection, an unusable SteamVR x64-only manifest is skipped so
an available 32-bit Virtual Desktop manifest can still be selected.
Build that loader from the pinned Khronos source with
`tools/build-gtasa-openxr-loader.ps1`; this stages it under `build/gtasa-x86`
without modifying the W: install.

Build without touching the W: install:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\build-gtasa-bridge.ps1
```

The exact-build profile can also be checked read-only against the installed
executable, including the PE32 identity and every fixed hook signature:

```text
python tests/native/adapters/validate_gtasa_native_profile.py --game-exe "W:\Grand Theft Auto San Andreas + Utilities\GTA San Andreas\gta_sa.exe"
```

The resulting `.asi` is staged under `build/gtasa-x86`. It is not copied into
the game directory automatically, so the existing mod installation remains
recoverable and unchanged until the user explicitly chooses a managed install.

After reviewing the artifact, explicitly stage the bridge and its x86 OpenXR
loader:

```powershell
vrclient gta-sa-convert --game-dir "W:\Grand Theft Auto San Andreas + Utilities\GTA San Andreas" --install
vrclient gta-sa-launch --game-dir "W:\Grand Theft Auto San Andreas + Utilities\GTA San Andreas" --xr-runtime auto
```

The launch command starts only the exact pinned executable, passes the loader
path and stereo flag to the child process, and does not alter the existing ASI
files, INI files, scripts, or game executable. `--xr-runtime system` can be used
to leave the system OpenXR runtime selected; `steamvr` and `vd` request an
available named runtime explicitly. Advanced users can pass an explicit x86
runtime manifest with `--xr-runtime-manifest <path>`; this is useful for
diagnostic runtimes and custom OpenXR installations.

For HMD-less bridge-path diagnostics, VRClient also includes a test-only fake
OpenXR runtime. Build it with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\build-fake-openxr-runtime.ps1
```

The build produces `build/gtasa-fake-openxr-x86/fake-openxr.json` and an x86
loader probe. The probe verifies that the repository's x86 loader accepts the
runtime negotiation handshake; the fake runtime is not a headset emulator and
is never copied into the game directory. It is useful for distinguishing
loader/bridge startup regressions from the physical HMD requirement.

If a later bridge build needs to replace a bridge previously staged by VRClient,
the CLI requires an explicit old-hash match before updating it:

```powershell
vrclient gta-sa-convert --game-dir "W:\Grand Theft Auto San Andreas + Utilities\GTA San Andreas" --install --update-bridge --from-bridge-sha256 <existing-bridge-sha256>
```

The archived Team Vanilla RND package is a classic GTA SA modded install, not a current VR runtime or source bridge: [GTA San Andreas v1.3.0](https://github.com/TeamVanillaRND/GTA-San-Andreas-v1.3.0). The similarly named [GTA-SAN-ANDREAS-VR repository](https://github.com/lanouvelleecole/GTA-SAN-ANDREAS-VR) is a first-person/mod setup, not a stereo OpenXR conversion. UEVR support found for San Andreas targets the separate Definitive Edition, not this classic x86 executable: [6DoF motion controls VR for San Andreas](https://www.nexusmods.com/grandtheftautothetrilogy/mods/922). The [Quest source kit](https://github.com/dubrovskiy-yevhen-stakelogic/gta-sa-vr-quest) targets Android ARM64, also not this Windows executable.

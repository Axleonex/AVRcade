# Fallout 3 Free VR Architecture

## Product boundary

Fallout 3 is the only title implemented by this integration. The shared native renderer is title-neutral so another Gamebryo adapter can call it later, but no other Fallout title is configured or activated here.

VRClient exposes three unambiguous modes:

| Mode | Meaning | Current status |
|---|---|---|
| **Depth VR - Free** | ReShade reads the DirectX 9 depth buffer, user-supplied SuperDepth3D derives a stereo pair, and Osiris presents full-SBS through OpenXR. | Implemented and CI-testable; final depth selection, stereo and headset behavior require a real game/HMD. |
| **Native VR - Experimental** | VRClient's build-gated FOSE camera/render hook renders two independent Gamebryo views and submits them through the reusable OpenXR bridge. | The exact Steam/GOTY 1.7.0.3 Anniversary-patched executable is profiled and hardware-free tests pass; live game/HMD proof is still required. Automatic fallback is Depth VR. |
| **Native VR - Verified** | Same native path after exact-build, independent-eye and headset proof are all accepted. | Not currently available. |
| **Desktop** | Normal non-VR Fallout 3 profile. | Preserved separately. |

Depth VR is depth-derived stereoscopy, not engine-rendered dual-camera VR. Head rotation can be translated to mouse/gamepad input by the viewer. Positional behavior may be limited or simulated. Controller input does not mean the weapon is tracked independently from the head.

## Verified free stack and licensing decision

The initial data path is:

```text
Fallout3.exe (Direct3D 9)
  -> official 32-bit ReShade depth/color access
  -> user-supplied SuperDepth3D full-SBS output
  -> Osiris VR Viewer desktop/SBS capture
  -> standard OpenXR runtime
  -> SteamVR or VirtualDesktopXR
```

ReShade documents D3D9 support and color/depth access and is BSD-3-Clause: <https://github.com/crosire/reshade>. SuperDepth3D documents side-by-side output and a DX9 path, but its own source header and project licensing text make it proprietary, personal-use-only, and non-redistributable: <https://github.com/BlueSkyDefender/Depth3D>. VRClient therefore never bundles, mirrors, copies, or silently downloads it. Osiris is MIT licensed, accepts full/half SBS/TAB, uses OpenXR, documents SteamVR, and describes wireless Virtual Desktop use: <https://github.com/BerZerker96/Osiris-Vr-Viewer>. VirtualDesktopXR is MIT licensed and implements the standard OpenXR route without SteamVR; its own wiki also documents the SteamVR route: <https://github.com/mbucchia/VirtualDesktop-OpenXR/wiki>.

No named component is assumed to work merely because the APIs line up. Launch readiness requires the user-supplied files, a selected OpenXR manifest, and a live in-game depth/stereo calibration. Missing depth produces a blocked check rather than a false success.

## Managed conversion

`Fallout3Mo2ProfileService` creates `VRClient Fallout 3 VR` under an explicitly selected MO2 instance. It can clone `modlist.txt`, `plugins.txt`, load order, archives and INI files using copy-if-missing semantics. It never changes the source profile or the shared `mods` directory.

The workflow is:

1. discover Steam GOTY AppID 22370 (preferred), legacy AppID 22300, GOG, or a manually selected directory across Steam's configured library drives;
2. identify the executable and verify all five GOTY DLC master files;
3. validate MO2, FOSE/build state, ReShade, SuperDepth3D, Osiris, OpenXR and native proof state;
4. emit a dry-run plan with the active backend label;
5. require `--acknowledge` before writes;
6. create/clone only the isolated profile;
7. write an atomic ownership marker, stereo preset and setup guidance;
8. leave an interruption journal until all idempotent operations finish;
9. generate a mod compatibility report without disabling or reordering anything;
10. launch visible helpers and MO2;
11. keep structured per-process logs;
12. restore only managed files, or uninstall the whole profile only when every cloned file still matches its creation hash and no unknown file exists.

Changed managed files receive content-addressed backups in `.vrclient-backups`. A failed or interrupted conversion leaves its journal for `repair`. Dirty profiles are preserved and uninstall refuses with the exact unexpected/changed file.

## Compatibility classification

The analyzer preserves the source list and classifies entries as verified compatible, likely compatible, requires VR-specific configuration, conflicts with Depth VR, conflicts with Native VR, incompatible, or unknown. Rules cover FOSE DLL/build assumptions, camera/animation/weapon/skeleton/body changes, HUD/Pip-Boy/menu replacements, graphics injectors, depth behavior, weather/lighting/shadows/water, texture/LOD cost, input mods and alternate launchers. Unknown remains unknown; there is no universal-compatibility claim.

## Native Gamebryo renderer

Reusable code lives under `src/native/gamebryo_dx9_openxr/`. Fallout 3 policy and activation live under `adapters/fallout_3/`.

The shared bridge:

- owns a deterministic cold/ready/frame/device-lost/runtime-lost/stopped lifecycle;
- receives OpenXR predicted poses and asymmetric per-eye FOV;
- requires the title adapter to render and submit each eye separately;
- requires distinct view/projection signatures and rejects a duplicated desktop frame as native-stereo evidence;
- uses the existing x86 D3D9-to-D3D11 OpenXR path to copy each eye into a two-layer array swapchain;
- releases runtime resources on D3D9 device loss and reinitializes after reset;
- exposes counters, rejection reasons and a visible log callback;
- supports seated/local origin now and records standing intent for calibration.

The first safe interop is bounded CPU readback. It is deliberately slower than a future D3D9Ex shared-handle path, but it is easy to validate and avoids claiming unsupported direct D3D9 OpenXR binding. Frame pacing remains driven by `xrWaitFrame`/`xrBeginFrame` in the shared runtime. GPU shared-resource optimization is deferred until real stereo is proven.

`vrclient_fallout3_native.dll` is itself a FOSE plugin and also exports a narrow title-adapter ABI. Its implemented title hook:

- reads `fallout3-native-profile.ini` beside the DLL;
- validates the profile ABI, executable SHA-256, image-bounded RVAs, scale, offsets, and 5-16 reviewed render-entry bytes;
- installs an ASLR-safe MinHook trampoline only after every gate passes;
- ignores screenshot and other render-to-texture calls unless their first argument equals the exact main render context selected by the build profile;
- resolves the active `NiCamera` and D3D9 device through profile-bounded pointer paths;
- samples OpenXR poses, applies independent asymmetric projections, renders both eyes, verifies the engine did not replace the installed camera, and submits the matching surfaces;
- restores the complete camera view state after every attempt, supports F8 recenter, handles D3D9 loss/reset, and renders a fresh desktop frame before disabling stereo after a partial failure.

The published FOSE source corroborates the 0x114-byte Fallout 3 `NiCamera` layout used by the adapter. Ghidra 12.0.4 analysis of the exact local Steam/GOTY 1.7.0.3 executable established the `__thiscall` world-render dispatcher at `0x006ECBA0` (three stack arguments and `ret 0x0C`), main render context pointer at `0x0107A3C0`, camera owner at `0x0107A224` with its `NiCamera*` at `+0xAC`, `NiRenderer*` at `0x0108F048`, and D3D9 device at renderer `+0x288`. `fallout3-native-profile-steam-1.7.0.3.ini` records those RVAs, the executable SHA-256, and 16 entry bytes. The hash and instruction gate make this profile fail closed on every other executable; the non-activating example remains the template for future reviewed builds.

## Runtime and input diagnostics

Runtime selection uses `XR_RUNTIME_JSON` for only the processes VRClient starts. SteamVR and VirtualDesktopXR are detected through normal OpenXR manifests and process state; there is no custom Virtual Desktop protocol. Diagnostics distinguish inactive runtime, asleep/disconnected headset, missing x86 runtime path, OpenXR initialization failure, missing depth, missing stereo source, head-output failure, D3D9 loss/reset and native fallback.

Comfort configuration remains conservative: seated/standing choice, recenter, dominant-hand/controller guidance, head-to-camera sensitivity, viewer-supported snap/smooth turn and vignette options, HUD scale, FOV/convergence calibration, and safe disabling of head bob/blur/forced camera motion. No tracked weapon claim exists until controller pose demonstrably drives the in-game weapon independently of the head.

## Native work still requiring a headset

1. Enter a live 3D scene with the headset connected and confirm the profiled dispatcher and implemented camera transform/asymmetric projection injection.
2. Prove color/depth/HUD behavior across world, dialogue, Pip-Boy, VATS, terminals and menus.
3. Measure frame pacing; replace CPU readback with shared resources only if evidence justifies it.
4. Exercise device loss, OpenXR runtime restart, recenter and seated/standing origins on hardware.
5. Produce the proof containing `exact_build_match`, `independent_per_eye`, `headset_verified`, and the exact tested `Fallout3.exe` `executable_sha256` before changing the label to **Native VR - Verified**. A proof for any other executable hash is rejected and falls back to Depth VR.

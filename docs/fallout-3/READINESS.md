# Fallout 3 VR Readiness

**Overall verdict:** Fallout 3 Native VR is not currently playable. VRClient discovers the installed Steam/GOTY copy across Steam library drives, validates the exact executable, and stages the reviewed FOSE/OpenXR adapter, but the first observed in-game stereo frame failed closed and restored desktop rendering. The native adapter does not yet map OpenXR controllers to Fallout 3 input. Neither Native VR nor the separate Depth VR route has a completed end-to-end headset verification. A native hook failure falls back to desktop rendering in that game process, not automatically to Depth VR.

## Backend status

- **Depth VR - Free:** discovery, dependency/license validation, dry-run/apply/repair/restore/uninstall, isolated MO2 profile handling, compatibility reporting, and launch orchestration are implemented. Final ReShade depth selection, SuperDepth3D stereo tuning, and Osiris/OpenXR presentation require the headset session.
- **Native VR - Experimental (currently failing):** the reusable Gamebryo D3D9-to-OpenXR bridge, exact-build FOSE hook, per-eye camera transforms and render replay, device-loss handling, recentering, fail-closed desktop restoration, and x86 loader are implemented. In the 2026-09-29 headset run, OpenXR initialized and a reference pose was captured, but the first stereo frame failed closed before any independent frame was submitted. The reviewed profile targets only Steam/GOTY 1.7.0.3 SHA-256 `03CF5ADA02FCF789FCF4639E05127D545FF3F69363B926709B8F0C3F8AD29107`. OpenXR controller-to-game input is not implemented.
- **Native VR - Verified:** intentionally unavailable until a headset run proves distinct left/right view-projection matrices and corresponding OpenXR eye submissions.
- **Desktop:** AVRcade's direct desktop action starts FOSE with `VRCLIENT_FALLOUT3_NATIVE_VR=0`, preserving compatible FOSE and Vortex-deployed mods while leaving the native VR hooks inactive. The FOSE plugin also defaults to inactive for Vortex's own Play action unless that action explicitly sets the opt-in environment variable.

## Real-machine evidence (2026-09-21)

- Steam discovery found `W:\SteamLibrary\steamapps\common\Fallout 3 goty`; discovery is driven by Steam registry/library manifests rather than a fixed drive letter.
- The installed executable was safely patched from 1.7.0.4 to the FOSE-supported 1.7.0.3 build. `Fallout3_backup.exe` preserves the previous executable.
- FOSE 1.2 beta 2 and the project-built Khronos OpenXR loader 1.1.43 x86 were staged without overwriting unrelated files.
- Ghidra analysis of the exact executable established the render dispatcher RVA `0x002ECBA0`, main render-context RVA `0x00C7A3C0`, camera-owner RVA `0x00C7A224`, renderer RVA `0x00C8F048`, camera offset `0xAC`, D3D9 device offset `0x288`, `__thiscall` ABI, and the 16-byte entry signature recorded in the reviewed profile.
- A ProcDump full dump identified the initial startup failure as a 64 KiB stack allocation in the plugin's `DllMain`, not recursive render-hook execution. Module-path work now occurs after loader-lock entry using heap-backed buffers; `DllMain` only records the module and disables thread callbacks.
- After that repair, the exact-build hook installed and Fallout 3 remained responsive at the real main menu for more than one minute with no new Windows Application Error event. The main menu has no active world camera, so headset/per-eye execution remains a human in-game gate.

## Automated evidence

- `dotnet build client\VrClient.Fallout3.Cli\VrClient.Fallout3.Cli.csproj -c Release` — passed with 0 warnings and 0 errors.
- Full `VrClient.Core.Tests` suite — 243/243 passed.
- `python tests\native\adapters\validate_fallout_3_integration.py` — 7/7 passed.
- MSVC x86 CMake build produced `vrclient_fallout3_native.dll`, the shared Gamebryo bridge, and camera-stereo tests.
- `ctest --test-dir build\fallout3-hook-x86 -C Release --output-on-failure` — 2/2 passed.
- Live CLI discovery reported the W: Steam installation, supported 1.7.0.3 executable, all five DLC, and no discovery diagnostics.

## Remaining human gates

- Connect and wear the headset, enter a real 3D cell, and capture left/right view-projection signatures and OpenXR submissions.
- Validate eye order, asymmetric projections, IPD/world scale, yaw/pitch/roll and positional tracking, F8 recenter, Pip-Boy and menus, VATS, combat, save/load, representative mods, device loss/runtime restart, and frame pacing.
- Validate the free Depth VR route separately with its actual D3D9 depth buffer and full-SBS viewer path.
- Do not promote Native VR to **Verified** unless all proof fields described in `HUMAN-VERIFICATION.md` pass.

## Known limitations

- On 2026-09-29, AVRcade's Native VR action launched Fallout 3 and the desktop mouse worked, but the game did not appear in the headset and VR controllers could not select anything. The installed `fallout3-native.log` showed `OpenXR ready`, `OpenXR reference pose captured`, and immediately `Fallout 3 stereo hook failed closed; desktop rendering restored`, with no `fallout3_independent_stereo_frames` entry. A diagnostics-only adapter build now logs the exact failed eye/stage; its first launch reached only hook installation, so another run inside a 3D scene is needed to identify the failing condition.
- AVRcade has a Windows-only, experimental in-app Vortex window host for Fallout 3. It detects registered installs across drives and remembers a manually selected portable executable. On 2026-09-28, Vortex at `W:\Vortex\Vortex.exe` rendered inside an isolated AVRcade build, its Profiles page was interactive, and closing the panel returned Vortex to its own window. The original Per-Monitor V2 AVRcade build could not dock the Per-Monitor V1 Vortex window; the app manifest now uses the matching V1 mode. Cross-process Electron window hosting remains experimental and needs more popup, resize, multi-monitor, and version testing. Vortex owns mod deployment and profiles; AVRcade does not modify them.
- On 2026-09-28, Vortex's Fallout 3 staging folder was moved through its own Preferences UI from `C:` to `W:\Vortex Mods\fallout3`, the same NTFS volume as the Steam game. Vortex then offered Hardlink Deployment instead of "No deployment method available." The Fallout 3 mod list still showed no installed mods, so neither a deployed mod nor a modded VR launch has been demonstrated. Install and deploy a representative mod, then verify it loads through AVRcade's native FOSE launch before claiming modded VR works.
- On 2026-09-28, AVRcade added three Fallout 3 choices: Native VR, VR with Vortex mods, and Desktop with deployed mods (VR off). Both VR choices use the same FOSE executable and installed game files; the Vortex choice checks Vortex is installed but cannot prove which profile was deployed. The desktop choice and ordinary Vortex Play leave VR hooks inactive. The installed app and x86 FOSE DLL were updated after 303/303 managed tests, 7/7 static Fallout 3 tests, and 2/2 native tests passed. The three controls were observed in the installed UI; no modded in-game or headset run was performed.
- The real render dispatcher cannot exercise independent eye replay at the static main menu; an in-game headset run is required.
- The bridge's CPU readback/upload path favors correctness and may need performance work after measured headset evidence.
- Depth-derived stereo is not genuine engine-rendered dual-camera VR and does not imply positional tracking or tracked weapons.
- Mod compatibility is classified conservatively; unknown or conflicting FOSE, camera, skeleton, HUD, post-processing, and input mods require per-profile testing.
- Third-party restricted binaries are never redistributed; VRClient records authoritative acquisition guidance and hashes instead.

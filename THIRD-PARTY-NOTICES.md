# Third-party notices

AVRcade uses the components below. Each remains under its own license. The full license text of every bundled component is in the `licenses/` folder, which is installed with the app; `licenses/README.txt` maps each component to its file.

## Bundled in the installer

| Component | Used for | License |
|---|---|---|
| .NET runtime | Runs the app and the `vrclient` tool | Microsoft .NET Library license for the runtime files (the source is MIT) |
| Avalonia 11 | Desktop UI | MIT |
| CommunityToolkit.Mvvm | View-model helpers | MIT |
| SkiaSharp, HarfBuzzSharp | Rendering and text shaping (through Avalonia) | MIT; Skia and HarfBuzz under their own terms in the SkiaSharp notices |
| ANGLE | Graphics translation (through Avalonia) | BSD-3-Clause |
| MicroCom.Runtime, Tmds.DBus.Protocol | Platform plumbing (through Avalonia) | MIT |
| Inter typeface | UI font | SIL Open Font License 1.1 |
| OpenXR loader (Khronos), with jsoncpp | Loaded by the native VR bridges | Apache-2.0; jsoncpp MIT / public domain |
| MinHook | Function hooking in the native VR bridges | BSD-2-Clause |
| Microsoft Visual C++ runtime | Runs the native helpers | Microsoft redistributable terms |
| RED4ext.SDK, Dear ImGui, im3d | Compiled into the Cyberpunk 2077 VR backend | MIT |

## Downloaded on your PC when you press a setup button

AVRcade does not redistribute these. It downloads the pinned version from the author's own release location and checks its hash.

| Component | Author | Used for |
|---|---|---|
| BepInEx | BepInEx team | Mod loader for the Unity games |
| RepoXR, LCVR, CWVR | DaXcess | VR for R.E.P.O., Lethal Company and Content Warning |
| PeakVR | Andrey04o | VR for PEAK |
| Big Walk VR | CircuitLord | VR for Big Walk |
| FixPluginTypesSerialization and other listed dependencies | Their authors | Required by the mods above |
| RED4ext | WopsS | Plugin loader for the Cyberpunk 2077 VR backend |
| Cyber Engine Tweaks | maximegmd and contributors | Scripting for the Cyberpunk 2077 VR backend |
| Codeware, ArchiveXL, TweakXL | psiberx | Frameworks the Cyberpunk 2077 VR backend needs |
| redscript | jac3km4 | Script compiler the Cyberpunk 2077 VR backend needs |
| CyberpunkVR Port 0.1.7 | dariulone | Cyberpunk 2077 VR conversion, downloaded directly from the official release |
| UEVR | praydog | VR injection for Unreal Engine games |
| UEVR profile for RV There Yet? | d_rey86 | Game-specific VR settings |
| .NET Desktop Runtime | Microsoft | Runs the UEVR injector |

The pinned versions and hashes are in `config/modpacks/*.lock.json`, `config/unreal/` and `config/redengine/native/cyberpunk-2077-backend.json`.

## Built on, not bundled

| Component | Relationship |
|---|---|
| r2modman, Thunderstore Mod Manager, Vortex, GGMM, SAMI | Mod managers AVRcade can open; installed by the player |

## Artwork and names

The controller diagrams in `client/VrClient.App/Assets/Controllers/` are original drawings made for AVRcade. "Meta Quest", "Valve Index", game titles and other product names are trademarks of their owners and are used only to identify compatible hardware and games. Game cover art is read from the player's local Steam cache at run time and is not part of this repository.

## CyberpunkVR Port attribution

CyberpunkVR Port 0.1.7 is created by **dariulone**. The VR renderer, avatar, motion controls and game-specific conversion belong to that upstream project. AVRcade provides package verification and launch management.

- Original mod: https://www.nexusmods.com/cyberpunk2077/mods/30093
- Official release: https://github.com/dariulone/cyberpunk-vr-port/releases/tag/0.1.7
- Source at a0c23fe110530bb74a73b3adf1ffa7d83050c43d; MIT copyright notice retained in `licenses/cyberpunk-vr-port-LICENSE.txt`.
- More Occluders by sosuine is a separate required download: https://www.nexusmods.com/cyberpunk2077/mods/33136. Select Scripted v0.3.

The official archive is downloaded on the user's PC and is not redistributed as an AVRcade-authored conversion. Historical AVRcade 0.1.2 patches remain reference material and are not applied to 0.1.7.

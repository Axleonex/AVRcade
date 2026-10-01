# Fallout 3 VR Credits and License Decisions

Machine-readable attribution is in [`../../config/modpacks/fallout-3.dependencies.json`](../../config/modpacks/fallout-3.dependencies.json). Retrieval date: 2026-09-21.

| Component | Credit | Version/evidence | License / redistribution | VRClient behavior |
|---|---|---|---|---|
| Fallout 3 | Bethesda Game Studios / Bethesda Softworks | Steam GOTY AppID 22370 or legacy AppID 22300; user-owned files only | Commercial game assets | Never downloaded, copied or redistributed. |
| FOSE | Ian Patterson, Stephen Abel, Paul Connelly and contributors | Stable 1.2 beta 2; official site lists 1.7.0.3 and GOG support | Project-specific terms | Official link only; user supplies files. <https://fose.silverlock.org/> |
| Fallout Anniversary Patcher | c6 and contributors | 1.1; official Nexus page describes 1.7.0.4 to 1.7.0.3 behavior | Whole-package asset reuse requires author permission | Guidance only, never bundled or silently run. <https://www.nexusmods.com/fallout3/mods/24913> |
| Mod Organizer 2 | ModOrganizer2 contributors | 2.5.2 | GPL-3.0 | User selects an existing install or official release; not bundled. <https://github.com/ModOrganizer2/modorganizer> |
| OpenXR | Khronos Group | OpenXR-SDK 1.1.43 x86 loader used for local validation | Apache-2.0 | VRClient builds the loader from the official tagged source; attribution retained. <https://github.com/KhronosGroup/OpenXR-SDK> |
| ReShade | crosire and contributors | 6.8.0; official project documents D3D9 and depth/color access | BSD-3-Clause | Official user download only; no binary bundled. <https://github.com/crosire/reshade> |
| SuperDepth3D / Depth3D | BlueSkyDefender (Jose Negrete) and credited shader contributors | SuperDepth3D 5.4.0 | Proprietary; free personal use; redistribution prohibited without permission | User-supplied only. VRClient does not package, mirror, download or copy it. <https://github.com/BlueSkyDefender/Depth3D> |
| Osiris VR Viewer | BerZerker96 and VRScreenCap contributors | 1.57 | MIT | Official-release link only; not bundled. <https://github.com/BerZerker96/Osiris-Vr-Viewer/releases/tag/v1.57> |
| SteamVR | Valve | User-installed current runtime | Valve platform terms | Detected/launched visibly through its standard OpenXR runtime; no Steam credentials handled. |
| VirtualDesktopXR | Matthieu Bucchianeri, Guy Godin and contributors | Provided with current Virtual Desktop Streamer | MIT for VDXR source; Virtual Desktop application has its own terms | Uses only the standard OpenXR runtime manifest. <https://github.com/mbucchia/VirtualDesktop-OpenXR> |

VRClient-owned source in `src/native/gamebryo_dx9_openxr` and `adapters/fallout_3` follows the repository license. The adapter contains no Bethesda assets, third-party shader or binary, DRM code, storefront credentials, or paid-service automation. Its reviewed profile contains only independently derived RVAs and a bounded 16-byte executable signature needed to reject unsupported builds.

The native adapter links the repository-pinned [MinHook](https://github.com/TsudaKageyu/minhook) source under its BSD 2-Clause license. VRClient builds it from source; no separately downloaded binary is required.

Hashes remain `null` when the dependency is user-selected, an upstream page is mutable, or the repository does not distribute the artifact. VRClient should record the actual local SHA-256 during a user-authorized installation session rather than publish a guessed digest.

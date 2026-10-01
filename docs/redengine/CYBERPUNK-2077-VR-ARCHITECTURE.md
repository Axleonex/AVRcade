# Cyberpunk 2077 VR for VRClient

Research status: 2026-08-05

## Correction and scope

Cyberpunk 2077 is not a Unity game. It runs on CD PROJEKT RED's REDengine 4.
It therefore cannot use VRClient's `unity-vr/` BepInEx/Unity XR path or its
Unreal/UEVR path.

VRClient should treat Unity, Unreal, and REDengine as separate engine adapters
around one shared VR product:

```text
VRClient app, safety, launch, packages, diagnostics
                       |
          shared OpenXR + D3D12 runtime
                       |
       +---------------+----------------+
       |               |                |
 Unity adapter    Unreal adapter   REDengine adapter
 BepInEx/XR       UEVR/native       RED4ext + CP2077
```

The reusable layer is headset timing, poses, OpenXR session and swapchains,
D3D12 device/queue handling, input actions, comfort settings, HUD policy,
diagnostics, build identity, and install/launch orchestration. Camera discovery,
view construction, rendering hooks, animation, weapon behavior, and UI capture
remain engine- and game-specific.

## Current external evidence

CD PROJEKT RED officially supports Cyberpunk mod installation and creation via
REDmod. REDmod handles scripts, tweaks, assets, deployment, and the `-modded`
launch mode; it is not itself a native rendering-plugin SDK.

RED4ext is the practical native integration seam. It loads version-aware native
plugins and its SDK exposes reversed REDengine 4 types plus access to the game's
scripting VM. Cyber Engine Tweaks (CET), redscript, Codeware, ArchiveXL, and
TweakXL cover scripting, UI/gameplay behavior, and authored assets.

The strongest current technical reference is the MIT-licensed
`dariulone/cyberpunk-vr-port`. Its 0.1.x architecture is important because it
documents two approaches and their result:

1. The early approach used a `dxgi.dll` proxy, mono/AER presentation, and later
   optical-flow/depth reprojection. This produced compatibility conflicts and
   AER artifacts and made multiple swapchains hard to distinguish.
2. Version 0.1.0 moved to RED4ext plugins and removed the DXGI proxy and AER.
   It creates an authored `entRenderToTextureCameraComponent` on the player,
   uses that component as a genuine second REDengine view with its own position
   and projection, captures both views, and submits them to OpenXR.

The second path is the architecture to pursue. Generic post-process stereo,
depth warping, and alternate-eye rendering remain useful only as diagnostic or
fallback modes; they should not define the product.

The reference port also demonstrates that a complete conversion spans more
than stereo rendering: finite-depth HUD composition, FOV/frustum correction,
LOD/culling correction, controller-to-XInput mapping, decoupled weapon aim,
VRIK, motion melee, holsters, menus, map handling, vehicles, and cutscene
policy. Its current known issues include mismatched foliage/shadows, occasional
mono fallback or missing second-eye HUD, runtime-specific projection problems,
vehicle shooting, zoomed sights, cutscenes, and settings that desynchronize the
two views. These are useful acceptance-test targets for VRClient.

## Licensing and distribution boundary

The GitHub repository is MIT licensed, so its software may be studied, modified,
and reused with the required copyright and license notice. Its Nexus page lists
more restrictive asset redistribution permissions. Until those are reconciled,
VRClient must not redistribute the other project's packaged game assets or
release archive merely because the source code is MIT licensed.

Preferred policy:

- Reuse or adapt MIT source only with attribution and a preserved license.
- Generate VRClient-owned patch data from legally installed user game files
  where feasible.
- Do not ship CD PROJEKT RED assets.
- Keep the Cyberpunk integration free and review CD PROJEKT RED's current Fan
  Content Guidelines and REDmod EULA before public distribution.
- Detect, explain, and refuse conflicting `dxgi.dll` VR proxies or duplicate
  RED4ext plugin copies.

## Proposed VRClient architecture

```text
VRClient app / CLI
  -> exact Cyberpunk executable + build fingerprint
  -> dependency and conflict preflight
  -> install/enable VRClient REDengine package
  -> launch Cyberpunk through its supported launcher path

Inside Cyberpunk2077.exe
  RED4ext
    -> vrclient_redengine_bridge.dll
       -> REDengine build resolver and fail-closed hook table
       -> D3D12 device / direct queue / Present acquisition
       -> REDengine camera, view, HUD, input, and game-state services
       -> existing VRClient OpenXR runtime
       -> cyberpunk_2077 game adapter
       -> optional CET/redscript/ArchiveXL support package
```

### REDengine bridge

The bridge is loaded by RED4ext, not by a generic `dxgi.dll` proxy. It owns only
REDengine-wide integration:

- supported-build resolution and hook validation;
- D3D12 device, direct queue, swapchain, and resize observation;
- OpenXR lifecycle and predicted-pose timing through `vr_runtime_api`;
- discovery and identity of REDengine render views;
- capture/submission contracts for main and auxiliary views;
- fail-closed services exposed to a game adapter.

It must not contain Cyberpunk HUD element names, weapon rules, holster slots,
camera-component asset names, or quest/cutscene policy.

### Cyberpunk game adapter

`adapters/cyberpunk_2077/` owns the game-specific behavior:

- selecting and enabling the authored VR camera component;
- mapping OpenXR eye poses and asymmetric FOV to REDengine transforms and
  projections;
- recognizing the main and VR camera views by explicit identity;
- HUD capture, finite-depth placement, menus, map, scanner, and subtitles;
- HMD/controller-relative locomotion and XInput/game-action mapping;
- weapon muzzle aiming, sights, recoil, melee, IK, and body visibility;
- vehicle, dialogue, braindance, photo-mode, scripted camera, and cutscene
  comfort policies;
- game-version compatibility data and evidence logs.

### Shared API additions

Do not rename or generalize the existing Unreal service IDs. Add sibling
services, initially private until their contracts are proven:

- `VRCLIENT_ADAPTER_SERVICE_REDENGINE_GRAPHICS`
- `VRCLIENT_ADAPTER_SERVICE_REDENGINE_VIEW`
- `VRCLIENT_ADAPTER_SERVICE_REDENGINE_GAME_STATE`

The existing shared input, comfort, HUD, diagnostics, pose timing, and config
services should be reused unchanged wherever their semantics already fit.

## Proof ladder

Each rung needs build-stamped evidence. A later rung cannot substitute for an
earlier one.

1. **Research and legal gate** — pin Cyberpunk, RED4ext SDK, and dependency
   versions; record licenses and distribution rules.
2. **No-op plugin** — RED4ext loads a VRClient plugin on the exact supported
   Cyberpunk build, logs lifecycle and build identity, then exits cleanly with
   no rendering changes.
3. **Graphics observation** — acquire and validate the live D3D12 device,
   direct queue, game swapchain, Present/ResizeBuffers path, and window identity.
4. **OpenXR diagnostic** — using the borrowed game device/queue, submit bounded
   left/right diagnostic colors from inside the game process.
5. **Tracked mono** — submit the live scene to both eyes and prove stable 6-DoF
   pose timing. This is transport evidence, not stereo completion.
6. **Real stereo** — create/enable an engine-owned second view, apply distinct
   eye transforms and asymmetric projections, and prove correct parallax,
   culling, shadows, depth, and frame-pair identity. No AER acceptance credit.
7. **Playable seated baseline** — gamepad play, aiming, interaction, HUD,
   inventory, map, dialogue, scanner, vehicles, saves, and recentering work.
8. **Motion-control baseline** — controller aim, reload/interact mapping, melee,
   body/hand IK, holsters, dominant hand, and seated/standing calibration work.
9. **Comfort and scenario matrix** — snap/smooth turn, vignette, camera-shake
   suppression, vehicle horizon, cutscenes, braindance, death, loading, scripted
   camera changes, and photo mode have explicit policies.
10. **Release gate** — clean install/uninstall, conflict detection, dependency
    hashes, version refusal, crash-safe logging, multi-runtime headset tests,
    performance budgets, and public distribution review pass.

## First implementation slice

The first slice should stop at rung 4. It is intentionally small enough to
prove the loading and graphics architecture before importing gameplay mods or
authoring VR camera assets.

Deliverables:

1. `config/redengine/cyberpunk-2077.json` with exact executable/build identity,
   dependency versions, launch mode, and conflict rules.
2. `src/native/adapters/redengine/` with a RED4ext bridge skeleton and a
   supported-build resolver.
3. `adapters/cyberpunk_2077/` with metadata and no-op lifecycle behavior.
4. A REDengine service header separate from `unreal_services.h`.
5. A preflight command that reports Cyberpunk build, RED4ext state, renderer,
   overlays/proxies, and OpenXR runtime before launch.
6. Controlled no-op, D3D12 observation, and red/blue headset evidence artifacts.

Only after those pass should the project choose whether to adapt the reference
port's MIT camera/view implementation or independently reproduce the same
REDengine view path.

## Foundation implementation status

Implemented on 2026-08-03:

- `config/redengine/native/cyberpunk-2077.json` and its versioned schema define
  the RED4ext loading route, D3D12 backend, real second-view stereo requirement,
  dependencies, hook conflicts, capability evidence states, and non-commercial
  distribution boundary.
- `tools/cyberpunk_native_preflight.py` performs a read-only installation check,
  captures the actual executable SHA-256 and Steam build ID, requires RED4ext,
  detects conflicting render/VR hook owners, and refuses any unpinned or changed
  build.
- `redengine_services.h` defines separate graphics, normalized view, and game
  state contracts without changing the Unreal services.
- `vrclient_cyberpunk_2077_adapter` compiles and loads through the existing
  plugin host but refuses activation while its build is unpinned or the three
  REDengine services are unavailable.
- The initial shared input/comfort/HUD profile is intentionally template data.
  It cannot satisfy a live UI or gameplay acceptance gate until observed in the
  supported game build.

Advanced on 2026-08-04:

- The X: Steam installation was pinned to build `20383525`, executable SHA-256
  `a7de82945c03e041fc7339fcf9066224d98db2f5d80fea50f7947bb350a60991`,
  and REDengine product/file version 2.31 / 3.0.80.51928. RED4ext exposes the
  compact CDPR product version as SemVer fields `2.3.1`; this is intentionally
  different from parsing the display string as fields `2.31.0`.
- Official RED4ext v1.30.0 was checksum-verified and installed additively. Its
  `winmm.dll` is the RED4ext loader itself; `proxy_dll_forbidden` refers to
  competing VR/render proxies such as DXGI or D3D12 owners, not this pinned
  dependency.
- `VRClient.REDengine.dll` implements the RED4ext API v1 `Supports`, `Query`,
  and `Main` exports, refuses non-2.31 runtime products, and installs a
  pass-through D3D12 observer. The observer does not change swapchain
  descriptors, window state, resolution, resources, or command submission.
- Controlled DLL tests and the 2026-08-03 live run pass. RED4ext v1.30.0 loaded
  exactly one VRClient bridge, the bridge logged its pinned build, and both
  plugin and loader unloaded cleanly. This closes proof-ladder rung 2.
- The bridge build uses RED4ext.SDK tag 1.0.0 at commit
  `a4a781088a92a8efa890d94fde4efd8985d497c7`, the SDK revision selected by
  RED4ext v1.30.0. The dependency remains under the gitignored `external/`
  tree and is not redistributed from this repository.
- A controlled WARP test proved exact pass-through behavior for a direct D3D12
  queue, HWND swapchain, `Present`, `ResizeBuffers`, observation query, and
  complete detour removal before DLL unload.
- A 48-minute live Cyberpunk run then captured the real game renderer at
  1920x1080, DXGI format 28, three buffers, 320419 presents, and two resizes.
  The user reported normal behavior and RED4ext cleanly detached and unloaded
  the only plugin. This closes proof-ladder rung 3; the hashed logs are under
  `artifacts/cyberpunk-red4ext/20260804-d3d12-live/`.

### Engine-native second-view decision

The next stereo seam is REDengine's own
`entRenderToTextureCameraComponent`, not D3D12 command replay and not an
Unreal-style scene-view extension. The MIT reference implementation calls this
auxiliary player view VRCAM. An authored, initially-disabled VRCAM component:

- owns a distinct camera identity and render-to-texture target;
- causes REDengine to build and execute a second frame graph;
- can receive a separate eye transform and projection;
- stops cleanly during menus/loading, allowing a deliberate mono fallback.

The first VRCAM proof used an isolated, manifest-backed dependency slice and a
VRClient-owned D3D12 observer. Live testing proved that resolution/format
histograms cannot identify REDengine's final VRCAM color: likely candidates
were either black or pre-final surfaces, and submitting them produced
green/black halves, pink frames, or pixel blocks in the headset.

The active backend therefore uses the MIT reference implementation's
Cyberpunk-specific seam. It identifies the VRCAM by its REDengine view-context
`CName`, redirects the final `RenderFinal2D` output in its native
`R11G11B10_FLOAT` format, records the copy on the engine command list, converts
linear HDR for OpenXR submission, and falls back to mono when the auxiliary
view is stale during menus or loading. VRClient owns discovery, version and
dependency checks, runtime selection, installation records, launch, rollback,
and validation; the pinned MIT plugin owns the reverse-engineered in-process
render seam.

Pinned local backend on 2026-08-05:

- CyberpunkVR Port source `f5e59d7e81a35ccf71da1e75b34483335e908174`.
- RED4ext SDK `d02a1f329744cd4e39c6f7f3fdf63053731e1aff`.
- MinHook `d94c64d32ea37bc4f5ee47d580709f70c6fb6080`.
- VRClient HMD-readiness patch
  `patches/cyberpunk-vr-port/f5e59d7-openxr-hmd-readiness.patch`.
- VRClient controller-navigation patch
  `patches/cyberpunk-vr-port/f5e59d7-controller-navigation.patch`.
- Patched `CyberpunkVR_Stereo.dll` SHA-256
  `BFDE122D65D6878A611E4FB8EF823AD8ABE545325FBC22AA0747F1DB004EC51F`.
- ArchiveXL 1.27.1, TweakXL 1.11.4, redscript 0.5.31, Codeware 1.20.3,
  CET 1.37.1, and RED4ext 1.30.0.

Virtual Desktop may create an OpenXR instance before its streamed headset is
available through `xrGetSystem`. The pinned upstream source treated the
resulting `XR_ERROR_FORM_FACTOR_UNAVAILABLE` as permanent for the process. The
VRClient patch retries only that transient result for a bounded 29.5-second
window, while preserving immediate failure and diagnostics for every other
OpenXR error. A live Quest 2 session through VirtualDesktopXR subsequently
verified immersive stereo and HMD tracking on all rotational axes. Runtime
telemetry showed equal 1344x1440 eye recommendations, mirrored eye FOVs, a
stable approximately 0.068 m residual camera separation matching runtime IPD,
and 10198 successful frame submissions with only 2 misses.

### Controller navigation baseline

The OpenXR actions use standard Touch, Index, Vive, and WMR interaction
profiles and are translated to Cyberpunk's gamepad actions. Cyberpunk binds
Pause to Xbox Back/Select and Hub/Map to Xbox Start. The pinned upstream
backend translated its single OpenXR menu action to Start, so the documented
"Pause menu" button could not actually open Pause. VRClient corrects that
translation and keeps Hub/Map reachable with a profile-independent chord:

| VR input | Cyberpunk action | Oculus Touch | Valve Index |
| --- | --- | --- | --- |
| OpenXR menu action | Pause / Back | Left controller menu button | System button when exposed by the runtime |
| Both thumbsticks clicked | Hub/Map / Start | Click L3 + R3 together | Click L3 + R3 together |
| Left primary face button | Interact/reload / X | X | Left-hand primary button |
| Left secondary face button | Weapon switch / Y | Y | Left-hand secondary button |
| Right primary face button | Jump/confirm / A | A | Right-hand primary button |
| Right secondary face button | Dodge/cancel / B | B | Right-hand secondary button |
| Right / left trigger | Fire / aim | Right / left trigger | Right / left trigger |
| Left stick click, release without a chord | Sprint / L3 | Left stick click | Left stick click |
| Right stick click | Crouch / R3 | Right stick click | Right stick click |
| Hold left stick click + move right stick | D-pad | L3 + right-stick direction | L3 + right-stick direction |

The both-stick fallback is required for Index-class runtimes because OpenXR
permits the platform to reserve the controller system action. It also makes the
Hub discoverable without requiring a keyboard.

### VR comfort animation slice

The stereo backend alone does not neutralize REDengine's flat-screen first-person
animation. VRClient therefore installs the pinned upstream
`CyberpunkVRPort_NoAnims` redscript slice separately. It acts at the game-side
animation sources rather than compensating in the render camera:

- forces locomotion camera bobbing off;
- sets the melee camera-shake animgraph weight to zero;
- blocks the sprint weapon-lower pose that fights tracked hands;
- removes presentation-only post-shot camera recoil; and
- makes equip/unequip transitions immediate to avoid camera pull-back.

This deliberately preserves HMD motion and gameplay/ballistic systems. On
2026-08-05, redscript compiled the slice successfully with no errors and the
user confirmed that movement was more comfortable in the following headset
run. These overrides are now guarded by the native per-process activation
contract. They call the original wrapped methods with their original arguments
when `VRCLIENT_VR_ACTIVE` is absent or is anything other than the exact value
`1`.

### Flat-by-default launch contract

Installing the backend does not activate VR globally. Steam, REDlauncher, and
direct executable launches do not receive `VRCLIENT_VR_ACTIVE=1`, so the stereo
plugin registers its read-only redscript status function and publishes a flat
CET state, then returns before settings changes, D3D12/OpenXR hooks, bootstrap,
or worker threads. CET disables all authored VRCAM components and publishes an
empty active-camera value; NoAnims preserves vanilla behavior.

VRClient adds the activation value only to the game child process for **Launch
in VR**. **Launch flat** omits both activation and the OpenXR runtime override.
Every Cyberpunk process rewrites the CET activation state during RED4ext load,
so a VRClient crash cannot make the next external launch enter VR.

The retired `VRClient.REDengine` experiment is retained only as evidence and a
rollback artifact outside the game's `red4ext/plugins` tree. It must not be
installed beside `CyberpunkVR_Stereo`; both own the same D3D12/OpenXR hooks.

Run the read-only preflight after mounting or installing Cyberpunk:

```powershell
python tools/cyberpunk_native_preflight.py `
  --game-root "<SteamLibrary>\steamapps\common\Cyberpunk 2077" `
  --steam-manifest "<SteamLibrary>\steamapps\appmanifest_1091500.acf" `
  --evidence-out "artifacts\cyberpunk-preflight\session-evidence.json"
```

An unpinned profile must return `BLOCKED_FINGERPRINT_UNPINNED` while still
reporting the observed hash and build. Those values require review before the
profile and adapter build ID are promoted together.

VRClient's integrated commands use the same fail-closed checks and Steam
discovery:

```powershell
dotnet run --project client\VrClient.Cli -- cyberpunk-preflight
dotnet run --project client\VrClient.Cli -- cyberpunk-launch --mode vr
dotnet run --project client\VrClient.Cli -- cyberpunk-launch --mode vr --with-vortex
dotnet run --project client\VrClient.Cli -- cyberpunk-launch --mode flat
dotnet run --project client\VrClient.Cli -- cyberpunk-launch --mode flat --with-vortex
```

Add `--dry-run` to validate either route without starting the game.
`--xr-runtime steamvr`, `--xr-runtime vd`, or `--xr-runtime system` overrides
automatic runtime selection for VR mode only. Each route writes a local launch
record under `artifacts/redengine/cyberpunk-2077/launches/`.
The Vortex options require a discoverable Vortex installation and pass
`-modded` to Cyberpunk to enable deployed REDmod content. The VR option retains
the same per-process OpenXR activation; the flat Vortex option leaves VR off.
REDmod-packaged mods also require the free REDmod DLC (`tools/redmod/bin/redmod.exe`);
the launch panel warns when it is absent. Legacy archive and loose mods do not
require that DLC but still require successful Vortex deployment.
Select the Cyberpunk profile and deploy
its mods in Vortex first. AVRcade cannot confirm which Vortex profile was
deployed or whether individual mods are compatible with VR. All routes use the
same installed game directory, so already deployed loose files may also load
on the ordinary VR route; Vortex's own Play action does not activate AVRcade VR.

The Cyberpunk launch panel also writes snap/smooth turn preference to the
installed VR Port's `bin/x64/vrport.ini`. Snap turning uses `xr_snap_turn=1`
and `xr_snap_turn_angle_deg` (15, 30, 45, 60, or 90 degrees in the UI);
smooth turning uses `xr_snap_turn=0`. Both VR launch buttons share this file.
The VR Port does not expose a separate smooth-turn speed setting, so its
continuous turn rate follows the game's controller sensitivity.

## Sources

- [Official Cyberpunk 2077 modding support and REDmod overview](https://www.cyberpunk.net/en/modding-support)
- [Official REDmod technical documentation](https://cdn-l-cyberpunk.cdprojektred.com/REDmod-docs.pdf)
- [CD PROJEKT RED Fan Content Guidelines](https://www.cdprojektred.com/en/fan-content)
- [RED4ext repository](https://github.com/wopss/RED4ext)
- [RED4ext SDK overview](https://docs.red4ext.com/mod-developers/red4ext-and-red4ext.sdk)
- [CyberpunkVR Port repository and architecture](https://github.com/dariulone/cyberpunk-vr-port)
- [CyberpunkVR Port 0.1.0 engineering notes](https://github.com/dariulone/cyberpunk-vr-port/blob/main/docs/RELEASE-0.1.0.txt)
- [CyberpunkVR Port MIT license](https://github.com/dariulone/cyberpunk-vr-port/blob/main/LICENSE)

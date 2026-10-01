# VRClient-owned Unreal conversion

## Product boundary

The native route may depend on platform components that are not VR mods:
the Khronos OpenXR loader, the user's OpenXR runtime, Windows, DXGI, Direct3D,
and a transparent general-purpose loading mechanism.

VRClient owns everything that makes a flat game VR: headset timing, per-eye
view/projection, stereo rendering, OpenXR submission, camera/HUD/comfort/input
policy, engine discovery, and per-game adapters.

An existing game-specific VR mod is not a dependency. UEVR remains an optional
fallback and behavior oracle, but native build, launch, and verification run
with UEVR absent.

## Layering

```text
app/CLI -> safety + exact build identity -> launch-time loader
        -> VRClient OpenXR runtime -> Vulkan or D3D12 backend
        -> reusable Unreal adapter -> versioned game profile
```

Reusable code must not contain Meccha, PenguinHotel, Steam app ID, head-bone,
or paint-state constants. Those belong in the Meccha profile.

## Evidence ladder

1. Compile both graphics backends.
2. Prove D3D12 device/queue/resources/fence and stereo callback locally.
3. Load the runtime into a controlled process through safety/identity gates.
4. Observe the exact Meccha build and renderer without changing output.
5. Submit controlled eye output from the game process.
6. Relay the live game scene into both eyes monoscopically (transport proof
   only, not stereo). Passed 2026-08-01:
   `artifacts/meccha-scene-relay/20260801-000522/`.
7. User-confirm stable headset stereo and head tracking.
8. Verify first-person play, painting, controller aim, HUD, menus, and cutscenes.

Each rung needs its own evidence. UEVR evidence is labeled `fallback` and cannot
satisfy native rungs 4-8.

The rung-5 diagnostic is deliberately isolated from the flat observer. Its
payload adopts the exact observed D3D12 device/direct queue, creates the
OpenXR eye swapchains, and clears left red/right blue through the public stereo
callback. It keeps both color resources in OpenXR's required
`D3D12_RESOURCE_STATE_RENDER_TARGET` state and waits on its own bounded fence
before OpenXR releases them. This proves the graphics connection and head-pose
timing only; it is not evidence that the game scene is stereo.

## Implemented components

- **D3D12 observation core and borrowed-device bridge**
  (`src/native/adapters/unreal/meccha_observer_payload.cpp`): late-attach
  Detours hooks on `Present`, `ResizeBuffers`, and `ExecuteCommandLists`,
  primed from the live swapchain/queue vtables; fail-closed `D3D12Observer`
  validation of the game-owned device, direct queue, and swapchain; atomic
  evidence-JSON writes. The observed device/queue are retained and lent to the
  OpenXR runtime so eye submission runs on the game's own graphics objects.
- **Scene relay renderer**
  (`src/native/adapters/unreal/d3d12_scene_relay_renderer.{h,cpp}`): snapshots
  the final game backbuffer, samples it through a full-screen shader, and
  writes the result into each OpenXR eye target through per-eye RTVs. It is
  source-format-agnostic, including Meccha's
  `DXGI_FORMAT_R10G10B10A2_UNORM` backbuffer converted to RGBA8 eye targets.
  The relay is intentionally monoscopic: it proves the capture-to-per-eye
  transport path, not stereoscopic rendering or camera control.
- **Camera sample, composition, and stability contracts**
  (`src/native/adapters/unreal/unreal_camera_{contract,observer}.{h,cpp}`):
  validate fresh finite engine-space camera data, convert OpenXR reference/eye
  poses into UE coordinates, preserve distinct IPD-scaled eye transforms, and
  refuse missing, ambiguous, stale, or non-monotonic camera candidates. The
  adapter camera-service ABI is controlled-tested, but no live Meccha camera
  provider has been located yet. See `docs/unreal/MECCHA-CAMERA-SEAM.md`.

## Graphics decision

Keep the proven Vulkan backend for the harness and future Vulkan/SteamOS work.
Add D3D12 for the Windows Unreal route rather than translating game textures
through Vulkan. The actual Meccha renderer remains a verification fact. If the
shipping build is D3D11, the D3D12 route refuses until a tested D3D11 backend
exists.

## Safety

Native conversion runs only after exact executable/build identity and existing
anti-cheat/online policy permit it. Meccha verification remains private-room
only. Unknown renderer, build, camera identity, or anti-cheat state means refusal.

Launching the shipping executable or the `PenguinHotel.exe` stub directly
breaks Steam authentication and fails with `failed due to invalid token`. The
runner must launch through `steam://run/4704690`, wait for the real shipping
process, and late-attach only after the fail-closed identity revalidation
passes.

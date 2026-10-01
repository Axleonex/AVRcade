# RDR2 Native Adapter Contract

VRClient now ships the build-scoped `vrclient-rdr2-adapter` target and its
manifest, plus an optional Windows `vrclient_rdr2_bridge` payload. The adapter
is deliberately a service consumer: it does not scan
RDR2 memory, patch arbitrary instructions, or launch the game directly.

The host bridge must provide these versioned services before the adapter can
initialize:

- `rage_graphics`: current renderer/device/swapchain and a stereo-submit
  callback;
- `rage_view`: the active camera pose and field-of-view;
- `rage_game_state`: Story Mode state and whether camera control is safe.
- `rage_input`: accepts normalized VRClient actions at the adapter seam. The
  current bridge records these values but does not yet inject them into RDR2;
  keyboard/mouse remains the smoke-test input path.

On every frame the adapter queries all three services, refuses loading/menu or
camera-unsafe states, and submits the current head pose through the graphics
service. Missing services return `VR_ADAPTER_ERROR_UNSUPPORTED_API`; transient
game state returns `VR_ADAPTER_ERROR_RECOVERABLE`.

The current compatibility matrix is pinned to Steam build `13773296`, matching
the existing local diagnostic evidence. A different build must be observed and
reviewed before it can be admitted. The adapter binary can be built with:

```powershell
cmake --build build/rdr2-adapter-check2 --config Release --target vrclient_rdr2_adapter
```

For the Windows smoke payload, configure with
`VRCLIENT_BUILD_OPENXR_RUNTIME=ON` and `VRCLIENT_BUILD_RDR2_BRIDGE=ON` (the
project-local OpenXR/Vulkan prefixes and Detours 4.0.1 are required), then build
`vrclient_rdr2_bridge`. Stage the resulting DLL as
`vrclient_rdr2_bridge.asi`; ScriptHookRDR2 plus its `dinput8.dll` ASI loader
loads that ASI in Story Mode. The
profile staging command also carries the project-local `vulkan-1.dll` and
`runtime-profile.json` support files. VRClient does not bundle ScriptHookRDR2 or
any Rockstar files.

The bridge artifact is smoke-ready only after `ScriptHookRDR2.dll`,
`dinput8.dll`, and the ASI
are present in the selected game root. The contract is not proof of in-game
stereo, head tracking, controller behavior, or a playable session; those require
the planned early Story Mode smoke run. The current relay copies a monoscopic
scene capture into both OpenXR eye targets and forwards headset yaw/pitch only;
engine-native stereo, positional head translation, and OpenXR controller input
remain separate implementation work.

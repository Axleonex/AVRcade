# GTA San Andreas VRClient S4 Architecture Review

Date: 2026-09-21

## Verdict

The classic x86 GTA San Andreas conversion is machine-ready for physical
headset verification. The reviewed W: build completed the production bridge
path against the test-only x86 OpenXR runtime: startup movies stayed outside
OpenXR, the initial menu submitted as a quad, the real RenderWare tail was
observed, both gameplay eyes were captured, the first gameplay frame completed,
and stereo projection layers were submitted. No remaining code or deployment
blocker was found. Perceived headset quality and comfort remain an independent
human gate.

## System seams reviewed

| Layer | VRClient responsibility | S4 result |
| --- | --- | --- |
| Launch | Exact local offline executable and x86 runtime selection | Dry-run accepted; unpinned bridge rejected |
| Identity/preflight | PE32 executable, size, SHA-256, bridge architecture/hash | Enforced |
| Mod coexistence | Additive ASI; preserve ASI/CLEO/config/script surface | Enforced; no game/mod replacement |
| Native admission | Hash-pinned external profile plus in-image address/signature checks | Enforced |
| D3D9 bridge | Import/vtable interception, reset handling, full backbuffer capture | Strict build and live W: path passed |
| RenderWare stereo | Simulation once, complete render tail once per eye | Integration and live first-frame trace passed |
| OpenXR | x86 loader, D3D11 binding, quad/projection layers, lifecycle recovery | Fake-runtime integration and live process passed |
| Camera | Orientation, position, IPD, asymmetric frustum, state restoration | Behavior and production integration tests passed |
| Input | Optional OpenXR actions translated to keyboard/mouse with focus release | Action sync and menu-confirm regressions covered; headset feel pending |
| Diagnostics | Exact process/hash/profile evidence and first-frame trace | Passed |

## S4 findings fixed

1. Startup protection prevented the initial menu from entering VR until a
   gameplay tail had run. Frontend-menu state now admits a quad while startup
   movie presents remain desktop-only.
2. Launch and install paths accepted an unpinned `BridgeUnverified` artifact.
   All managed and CLI admission paths now require the catalog-pinned bridge.
3. Active camera/render addresses were split between the native profile and
   hard-coded production constants. Every active address now comes from the
   exact external profile; hook sites retain byte-signature checks.
4. Installed native-profile content was checked for existence but not exact
   identity at launch. Managed launch, CLI launch, and the ASI now fail closed
   on profile hash mismatch.
5. The bridge was not compiled under a permanent warning-as-error gate. All GTA
   native targets now build with MSVC `/W4 /WX`; unsafe string-copy warnings
   found by the gate were removed.
6. Physical-headset validation showed that Virtual Desktop exposes a negotiated
   UNORM swapchain through typeless D3D11 textures. The bridge now creates each
   render-target view with the negotiated typed format; the fake runtime mirrors
   this behavior to prevent regression.
7. The frontend controller mapping required Y for Enter while A remained Space.
   A now sends Enter/confirm in menus and returns to Space/jump in gameplay, with
   pressed inputs released across the mode transition.

## Automated and live evidence

- Strict x86 Release build: passed with `/W4 /WX`.
- Native behavior test: passed camera, recenter, IPD, projection, and input policy.
- Production bridge integration: passed stereo pixels, menu quad, tracking loss,
  input release, startup-movie gate, initial-menu gate, and x86 stack balance.
- Static bridge contract and exact native-profile validator: passed.
- Managed tests: 243 passed, 0 failed.
- CLI W: launch dry run: accepted.
- Live W: fake-runtime launch: responsive process, exact executable/profile
  admission, menu quad frames, real RenderWare tail, both eyes captured,
  `stereoRendered=1`, first gameplay frame completed, and stereo projection
  layers observed.
- Reviewed/deployed bridge SHA-256:
  `95fe2a2f1d07b0d7f357806b163803bdc1e050bfb68947c391ef44fea17166cf`.
- Reviewed/deployed native profile SHA-256:
  `847bb6872633110d5330ce92fe94b2fb11ba8f48a5d09a34f5aeb1e25bee98fd`.

## Human headset exit gate

Launch through VRClient with the active 32-bit OpenXR runtime and verify stereo
depth/scale, head pose, recentering, controller feel, HUD/menus/cutscenes,
on-foot and vehicle cameras, save/load, resolution/device reset, runtime
recovery, mod coexistence, comfort, and clean shutdown. Those observations are
sensory/hardware assertions and cannot be certified by the fake runtime.

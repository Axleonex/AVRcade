# RDR2 VRClient S4 Architecture Review

Date: 2026-08-29

## Verdict

The reviewed RDR2 path is build-clean and internally coherent enough for one
bounded Story Mode hardware smoke test. It is not a completed full-VR
conversion. The bridge currently provides a monoscopic D3D12-to-OpenXR relay,
yaw/pitch camera forwarding, guarded Steam launch, and transactional mod-profile
deployment. Native per-eye RAGE rendering, positional 6DoF, and OpenXR
controller input remain blocking architecture work.

## System seams reviewed

| Layer | VRClient responsibility | Review result |
| --- | --- | --- |
| Steam launch | Ownership-preserving AppID launch and Story-only marker | Fixed and regression-tested |
| Identity/preflight | Pinned Steam build and executable SHA-256 | Enforced |
| Mod profiles | Transactional staging/apply/disable/recovery | Active-profile ambiguity fixed |
| Native admission | ScriptHook, ASI loader, bridge and bridge hash | Enforced |
| RAGE bridge | ScriptHook camera service and D3D12 swap-chain observation | Builds strictly; hardware unverified |
| OpenXR runtime | Per-eye swapchains, lifecycle and D3D12 binding | Shared runtime builds; live session unverified |
| Diagnostics | Process-scoped bridge evidence tied to current build/hash | Stale/cross-install acceptance fixed |
| Input | Keyboard/mouse fallback and normalized adapter seam | No OpenXR controller source or RAGE injection |

## Confirmed defects fixed

1. The Story Mode authorization marker was process-local environment state and
   could be lost when Steam was already running. VRClient now forwards the exact
   `-vrclient-rdr2-story` command-line token through Steam, and the bridge parses
   arguments rather than trusting an inherited environment variable.
2. Native readiness checked `ScriptHookRDR2.dll` but not its required ASI loader.
   `dinput8.dll` is now required without bundling either user-owned dependency.
3. Launch could continue when a different mod profile was already active. Same
   profile and different profile now produce distinct results; a different
   active profile blocks launch.
4. An installed bridge could be stale. The installed ASI SHA-256 must now match
   the reviewed VRClient bridge artifact.
5. A shared or reused bridge log could certify the wrong run. Logs are now
   process-ID scoped and truncated at bridge initialization.
6. Readiness could accept weak or stale evidence. It now requires the exact
   schema/title, Steam build, executable SHA-256, live D3D12 module observation,
   and verified camera plus eye-target submission markers.
7. Live diagnostics selected a process by name only. The live executable path
   must now equal the executable admitted by preflight.
8. The optional bridge passes a strict `/W4 /WX` build. An unused hook teardown
   implementation was removed; bridge hooks intentionally live for the RDR2
   process lifetime.

## Remaining architectural blockers

- **Native stereo:** `D3D12SceneRelayRenderer::capture` captures one monoscopic
  back buffer. The renderer copies that source into both OpenXR eyes. Two eye
  submissions are not two independently rendered RAGE views.
- **6DoF:** headset orientation contributes yaw and pitch; headset position is
  not applied to the RAGE camera.
- **VR controllers:** no OpenXR action/controller subsystem feeds the shared
  input service. `applyRageInput` records values but does not invoke RAGE control
  natives. Keyboard/mouse is the only functional input path in this smoke build.
- **Live compatibility:** the W: Steam installation is discovered as build
  `13773296`, and `RDR2.exe` matches the pinned SHA-256. The reviewed baseline
  bridge is applied and hash-matched. Official Script Hook RDR2 v1.0.1491.17 and
  its ASI loader are installed, so the native adapter reports `ReadyForSmoke`.
  No OpenXR frame, headset, or stability assertion can be made until the live
  Story Mode smoke runs.

## Automated evidence

- Managed Release build: 0 warnings, 0 errors.
- Managed tests: 198 passed, 0 failed.
- RDR2 native static contract checks: passed.
- RDR2 bridge normal Release build: passed.
- RDR2 bridge strict `/W4 /WX` Release build: passed.
- W: launch dry run: passed through Steam ownership, profile, build, hash, and
  native bridge gates.
- Baseline staged bridge SHA-256 equals the reviewed artifact SHA-256:
  `6158A9EDCEC32AD44AD1A7FD99F02FB09EF6EE99D09DBC40250F276CD4C5244F`.

## Hardware exit gate

Start the active OpenXR runtime and perform the one Story Mode smoke in
`SMOKE-TEST.md`. The baseline VRClient profile, Script Hook, and ASI loader are
already installed. Passing that smoke validates only the current relay path. A
full-conversion milestone must separately deliver and verify native stereo,
positional 6DoF, and OpenXR controller input.

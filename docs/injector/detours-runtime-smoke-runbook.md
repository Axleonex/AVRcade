# Detours Runtime Smoke Runbook

This optional Phase 3 proof uses the user-built Microsoft Detours `withdll.exe`
as a launch-time loader for the controlled smoke target only, then routes that
load through `runBootstrapSmoke()`.

It does not vendor Detours, implement raw process-injection APIs in VRClient, or
target commercial games. The only approved target for this proof is
`vrclient_smoke_host.exe --runtime-smoke`.

## Inputs

- `VRCLIENT_ENABLE_DETOURS_SMOKE=ON`
- `VRCLIENT_BUILD_OPENXR_RUNTIME=ON`
- `VRCLIENT_DETOURS_WITHDLL=<path-to-user-built-withdll.exe>`
- Built `vrclient_smoke_host.exe`
- Built `vrclient_runtime_smoke_payload.dll`
- Built `vrclient_detours_runtime_smoke.exe`

## What It Proves

The C++ driver builds a known-supported controlled-smoke identity from
`config/games/sample-game.json`, evaluates the Phase 7 safety verdict from
`config/safety/default-rules.json`, and passes the resulting `Allow` verdict to
`runBootstrapSmoke()`. Only then does its `BootstrapRuntimeLoader` adapter launch
Detours `withdll.exe`.

Inside the loaded payload, the smoke host calls `vrclient_runtime_smoke_probe`
outside `DllMain`. The probe calls the real public runtime ABI without starting a
headset session:

- `vr_runtime_create`
- `vr_runtime_get_state`
- `vr_runtime_get_headset_state`
- `vr_runtime_get_frame_data`
- `vr_runtime_destroy`

The payload writes a per-run sentinel:

```text
pid=<process-id>;token=<run-token>;module=vrclient_runtime_smoke_payload.dll;runtime_create=VR_RUNTIME_OK;runtime_state=stopped;headset_state=VR_RUNTIME_OK;frame_data=VR_RUNTIME_OK;eye_count=2;pose_timing_api=available
```

The driver also runs a refusal leg with a known anti-cheat observation. That leg
must return `RefusedSafetyVerdict`, must not invoke the Detours loader, and must
not write a blocked sentinel.

## Scope

This proves no-headset runtime DLL delivery into a controlled local harness,
public runtime ABI availability, default frame/pose data-structure availability,
and bootstrap diagnostics containing `game_id`, `build_id`, `runtime_load_result`,
`pose_timing_api`, and the safety reason code.

It does not prove attach-to-running-process injection, R.E.P.O. injection, a live
OpenXR session, or real headset-tracked pose data.

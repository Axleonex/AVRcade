# Detours Controlled Smoke Runbook

This optional Phase 3 proof uses a user-built Microsoft Detours `withdll.exe`
as the launch-time loader for the controlled smoke target only.

It does not vendor Detours, implement raw process injection APIs in VRClient, or
target commercial games. The only approved target for this proof is
`vrclient_smoke_host.exe --once`.

## Inputs

- `VRCLIENT_ENABLE_DETOURS_SMOKE=ON`
- `VRCLIENT_DETOURS_WITHDLL=<path-to-user-built-withdll.exe>`
- Built `vrclient_smoke_host.exe`
- Built `vrclient_proof_payload.dll`

The payload writes a per-run sentinel from inside the launched smoke host:

```text
pid=<process-id>;token=<run-token>;module=vrclient_proof_payload.dll;exe=<smoke-host-path>;pose_timing_api=available
```

The token prevents stale-file false greens. The `exe=` field proves the payload
executed inside the controlled smoke host image. The pose/timing marker is a
local proof-payload availability marker, not headset pose evidence.

## Scope

This proves launch-time DLL delivery into a controlled local harness through a
user-supplied Detours tool. It does not prove attach-to-running-process injection,
R.E.P.O. hook compatibility, anti-cheat compatibility, or real headset pose data.

The stronger OpenXR-on runtime ABI proof is separate:
`docs/injector/detours-runtime-smoke-runbook.md`.

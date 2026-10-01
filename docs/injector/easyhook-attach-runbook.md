# EasyHook Attach Runbook

This optional Phase 3 proof wires a user-supplied EasyHook-compatible attach
loader into VRClient's existing bootstrap safety spine.

The loader is not vendored by VRClient. VRClient does not implement raw process
injection APIs for this path. The external executable must provide this CLI:

```powershell
AttachLoader.exe --pid <pid> --dll <payload.dll>
```

Exit code `0` means the DLL was loaded into the target process. Any nonzero exit
code is treated as `easyhook_attach_failed`.

## Scope

The proof starts `vrclient_smoke_host.exe` first, discovers it through the
`AttachRunning` flow, verifies the configured smoke-host identity, evaluates the
Phase 7 safety verdict, runs the Phase 3 safety preflight, and only then invokes
the external attach loader.

The loaded payload is `vrclient_proof_payload.dll`. It writes a tokenized
sentinel from inside the controlled smoke host. This proves attach-to-running
delivery only. It does not prove R.E.P.O. injection, headset-tracked pose data,
or live OpenXR session behavior.

## Required Command

```powershell
rtk powershell -NoProfile -ExecutionPolicy Bypass -File scripts/ci/build-and-test.ps1 `
  -BuildDir build/ci-easyhook-attach `
  -EasyHookAttachSmoke `
  -EasyHookAttachLoader C:\Tools\AttachLoader.exe `
  -Note "Phase 3 EasyHook attach-to-running controlled smoke proof"
```

The default build remains unaffected because `EasyHookAttachSmoke` is OFF by
default.

## Safety Behavior

The attach loader must not be invoked unless all of these are true:

- Target identity is `known-supported`.
- The target is the controlled local smoke host.
- Phase 7 returns `Allow` or an explicitly acknowledged `Warn`.
- Phase 3 safety preflight approves.

The capture also runs a blocked anti-cheat observation. That leg must return
`RefusedSafetyVerdict`, print `blocked_loader_invoked=false`, and write no
blocked sentinel.

## Accepted Loader Shape

Use a transparent user-mode EasyHook wrapper that takes only `--pid` and `--dll`,
logs plainly, and returns a deterministic exit code.

Do not use loaders that advertise stealth, anti-cheat bypass, manual mapping,
kernel drivers, process hiding, EDR bypass, or any attach behavior that cannot be
explained from ordinary user-mode DLL loading.

## Result Codes

| Code | Meaning | Operator action |
|---|---|---|
| `easyhook_attach_launch_failed` | The configured attach loader executable could not be started. | Verify `EasyHookAttachLoader` points to the expected executable. |
| `easyhook_attach_timeout` | The attach loader did not exit within the bounded timeout. | Kill stale test processes and retry against the controlled smoke host. |
| `easyhook_attach_failed` | The attach loader returned a nonzero exit code. | Inspect the attach loader's own logs. |
| `easyhook_attach_sentinel_unconfirmed` | The loader exited successfully, but the payload did not prove in-target execution. | Verify the loader's CLI contract and payload architecture match the smoke host. |

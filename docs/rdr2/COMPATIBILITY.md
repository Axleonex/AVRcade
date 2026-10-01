# RDR2 Compatibility and Diagnostic Baseline

This document is the non-production handoff for the reviewed, build-scoped evidence collected in Phase 12.

## Scope

- Identity: Steam AppID `1174180`, title `RDR2.exe`
- Mode: `story` only
- No Red Dead Online coverage
- First smoke payload is D3D12-only and launches with `-dx12`; Vulkan capture
  remains a future bridge path.
- Read-only diagnostics only (no launch, no file mutation in game folders)

## Baseline evidence

On 2026-08-24, local evidence collection ran against the installed game:

- `rdr2-preflight --mode story` now discovers the Crucial X9 `X:` Steam library,
  validates build `13773296`, and reports `ReadyForRageEvidence`.
- Executable SHA-256 observed as:
  `b56c9548f670654a9b73bf25def3cd73af12e269f6e47dba28a34079adaf465e`
- `rdr2-diagnostics --mode story --run-id phase12-static-renderer-v2` wrote:
  `%LOCALAPPDATA%\VRClient\games\red-dead-redemption-2\evidence\rdr2-phase12-static-renderer-v2.json`
- JSON evidence fields currently include:
  - `Schema`: `vrclient-rdr2-diagnostic/1`
  - `Compatibility`: `candidate`
  - `RendererObservation`: `vulkan_and_d3d12_candidate`
  - `CameraObservation`: `unknown`

## Compatibility outcome

The build identity is pinned and preflight-ready. Compatibility remains
`candidate` until live in-process diagnostics and native capture provide a
proven renderer+camera contract.

## Native adapter handoff

The native RAGE adapter contract is now present at `adapters/rdr2/` and is
build-scoped to this fingerprint. The optional Windows bridge is built at
`artifacts/adapters/rdr2/vrclient_rdr2_bridge.dll` and can be staged as
`vrclient_rdr2_bridge.asi`; ScriptHookRDR2 remains a user-provided prerequisite.
Static evidence and a built payload still do not establish live stereo readiness.

To collect live observations, the game must be running in Story Mode via Steam by the user:

```
dotnet run --project client/VrClient.Cli/VrClient.Cli.csproj -c Release --no-build -- rdr2-live-diagnostics --mode story --run-id phase12-live-<suffix>
```

## Refusals and fail-closed behavior

- `rdr2-preflight --mode online` must continue to return an Online rejection.
- `FingerprintUnpinned` / manifest mismatch must block downstream "ready" checks.
- `rdr2-launch --mode story` is now implemented, but remains fail-closed until
  the pinned preflight and installed bridge prerequisites are ready. It launches
  only through Steam AppID `1174180`; pass `--profile <id>` to apply a validated
  bridge profile before launch.

Use `rdr2-readiness` for deterministic blocker reporting while in transition.

# Native Meccha Documentation Drift Audit — 2026-07-24

Snapshot as of 2026-07-24; superseded facts (D3D12 now live-verified, game now
installed) — see `PASSOVER-NATIVE-MECCHA.md` for current state.

Scope: the current Meccha handoff spine plus UEVR-era operational documents.
All executed checks were read-only. Mutating launch/fetch/injection commands
were classified by proxy and were not executed.

## Classification

| Claim/document | Verdict | Evidence | Action |
|---|---|---|---|
| Native GSD plan exists | VERIFIED | `.planning/phases/10-native-unreal-conversion/10-01-PLAN.md` | None |
| Native architecture exists | VERIFIED | `docs/unreal/NATIVE-ARCHITECTURE.md` | None |
| Meccha native profile exists and keeps renderer/bridge unverified | VERIFIED | schema validator plus profile content | None |
| Root `PASSOVER.md` is the current resume document | DRIFTED | it routes next work to UEVR injection closure | Banner as superseded; point to `PASSOVER-NATIVE-MECCHA.md` |
| `UEVR-ORCHESTRATION.md` is the active Unreal architecture | DRIFTED | Phase 10 makes native ownership primary | Banner as legacy fallback research |
| UEVR Meccha profile README is current native tuning | DRIFTED | it deploys to `%APPDATA%\UnrealVRMod` and uses UEVR Lua | Banner as legacy UEVR-only |
| `scripts/meccha-rtx-qa.ps1` closes current Meccha requirements | DRIFTED | it validates UEVR artifacts, not `NUE-*` native evidence | Mark legacy fallback; forbid native closure use |
| Windows v0.2 app/release docs describe that release | VERIFIED historical | their UEVR statements match the shipped v0.2 route | Banner as historical so they are not read as Phase 10 |
| UEVR JSON/schema/release data still exists | VERIFIED historical | paths exist; Windows v0.2 consumes them | Retain; route through `config/unreal/README.md` |
| Live Meccha renderer is D3D12 | UNTESTABLE | profile says expected D3D12, observed API remains unverified | Do not promote before game observation |
| UEVR fetch/launch/apply commands work now | UNTESTABLE (proxy) | commands are mutating and belong to legacy route | Do not execute during native audit |

## Counts

- Claims/doc groups: 11
- VERIFIED: 5
- DRIFTED: 4
- DEAD: 0
- UNTESTABLE: 2

No legacy file is dead: it remains useful for the shipped v0.2 fallback or as
historical research. The hygiene fix is explicit routing, not deletion.

## Repeatable check

Run read-only:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/audit-native-meccha-docs.ps1
```

The checker fails if authoritative paths disappear, legacy banners are removed,
the native profile overclaims renderer/bridge status, required CMake references
disappear, or the passover stops denying unbuilt game integration.

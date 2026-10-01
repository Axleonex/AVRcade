# Native pre-headset evidence audit — 2026-07-24

Snapshot as of 2026-07-24; superseded facts (D3D12 now live-verified, game now
installed) — see `PASSOVER-NATIVE-MECCHA.md` for current state.

Scope: the claim that all useful work possible without the installed Meccha
binary has been built and controlled-tested. This does not claim in-game or
headset success.

## Gate inventory and falsification

| Gate | Claim | Data | Concrete failing input/state | Red proof | Verdict |
|---|---|---|---|---|---|
| Full default MSVC build | New code integrates with the repository | Real source/toolchain | Compile or link error in any target | A prior include defect made the dual-backend build red; current full build is green | SOUND for build only |
| Full default CTest | Registered regression suite remains green and non-vacuous | 59 controlled tests | Any assertion failure or zero discovered tests | 59 tests discovered; negative fixtures execute inside the suite | SOUND for controlled behavior |
| `vr_native_unreal_tests` | Readiness, camera, submission, D3D12 observation, and borrowed binding refuse bad state | Controlled D3D12 objects and fixtures | Unpinned/wrong hash, external converter, missing swapchain, ambiguous camera, shared eye target, NaN matrix, compute queue, adapter mismatch | Each input is injected and must return its distinct refusal enum | SOUND for controlled contracts |
| `vr_meccha_native_adapter_tests` | Exact adapter lifecycle is fail-closed | Controlled DLL host/services | Stale build, launcher stub, missing graphics, absent shutdown | Inputs are injected; stale/launcher refuse and pending graphics skips without disabling | SOUND for controlled lifecycle |
| `meccha_native_preflight` | Disk identity route refuses until exact binary is pinned | Synthetic executable and Steam manifest | Null hash, wrong hash, wrong build, wrong executable, absent file | Test observes exit codes 5, 6, and 4 before the pinned fixture reaches 0 | SOUND for mechanism; not game evidence |
| Public API contract | Runtime ABI exposes D3D12 without backend type leakage | Real public header/source | Enum drift, legacy selector acceptance, forbidden backend name/type | New comment containing a forbidden backend name made this gate red, then was corrected | SOUND for static contract |
| OpenXR-on `vr_runtime` build | Borrowed-device changes compile/link in the dual-backend runtime | Real source/toolchain and local SDKs | Signature/type/link incompatibility | Current focused build is green; earlier include-order defect was red | SOUND for compile/link only |
| Documentation drift audit | Current handoff routes agents to native work and marks legacy/outdated paths | Real docs/source paths | Missing native path, removed legacy banner, promoted game capability, stale handoff denial | Ten claims run; prior bad regex demonstrated a red result | SOUND for routing |
| Meccha flat observation | Real renderer and safe hook surface are known | No real game data | Missing installed shipping binary | App `4704690` is absent from the configured Steam library | NOT RUN; closure blocked |
| D3D12 headset session and game stereo | Real headset/game bridge works | No real game/headset data | No active game/session/headset evidence | Not executed | NOT RUN; closure blocked |

## Integrity notes

- Controlled fixtures are labeled controlled and do not promote the profile's
  `game_observed` or headset states.
- The test author and implementation author are the same work session, so an
  independent review remains useful before release. The injected negative
  states reduce, but do not remove, that self-grading risk.
- Compilation is not used as a proxy for runtime, and the local D3D12 probe is
  not used as a proxy for Meccha.

## Closure impact

The autonomous pre-game layer can close. The native Meccha milestone cannot:
the installed shipping binary, flat renderer observation, in-process
interception/loading proof, and headset ladder remain load-bearing and have no
real data yet.

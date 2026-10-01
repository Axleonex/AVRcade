# RDR2 foundation boundary

Phase 11 establishes a fail-closed RDR2 Story Mode foundation. It does not
launch RDR2, start Steam or Rockstar Games Launcher, load a native adapter,
inspect game memory, submit OpenXR frames, or claim headset verification.

## Identity and safety

Only Steam AppID `1174180` is considered. The RDR2 profile requires the normal
Steam-to-Rockstar ownership chain, `RDR2.exe`, and explicit `story` intent.
Online, missing, or unknown intent refuses before executable hashing or profile
mutation. The committed profile is pinned to the observed Crucial X9 Steam
installation (build `13773296` and its SHA-256) and reviewed for Story Mode;
renderer/camera compatibility remains separately gated by native evidence.

## External profiles and recovery

Profiles are stored below `%LOCALAPPDATA%/VRClient/games/red-dead-redemption-2/`
(or an explicitly supplied test root), never under the game directory. A profile
contains staged files and a manifest with game-root-relative targets and hashes.
Absolute paths, traversal, reparse points, `.vrclient`, `RDR2.exe`, and
`PlayRDR2.exe` are rejected. Apply creates verified external backups and an
atomic journal before each game-file replacement. Disable and recovery verify
the journal/backups and refuse as `manual_recovery_required` when current bytes
cannot be reconciled; they remain available after a build/profile pin changes.

State, staging, transactions, evidence, locks, and IPC identifiers are scoped
to the RDR2 title and run ID. An unavailable second XR focus becomes
`waiting_for_xr_focus`; it never terminates another converter or mutates files.

## CLI phase gates

Phase 12 may add only reviewed, build-specific RAGE render/camera evidence.
It must not promote the target to launchable or VR-ready without a reviewed
fingerprint, Story Mode safety approval, and its own evidence gates.

- `rdr2-preflight --mode story` — identity gate and hash/build review
- `rdr2-diagnostics` / `rdr2-live-diagnostics` — evidence collection
- `rdr2-readiness` — strict readiness blocker check for this phase

`rdr2-readiness` remains explicit about blockers and is expected to return a
`blocked_*` state until the native adapter and hardware validation phases are
implemented.

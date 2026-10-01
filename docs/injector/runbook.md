# Injector Operator Runbook

This runbook maps discovery, versioning, safety, and bootstrap outcomes to operator action. The initial Phase 3 smoke path is represented by `config/games/sample-game.json`; selected commercial targets such as `config/games/repo.json` may load only through exact build identity, allowlisted source, known-safe anti-cheat policy, explicit online/session policy, architecture match, and runtime hash match.

## Normal Flow

1. Discovery returns one or more target descriptors without touching the process.
2. Version detection returns `known_supported` for the controlled smoke build or an explicitly selected commercial build.
3. Safety preflight returns `approved`.
4. Bootstrap records `runtime_load_result=loaded`, `game_id`, `build_id`, `adapter_id`, `preflight_verdict`, and `openxr_state`.

The optional Detours controlled smoke proof is documented in
`docs/injector/detours-smoke-runbook.md`. It uses a user-built Detours
`withdll.exe` only for `vrclient_smoke_host.exe --once` and is not enabled in the
default CI build.

The stronger no-headset runtime proof is documented in
`docs/injector/detours-runtime-smoke-runbook.md`. It requires both Detours smoke
and OpenXR runtime compilation, loads `vrclient_runtime_smoke_payload.dll` into
`vrclient_smoke_host.exe --runtime-smoke` through `runBootstrapSmoke()`, and proves
the public `vr_runtime_*` ABI can create/query default frame and pose/timing data
before a headset session starts.

The optional EasyHook-compatible attach proof is documented in
`docs/injector/easyhook-attach-runbook.md`. It uses a user-built external
`AttachLoader.exe --pid <pid> --dll <payload.dll>` to load only
`vrclient_proof_payload.dll` into an already-running controlled smoke host after
the existing identity, Phase 7 verdict, and Phase 3 preflight gates approve.

For selected multiplayer-capable games, `online_risk: private_modded_coop` is the only multiplayer approval state. It requires confirmed private/modded session scope and explicit mod compatibility confirmation. Public matchmaking, public online state, or unknown online state still refuses before load.

## Refusal Matrix

| Reason code | User-facing explanation | Developer next step |
|-------------|-------------------------|---------------------|
| `missing_executable` | The selected executable could not be found. | Re-scan the install path or correct `config/games/sample-game.json`. |
| `missing_process` | The selected running process is no longer available. | Re-run discovery and attach to a fresh descriptor. |
| `multiple_install_candidates` | More than one matching install was found. | Ask the user to choose a descriptor; never guess silently. |
| `process_restarted` | The process changed after discovery. | Re-run discovery; reject stale PID/start-token pairs. |
| `running_process_mismatch` | The running process does not match the discovered executable. | Re-run discovery and verify the executable path before attach. |
| `privilege_mismatch` | Current privileges cannot safely inspect or bootstrap the target. | Relaunch with the required privilege level or use direct launch. |
| `architecture_mismatch` | Target architecture does not match the runtime policy. | Use a matching x64 smoke host/runtime pair. |
| `runtime_architecture_mismatch` | Runtime binary architecture does not match the target. | Rebuild or select the runtime binary matching the target architecture. |
| `unknown_build` | The target build is not recognized. | Add a supported fingerprint config after verifying the build. |
| `ambiguous_build` | Fingerprint evidence matches multiple builds. | Tighten hashes/signatures so one build wins deterministically. |
| `unsupported_sample_build` | The build is explicitly unsupported. | Update the controlled smoke host or select a supported build. |
| `identity_not_supported` | Bootstrap was requested without a `known-supported` identity. | Fix the caller: bootstrap must be downstream of version detection. |
| `unsafe_target_source` | The discovery source is not allowlisted for the selected target. | Use an allowlisted direct, manual, attach, Steam, or Epic flow for that target policy. |
| `unknown_anti_cheat_risk` | Anti-cheat risk is unknown, so bootstrap is blocked. | Add explicit risk metadata after safety review. |
| `unsafe_anti_cheat_risk` | Known anti-cheat risk blocks bootstrap. | Do not proceed; this belongs to a later safety decision. |
| `unknown_online_risk` | Online-mode risk is unknown, so bootstrap is blocked. | Add explicit offline/safe metadata before retrying. |
| `online_mode_risk` | Online-mode risk blocks bootstrap. | Use only controlled smoke or explicitly offline selected-game paths. |
| `public_matchmaking_risk` | Public matchmaking or unknown public online state was detected. | Do not proceed; select a private/modded session or offline path. |
| `private_multiplayer_not_confirmed` | Private modded co-op policy was configured, but the requested session was not confirmed private/modded. | Confirm private/session scope before retrying. |
| `multiplayer_mod_compatibility_unconfirmed` | Private modded co-op policy was configured, but mod compatibility was not explicitly confirmed. | Confirm all required players/tools/builds are compatible before retrying. |
| `offline_policy_multiplayer_scope` | An offline/no-online target policy was asked to approve a multiplayer session. | Use an offline session or switch to an explicit private/modded co-op policy. |
| `runtime_binary_missing` | Runtime binary path is missing or unavailable. | Build the runtime and update the configured path. |
| `runtime_hash_missing` | Runtime binary hash evidence is missing or malformed. | Compute the runtime SHA-256 from the selected binary before retrying. |
| `runtime_hash_mismatch` | Runtime binary hash does not match config. | Rebuild or update the config after verifying provenance. |
| `runtime_already_loaded` | The runtime marker already exists in the target. | Stop; avoid double-load and inspect cleanup state. |
| `untrusted_identity_evidence` | The target identity was supplied by an untrusted caller path. | Re-run discovery with a trusted file or process probe. |
| `executable_name_mismatch` | The discovered executable name does not match the target config. | Verify the selected executable before version detection. |
| `interrupted_before_load` | Bootstrap was interrupted before load. | Re-run after ensuring the controlled host is stable. |
| `runtime_loader_missing` | Bootstrap had no explicit runtime loader/probe implementation. | Wire the loader adapter; Phase 3 must not report simulated load success. |
| `runtime_load_failed` | The controlled runtime load failed. | Inspect diagnostics and loader error details; cleanup should be recorded. |
| `detours_withdll_launch_failed` | The user-supplied Detours launcher could not be started. | Verify the configured `withdll.exe` path and local permissions. |
| `detours_withdll_timeout` | The controlled Detours launch did not finish within the bounded timeout. | Inspect the smoke host and kill stale local test processes before retrying. |
| `easyhook_attach_launch_failed` | The user-supplied EasyHook-compatible attach loader could not be started. | Verify the configured `AttachLoader.exe` path and local permissions. |
| `easyhook_attach_timeout` | The attach loader did not finish within the bounded timeout. | Kill stale smoke hosts and retry only against the controlled host. |
| `easyhook_attach_failed` | The attach loader returned a nonzero exit code. | Inspect the loader logs and verify `--pid <pid> --dll <payload.dll>` syntax. |
| `easyhook_attach_sentinel_unconfirmed` | The loader exited, but the in-target payload did not prove execution. | Verify payload architecture and that the target is the controlled smoke host. |
| `runtime_smoke_host_failed` | The controlled smoke host returned a nonzero exit during the runtime smoke probe. | Inspect the runtime smoke sentinel/logs and payload dependency DLL staging. |
| `runtime_smoke_sentinel_unconfirmed` | The runtime smoke payload did not write a matching tokenized sentinel. | Reject the run as stale/wrong-process evidence and rerun after clearing the sentinel path. |
| `pose_timing_unavailable` | Runtime loaded but pose/timing probe failed. | Verify Phase 1 runtime exports and OpenXR availability. |

## Required Diagnostic Fields

Every refusal or bootstrap outcome should log:

- `game_id`
- `build_id`
- `reason_code`
- `preflight_verdict` when safety was evaluated
- `bootstrap_result` when bootstrap was evaluated
- `runtime_load_result` when a load was attempted
- `adapter_id` for the placeholder Phase 3 adapter value
- `openxr_state` for the smoke probe state

## Scope Boundaries

- No anti-cheat bypass, stealth, memory patching, or remote-thread injection belongs in this safety gate.
- User-supplied launch-time Detours smoke proof is controlled-harness-only and
  does not authorize commercial game injection.
- User-supplied EasyHook-compatible attach proof is controlled-harness-only and
  proves attach-to-running delivery only after the safety gate approves. It does
  not prove R.E.P.O. injection or live headset-tracked pose data.
- Detours runtime smoke proves no-headset public runtime ABI availability only;
  live headset-tracked pose data still requires a headset session.
- Signature and offset config fields are identity evidence only.
- Discovery returns descriptors; it never performs bootstrap.
- Unknown identity, risk, architecture, runtime hash, or already-loaded state means refusal.

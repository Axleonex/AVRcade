# Hook-Discovery Runbook — Live R.E.P.O. Capture Session

This runbook governs the user-gated live capture sessions that promote
R.E.P.O. hook candidates in `config/hooks/repo.json` from `candidate` to
`validated`. It implements bolt-on Phase B2 requirements RE-03/RE-04/RE-05 and
sits downstream of the Phase 3 safety preflight and the Phase 6 R.E.P.O.
private/modded co-op gate.

## Absolute Rule (read first)

No anti-cheat evasion, tampering, stealth, or bypass capability — ever.

If a hook is only reachable by defeating, disabling, weakening, or hiding from
an integrity check of any kind, set that hook's `status` to `blocked` in
`config/hooks/repo.json`, record the exact obstacle in `blocked_reason`, and
**stop all work on that hook**. Do not look for an alternate route around the
check. Blocked is a terminal state for B2; it is never "blocked until we find
a workaround."

## Target Identity (the only supported target)

- Game: R.E.P.O. — `game_id`: `repo`
- Build: `steam-3241660-build-23363152` (Steam app `3241660`, build `23363152`)
- Executable: `REPO.exe` (`H:\SteamLibrary\steamapps\common\REPO\REPO.exe`)
- Executable SHA-256: `412f7cf79cf16888999e22905ca3bb11a6074857efdf8c754073a47ef872317c`
- Unity build: `2022.3.67f2 (6bedba8691df)`, x64, Unity/Mono

Any other game ID, executable name, hash, or build ID must refuse before the
harness loads. Identity uncertainty blocks by default — detection is advisory,
uncertainty refuses.

## Preconditions (all required, in order, before any capture)

1. **User gate.** A live capture session runs only with explicit user
   confirmation for this specific session. The harness never attaches
   automatically or as a background task.
2. **Safety preflight pass.** The Phase 3 `safety_preflight`
   (`src/native/injector/safety/safety_preflight.cpp` path) must return an
   allow verdict for the running target. The B2 harness refuses any target the
   preflight does not approve — no override flag exists.
3. **Exact build hash match.** The running `REPO.exe` must hash to
   `412f7cf79cf16888999e22905ca3bb11a6074857efdf8c754073a47ef872317c` and the
   Steam manifest must report build `23363152`. A Steam update that changes
   either value invalidates this entire hook surface: stop, do not attach, and
   re-run discovery against the new build only after a new dossier pass.
4. **Private/modded co-op or offline session confirmed.** R.E.P.O. capture is
   restricted to a private/modded co-op session or fully offline validation.
   Public matchmaking, public online state, unconfirmed private room state, or
   unknown network state are block conditions — confirm session scope before
   attach, and abort if it cannot be positively confirmed. Modding-community
   signals are compatibility evidence only; they never substitute for this
   confirmation.
5. **Anti-cheat scan clean.** The install-scan evidence
   (`artifacts/phase06/repo/selection/index.md`) must still hold: no bundled
   anti-cheat present. `config/games/repo.json` must still record
   `anti_cheat_risk: known_safe`. If any anti-cheat or integrity-protection
   component is found at session time, refuse the session entirely.

## In-Target Capture Mechanism (built and CTest-proven)

The end-to-end capture pipeline this runbook drives is now built and proven —
**cooperatively, in a second approved process** — by the observation host
`vrclient_hookdisc_obs_host`
(`src/native/tooling/hookdisc/obs_host_main.cpp`). It is the reference
implementation of the read-only capture flow steps 3–8 below, exercised against
the controlled smoke target rather than against R.E.P.O.:

- The host is a separate OS process that links the `vr_tooling_hookdisc`
  harness at build time. It runs `runHookSurfaceSafetyGate` against the
  controlled smoke spec (`config/games/sample-game.json`) **on itself first**;
  only a `SafetyVerdict::Approved` proceeds, and there is no force/override flag.
- After the gate approves, it runs the full read-only pipeline **inside its own
  process**: module/export/RTTI enumeration plus a hardware-breakpoint observer
  armed on its own known function before worker threads run. It then promotes
  that function `candidate -> validated` with the *real captured evidence*
  (`method: hardware_breakpoint`, `hit_count`, thread/cadence) and writes a
  schema-valid hook-surface JSON to an explicit output path **only** — never to
  `config/hooks/*`.
- The CTest driver `vrclient_hookdisc_obs_capture`
  (`tests/native/tooling/hookdisc_obs_capture.ps1`) launches it under a timeout
  guard, asserts a clean exit and a promoted-and-validated entry, re-validates
  the emitted JSON with the same `validate_hook_surface.py` and
  `vrclient_hookdisc doc-validate` used below, and then re-asserts the
  safety-gate regression (`vrclient_hookdisc inspect repo` must refuse).

**Honest scope (do not overclaim).** This proves COOPERATIVE in-target capture
only: a controlled target process that itself loads/links the harness and
captures from inside. It is **not** uncooperative injection and it builds **no**
cross-process loading primitive (no `CreateRemoteThread`/`LoadLibrary`/
`WriteProcessMemory`/`VirtualAllocEx`). Export/RTTI parsing and thread arming
remain current-process; the host satisfies that by being the process that runs
the harness.

**Delivery into R.E.P.O. (uncooperative) rides the safety-gated injector.**
R.E.P.O. does not cooperatively load this harness, so a live session delivers
the harness into the running `REPO.exe` through the existing Phase 3
safety-gated INJECTOR — under the same `runSafetyPreflight` gate (precondition 2
below), never through any new injection capability added to this toolkit. The
`BootstrapRuntimeLoader` seam that performs that delivery is still a stub in v1;
implementing it is the injector's job at the user-gated session, not this
toolkit's. The **Absolute Rule** above and the BLOCKED terminal state apply
identically inside the target regardless of how the harness was delivered: no
evasion, no tampering, no bypass — ever.

## Capture Procedure (per session)

1. **Pick targets.** Choose the candidate hooks for this session from
   `config/hooks/repo.json` (entries with `status: candidate`). Prioritize by
   adapter need: camera/projection first (ADAPT-02), then HUD (ADAPT-04),
   then input/interaction (ADAPT-03/05), then mode-state and block markers.
2. **Launch the session.** Start R.E.P.O. in a private/modded co-op or offline
   session per precondition 4. Confirm the session scope in-game before
   proceeding.
3. **Run the preflight.** Execute the safety preflight against the running
   process. Record the allow verdict. Refusal at this step ends the session.
4. **Load the B2 inspection harness (read-only).** The harness performs
   non-invasive enumeration only: module list, export tables, RTTI/managed
   symbol candidates, and candidate call-site identification. It patches
   nothing at this stage. All output is logged through Phase 2 diagnostics —
   no ad-hoc logging, no blocking I/O on the game's frame path.
5. **Resolve each candidate.** For each selected hook, confirm the candidate
   `name` resolves in the expected `module` (for example `Assembly-CSharp.dll`
   or `PhotonUnityNetworking.dll`). Correct the `module` field if the
   observation disagrees with the candidate attribution. If a name does not
   resolve at all, leave it `candidate` with `confidence: low` and note the
   non-resolution in the session log — do not invent a locator.
6. **Observe, do not transform.** Attach read-only observation using one of
   the approved methods: `hardware_breakpoint`, `vtable_observation`, or
   `pass_through_detour` (a detour that forwards unchanged and only counts
   hits). Run normal gameplay in the safe session and record, per hook:
   - observed cadence (`cadence_hz`) or a `per_frame` determination, using the
     Phase 1 frame-timing API as the cadence reference
   - thread context (`render`, `main`, `worker`, or `unknown`)
   - total `hit_count` over the observation window
   - capture timestamp (`captured_at`, UTC `YYYY-MM-DDThh:mm:ssZ`)
7. **Integrity-check rule check.** If reaching, observing, or detouring a hook
   would require defeating any integrity check, apply the Absolute Rule above:
   mark it `blocked` with a `blocked_reason` and stop work on that hook.
8. **Detach cleanly.** Remove all breakpoints/detours, unload the harness, and
   confirm via diagnostics that the game process is untouched on exit.

## Promotion: candidate → validated

A hook may be promoted only when the read-only observation proved it is hit at
the expected cadence and thread **before any transform is enabled**. To
promote, edit its entry in `config/hooks/repo.json`:

1. Set `status` to `validated`.
2. Add the complete `validation` object: `method`, `cadence_hz` or
   `per_frame`, `thread_context`, `hit_count`, `captured_at`. Incomplete
   evidence fails validation — there is no provisional-validated state.
3. Record the proven locator: keep or correct `symbol`, and add `signature`
   and/or `offset` only with values actually observed this session for this
   exact build. Never copy offsets from another build or from community
   sources.
4. Raise `confidence` to match the evidence (`medium` for a single clean
   session, `high` for reproduced observations across sessions).
5. Update `generated_at` and `generated_by` to reflect the capture session.
6. Re-run the validator from the repo root and require a pass:

   ```
   [Either] python tests/native/tooling/validate_hook_surface.py config/hooks/repo.json
   ```

7. Record the session (date, session scope, preflight verdict, hooks promoted
   or blocked) in the B1 evidence ledger (`.planning/evidence/LEDGER.md`).

Validated entries in `config/hooks/repo.json` are the artifact that unblocks
adapter transform work (ADAPT-02..ADAPT-06 / Phase 6 Task 5). The hook surface
stays data-only and versioned: adapters consume it from config; no offsets or
signatures are compiled into code.

## Blocked Entries

For any hook marked `blocked`:

- `blocked_reason` must state the concrete obstacle (for example, "reachable
  only past an integrity check on module load").
- The entry stays in the file as a permanent record; do not delete it.
- Do not revisit blocked hooks in later sessions unless the obstacle itself is
  removed by the game vendor in a new supported build — which is a new build
  ID, a new dossier, and a new hook-surface document anyway.

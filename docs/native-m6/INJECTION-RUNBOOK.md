# Native injection runbook (M6 Half B) — GATED

**Half B does NOT run without authorization.** Every Phase 3+ task in
the M6 plan refuses with `RESULT: injection-gated` unless
`docs/native-m6/INJECTION-AUTHORIZATION.md` exists AND contains the literal line
`CYBER-VERIFICATION: CLEARED` (see `INJECTION-AUTHORIZATION.md.template`).

This runbook documents the gated procedure; it is informational until the token
exists.

## Preconditions (all required before ANY injection step)

1. The `CYBER-VERIFICATION: CLEARED` token is present (the absolute gate, §9 rule 9).
2. A selected target game recorded in `docs/native-m6/TARGET-B.md` (`SELECTED SLUG:`),
   chosen by M6D6 criteria (owned, installed, the policy explicitly permits it).
3. The M5 / Phase 7 safety gate returns **Allow or Warn** for the target — checked
   BEFORE anything else. Block/Unknown → STOP, never inject.

## Procedure (orchestration only — no new injection primitive)

Half B invokes the EXISTING primitives; it writes orchestration, not new
injection code:

1. Re-check the token at the start of every step.
2. Confirm the safety verdict (step 3 above).
3. Run the existing safety-gated injector (the proven Detours/EasyHook launch-time
   path) to load the Phase-1 native runtime into the target; capture in-target
   diagnostics (runtime load + pose-timing availability) under
   `artifacts/native-m6/<slug>/`.
4. USER confirms stereo + head tracking on a headset (the native equivalent of
   M1's tier T1); record in `artifacts/native-m6/<slug>/headset-confirmation.md`.
   Not reached → honest `DONE-WITH-DEVIATIONS`.

## What Half B never does

- No new injection/hook/patch/evasion primitive is written (M6D7 — it reuses the
  existing injector).
- No anti-cheat interaction, tampering, or evasion.
- Nothing runs without the token; uncertainty about the gate = treat as NOT
  cleared, STOP.

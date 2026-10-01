# Hook-Discovery Toolkit (`hookdisc`) — Design

Bolt-on Phase B2. Requirements **RE-01** (non-invasive inspection harness),
**RE-02** (read-only hook validation sandbox), **RE-05** (harness honors the
safety preflight and never evades anti-cheat), and the gating half of **RE-04**
(the hook-surface evidence artifact that unblocks adapter transform work).

This is the keystone phase of the bolt-on track. Its single product is *data*:
a validated `config/hooks/<slug>.json` hook-surface artifact. It produces no
transforms and patches no code.

## Module map

All code is additive and lives in `src/native/tooling/hookdisc/` (new CMake
targets `vr_tooling_hookdisc` + `vrclient_hookdisc`; nothing existing is
modified).

| File | Role |
|---|---|
| `hook_surface_doc.{h,cpp}` | Load / validate / promote / save hook-surface documents. Mirrors `config/schemas/hook-surface.schema.json` and `tests/native/tooling/validate_hook_surface.py`. Refuses wrong version, missing locator, blocked-without-reason, and **validated-without-complete-evidence**. |
| `inspection.{h,cpp}` (RE-01) | Read-only module enumeration, PE export-table parsing, candidate symbol search, and MSVC RTTI type-descriptor (`.?AV`/`.?AU`) scanning. |
| `observe.{h,cpp}` (RE-02) | The read-only validation sandbox: an x64 hardware-breakpoint observer (DR0–DR3 + a Vectored Exception Handler) that counts hits, captures thread ids/timestamps, computes cadence, and removes cleanly. |
| `safety_gate.{h,cpp}` (RE-05) | The hard gate: `loadGameFingerprintConfig` → `detectVersion` → `makePreflightRequest` → `runSafetyPreflight`, approving only on `SafetyVerdict::Approved`. |
| `byte_patch_detour.h` | Interface-only stub for a future byte-patching pass-through detour. **Compile-time disabled** and unimplemented; see below. |
| `hookdisc_cli.cpp` | Thin `vrclient_hookdisc` CLI over the library. |

## Why observation before detour

A hook is only "validated" (RE-02 / B2 success criterion 4) when it is proven to
be **hit at the expected cadence and on the expected thread, before any transform
exists**. There are two ways to prove that:

1. **Read-only observation** — watch the candidate address execute without
   changing it. This is what v1 does, using x64 hardware execution breakpoints
   (debug registers DR0–DR3) and a Vectored Exception Handler. Setting a debug
   register changes only the *observing thread's* register state; it never alters
   a single byte of the target's code. The handler records a hit only when an
   armed breakpoint matches (DR6 status bit **and** faulting `RIP`), but it
   consumes **every** single-step it sees — including trap-flag single-steps that
   are a resume side effect and stray ones in flight during teardown — because
   this module is the sole debug-register/single-step user in-process and a
   passed-through single-step has no other claimant and would crash the process.
   Every non-single-step exception is passed through
   (`EXCEPTION_CONTINUE_SEARCH`) so unrelated faults are never swallowed. Removal
   restores the threads' debug registers (on every armed thread and on the
   calling thread) and drains any in-flight handler before returning, leaving
   zero behavioral residue — the target stays callable because nothing was
   patched and no breakpoint remains armed. The process-global VEH itself is
   installed once and left registered (inert when no slot is armed) on purpose:
   unregistering it per-observer would race a hardware single-step already in
   flight toward the handler and turn it into an unhandled exception (crash).

2. **Byte-patching pass-through detour** — overwrite the prologue with a jump to
   a trampoline that calls the original unchanged. This is strictly more invasive
   (it rewrites executable bytes) and is **not** needed to *prove a hit*. It is
   therefore deferred and kept disabled.

Read-only-first is the safer order: we can confirm cadence/thread for a candidate
with the technique that cannot destabilize the target, and only consider the
invasive technique later, under human review, when an actual transform is wanted.

### The disabled detour

`byte_patch_detour.h` declares a `BytePatchDetour` interface but wraps its
implementation in `#if defined(VRCLIENT_HOOKDISC_ENABLE_BYTE_PATCH_DETOUR)`. That
macro is **not defined by any CMake target** in this repository, so no shipping or
tested target contains a byte-patching path. Enabling it requires explicit human
sign-off and confirmation the target is in the safety-approved set (the comment in
the header records this). `tests/native/tooling/hookdisc_static_tests.py` asserts
the guard and sign-off comment are present and that CMake never defines the macro.

## The RE-05 safety gate

Before the harness performs any in-process work against a target that is not the
current process, it must pass the existing Phase 3 safety preflight against the
target's fingerprint config. `safety_gate.cpp` runs the full chain and returns
`approved` only on `SafetyVerdict::Approved`. There is **no override or force
flag** — by design.

Autonomy rule: the harness never auto-confirms private/modded multiplayer posture
for a commercial game. The only target it can approve autonomously is the
controlled smoke host (`config/games/sample-game.json`: `controlled_smoke_target`,
`anti_cheat_risk: none`, `online_risk: offline_only`). A commercial target such as
R.E.P.O. (`config/games/repo.json`, `online_risk: private_modded_coop`) therefore
**refuses** here (`private_multiplayer_not_confirmed`) unless a human explicitly
supplies the confirmed posture via `HookSurfaceGateOptions`. Public matchmaking,
public/unknown online state, unconfirmed mod compatibility, `known_risky`/unknown
anti-cheat, and architecture/hash mismatches all refuse, inheriting the Phase 3
refusal matrix unchanged.

No evasion, ever: there is no anti-debug circumvention, no handle-hiding, no
anti-cheat tampering. If a hook were only reachable by defeating an integrity
check, it is marked **BLOCKED** in the hook-surface document and work stops.

## v1 scope and honest limitations

- **Module enumeration** works for any process id (Toolhelp snapshot, read-only).
- **Export-table parsing and RTTI scanning are current-process only.** They read
  the live, already-mapped image directly — no cross-process memory reads. Remote
  export/RTTI parsing is deferred to a later phase that performs it under the live
  safety gate. This keeps the v1 inspection surface maximally transparent.
- **The observation sandbox runs in the current process.** It arms every thread of
  the current process except the calling thread (the validation harness drives the
  target from worker threads). Live external observation of the smoke host /
  R.E.P.O. is the next increment, gated by `safety_gate`.
- Platform: x64 Windows only (hardware debug registers). That is the only target
  platform for this tooling.

## What live R.E.P.O. capture will add later

The v1 artifact `config/hooks/repo.json` carries the candidate camera/projection/
HUD/input/interaction names and the network block markers as `status: candidate`,
`confidence: low`, with no `validation` block. The live-capture increment will,
under the safety gate, attach to an approved private/modded co-op or offline
R.E.P.O. session and:

1. Resolve each candidate name to a real module + export/RVA/signature using the
   inspection harness against the live image.
2. Arm a hardware breakpoint on each resolved call site and observe it for a
   window, capturing hit count, thread context (render/main/worker), and observed
   cadence (cross-checked against the Phase 1 frame-timing cadence).
3. Promote a candidate to `status: validated` via `promoteHookToValidated`, which
   refuses unless the evidence is complete (method, thread context, `hit_count ≥ 1`,
   `captured_at`, and a positive `cadence_hz` or a `per_frame` flag).
4. Mark any site reachable only by defeating an integrity check as `blocked` with
   a reason, and stop.

The resulting validated `config/hooks/repo.json` is the artifact that unblocks the
R.E.P.O. adapter's Task 5 work (ADAPT-02..ADAPT-06) — the adapter consumes the
data; it is never compiled in.

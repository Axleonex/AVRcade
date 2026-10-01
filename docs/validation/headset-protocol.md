# Headset Validation Protocol (B3) — first real Quest bring-up

**Audience:** the user, running on a machine with a Meta Quest in PCVR mode and an
**active OpenXR runtime**. This is the step-by-step for the first real headset run
of the VRClient runtime's OpenXR session / swapchain / present path.

> **Read this first — honesty boundary.**
> The harness has been **built** and **headless-graceful-verified** on the
> agent/CI machine (no headset, no OpenXR runtime there): it compiles, links the
> Release OpenXR loader, runs, detects the missing runtime at
> `xrCreateInstance`/`xrGetSystem`, records a clear blocker, and exits cleanly
> (exit code 10) **without crashing or hanging**. That is the *only* thing
> verifiable without hardware, and it ticks **no** CORE/DIAG box.
>
> The OpenXR **session / swapchain / present** path and the **headset render**
> have **never executed against a real runtime**. Your run is the **first
> execution of never-run code**. Treat the first attempt as a bring-up /
> debugging session, not a pass/fail gate — **iteration is expected** (see
> [§7 Known watch-items](#7-known-watch-items-first-run-bugs-we-already-flagged)).
> Real headset evidence for **CORE-01..CORE-05, CORE-07, DIAG-03** is produced
> only by your run on the Quest and is reviewed afterward — nobody has ticked
> those boxes from the agent side.

---

## 1. Prerequisites

You need a PC VR stack that exposes an **active OpenXR runtime** to desktop apps,
with the Quest connected and presenting to it. Any one of these works:

| VR stack | How the Quest connects | Sets the active OpenXR runtime |
|---|---|---|
| **Meta Quest Link** (Air Link or cable) | Quest -> Meta desktop app | Meta sets itself active (Settings -> Beta -> "Set as active runtime"), or via Oculus app |
| **Virtual Desktop** (with the VDXR runtime) | Quest VD app -> VD Streamer | Select **VDXR** as the OpenXR runtime in the VD Streamer |
| **SteamVR** | Quest via Link / Air Link / VD, then SteamVR | SteamVR -> Settings -> "Set SteamVR as OpenXR runtime" |
| **ALVR** | Quest ALVR app -> ALVR server (+ SteamVR) | ALVR drives SteamVR; SteamVR is the active runtime |

Checklist before you run:

1. **Headset is on, donned, and showing the VR home/dashboard of your stack** —
   not asleep, not in the guardian-setup screen. A sleeping/un-donned headset is
   the most common cause of a "started but 0 frames" result (exit 13).
2. **Exactly one OpenXR runtime is active.** If two stacks fight over "active
   runtime" (e.g. both SteamVR and Meta claim it), set one explicitly and close
   the other. The harness talks to whichever runtime is registered active.
3. **GPU driver + Vulkan present** (you already have this on the build machine).
4. **The OpenXR loader is bootstrapped** — `external/openxr` must contain
   `OpenXRConfig.cmake`. If not, run `tools/build-openxr.ps1` once first; the
   launcher hard-errors with that instruction if it is missing.
5. You are at the **repo root** in **PowerShell**.

> Internet is **not** required and **not** used by the run. No injection happens.
> The harness only opens an OpenXR session and renders the built-in test scene to
> your headset — it does not touch any game.

---

## 2. The one command

From the repo root, in PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run-headset-smoke.ps1 -Seconds 10
```

That single launcher does everything:

1. Imports the build environment (`tools/setup-build-env.ps1`).
2. CMake-configures + builds the **`vrclient_headset_smoke`** target with
   **`-OpenXR ON` (Release)** into `build/headset/` (a separate build dir, so it
   never clobbers your default `build/ci` cache).
3. Runs the harness for the bounded duration (default 10s), under an **outer
   process-timeout guard** (`Seconds + 30`) so nothing can ever wedge.
4. Writes the evidence to `artifacts/headset/<timestamp>/` (see [§4](#4-what-the-run-produces--where-the-evidence-lands)).
5. Prints a short verdict (`RAN` / `NO-RUNTIME` / `STARTED-NO-FRAMES` / ...) and
   the artifact path.

Useful options (all optional):

| Flag | Default | Meaning |
|---|---|---|
| `-Seconds <n>` | `10` | wall-clock deadline for the bounded frame loop |
| `-Frames <n>` | `0` (unbounded by count) | stop after N rendered frames instead |
| `-Compiler <msvc\|clang-cl\|auto>` | `auto` | mirrors `build-and-test.ps1` |
| `-BuildDir <path>` | `build/headset` | CMake binary dir |
| `-ProfilePath <path>` | `config/defaults/runtime-profile.json` | runtime profile |
| `-ProcessTimeoutSeconds <n>` | `Seconds + 30` | outer kill-guard |

---

## 3. What to look for

### In the headset (the thing only you can confirm — CORE-04)

While the harness runs (10s by default), **look in the Quest**:

- **The built-in test scene must appear in BOTH eyes** — this is **CORE-04**, the
  single piece of evidence that cannot be captured in any file. Confirm:
  - the scene is visible in **left and right** eyes (not one eye black);
  - it is **stereo-correct** (slight per-eye offset, not a flat duplicate);
  - **poses are stable** — turning your head moves the view smoothly, no
    swimming/lag/judder;
  - no obvious tearing, corruption, or single-color flashes.
- If you see nothing but the scene rendered fine in the launcher's verdict, note
  that — it points at the swapchain layout watch-item ([§7](#7-known-watch-items-first-run-bugs-we-already-flagged)).

> **Why your eyes matter:** the harness can report `RAN` (exit 0, >=1 frame
> submitted to both eyes) without knowing whether the pixels were *correct*.
> "Rendered to both eyes" in the JSON means frames were submitted; only you can
> confirm they looked right. The verdict explicitly says **"NEEDS USER EYE
> CONFIRMATION."**

### In the printed verdict (Step 6 of the launcher)

| Verdict | Exit | Meaning / next step |
|---|---|---|
| `RAN` | 0 | Session ran and submitted >=1 frame to both eyes. **Confirm with your eyes** (above). This is the path that produces CORE-01..05, CORE-07, DIAG-03 evidence for review. |
| `NO-RUNTIME` | 10 | No active OpenXR runtime / no headset detected at `xrCreateInstance`/`xrGetSystem`. This is the **expected agent/CI result** — if you get it on your Quest machine, your runtime is not active or the headset is not connected. Re-check [§1](#1-prerequisites). |
| `STARTED-NO-FRAMES` | 13 | A runtime + system were found, the session started, but **0 frames** rendered in the window (every tick returned SKIPPED). Usual cause: **headset asleep / not donned**, no `XR_SESSION_STATE_READY` event, or the compositor never focused the session. Don the headset and re-run. **This is NOT a render to both eyes.** |
| `GRAPHICS-BLOCKER` | 11 | Vulkan device / renderer init failed (driver/SDK). Check the staged `vulkan-1.dll` next to the exe and the log. |
| `PROFILE-BLOCKER` | 12 | The runtime-profile JSON was missing/invalid. Pass a valid `-ProfilePath`. |
| `TIMEOUT` | 124 | The harness did not exit within `Seconds + 30` and was killed. **Unexpected** — the loop is bounded; capture and report the artifacts. |
| `FAILURE` | 1 (other) | Generic create/frame-loop error — see evidence + log. |

### In the evidence JSON (for later CORE/DIAG review)

Open `artifacts/headset/<ts>/session-evidence.json` (schema
`vrclient-headset-smoke/2`) and check, against the requirement it maps to:

- **`state_transitions[]`** — the session lifecycle (`stopped -> initializing ->
  ready -> running -> ...`). This is **CORE-01** (lifecycle) and **CORE-05**
  (any disconnect / loss-pending / restart you induce — see [§6](#6-deliberately-exercising-core-05-disconnect--lost-session)). A healthy live run shows
  progress past `initializing` into `ready`/`running`, not the headless
  `initializing -> error(-4) -> stopped` you see on a no-runtime host.
- **`headset.per_eye[]`** + `view_count_is_stereo` + `current_refresh_hz` — per-eye
  recommended dimensions and refresh. Partial **CORE-02** (dimensions). The
  per-eye **format + image count** are NOT in this JSON by design (the public ABI
  is graphics-agnostic) — they are in the runtime log (next bullet).
- **`frames{rendered, skipped, missed, min/avg/max_frame_ms, loop_wall_seconds}`** —
  host-measured frame count + timing. Toward **CORE-03 / CORE-07**.
- **`overlay_snapshot_host_reconstructed`** — **DIAG-03**, a host-reconstructed
  overlay-equivalent snapshot (runtime state, frame index, frame time, dynamic
  resolution, foveation). The true in-runtime overlay snapshot
  (`openxr_session_state` string + `recent_warnings`) is in the runtime log.
- **`honesty` block** — restates what the run does / does not verify. Read it.

### In the copied runtime log (`vrclient-runtime-*.jsonl`)

The launcher copies the runtime's own diagnostics log next to the JSON. Grep it
(or open it) for:

- `openxr_runtime_identity` / `openxr_system_identity` — **runtime name +
  system/headset name** (identity for **CORE-01**).
- `openxr_swapchain_created` — per-eye `{eye, format, image_count, w, h}` (the
  **CORE-02** format + image-count that are not on the public ABI).

The harness's own log is `vrclient-smoke-*.jsonl` (state transitions + `smoke_*`
events only).

---

## 4. What the run produces / where the evidence lands

```
artifacts/headset/<UTC-timestamp>/
  session-evidence.json              # structured evidence (schema vrclient-headset-smoke/2)
  stdout.log                         # harness stdout + (--- stderr ---) merged
  vrclient-smoke-*.jsonl             # this harness's state-transition + smoke_* events
  vrclient-runtime-*.jsonl           # the runtime's identity + swapchain_created lines (CORE-01/02)
```

The launcher prints the exact `artifacts/headset/<ts>/` path at the end. Only the
logs from **this run's time window** are copied (older logs are filtered out so
it is unambiguous which log backs which JSON).

> **This is a separate evidence lane.** The launcher deliberately does **not**
> append to `.planning/evidence/LEDGER.md` and does **not** write a
> `vrclient-ci-run` JSON. The headset smoke evidence is its own
> `artifacts/headset/` lane — it never poses as the OpenXR-OFF green CI gate.

---

## 5. How to capture / share the evidence

After a run (any verdict):

1. Note the printed `artifacts/headset/<ts>/` directory.
2. Zip the whole timestamp directory and share it (it is self-contained —
   evidence JSON + stdout + both `.jsonl` logs):

   ```powershell
   Compress-Archive -Path artifacts\headset\<ts>\* -DestinationPath headset-evidence-<ts>.zip
   ```

3. Add a one-line note of what you **saw in the headset** (both eyes? stereo?
   stable poses? any visual glitch?) — that is the CORE-04 evidence that is not in
   any file.

The agent/orchestrator side reviews these to decide which CORE/DIAG boxes the run
actually supports. **Do not tick boxes yourself** — share the artifacts + your
eye-confirmation note.

---

## 6. Deliberately exercising CORE-05 (disconnect / lost session)

CORE-05 is "session-state transitions including headset disconnect /
lost-session / restart." To produce that evidence, run a longer window and induce
a disconnect mid-run:

1. Start with a longer duration so you have time to act:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File tools\run-headset-smoke.ps1 -Seconds 30
   ```

2. Once you confirm the scene is rendering in the headset, **mid-run** do ONE of:
   - **take the headset off** (proximity sensor -> session loses focus/visibility), or
   - **stop the VR runtime** (quit SteamVR / VD Streamer / Quest Link) while the
     harness is still looping, or
   - **disconnect the link** (turn off Air Link / unplug the cable).

3. **Expected graceful behavior:** the harness must **not crash or hang**. The
   session-state machine should record the transition (e.g. into
   `loss_pending` / `exiting` / `error`) in `state_transitions[]`, the loop should
   break on a terminal state (or ride the bounded deadline), and the process
   should exit cleanly with the evidence written. The outer process-timeout guard
   (`Seconds + 30`) is the backstop if a wedged compositor blocks inside a present
   call.

4. Check `state_transitions[]` in the JSON for the disconnect transition — that is
   the CORE-05 evidence. If the harness crashed or hung instead, that is a
   bring-up bug to report (capture the artifacts + the stdout).

> Re-don the headset / restart the runtime afterward to test the **restart** leg
> if your stack re-offers the session.

---

## 7. Known watch-items (first-run bugs we already flagged)

This is the **first execution** of the session/swapchain/present path, reviewed
statically but never run. Watch for these on your first attempt — they are the
most likely first-run failure points (none is a confirmed bug; they are flagged
for your iteration):

1. **Swapchain image layout for present (highest-probability).** The render path
   was validated headless against a self-owned offscreen image with a known
   initial layout. The real OpenXR swapchain image's layout post-acquire is
   runtime-defined. The render pass's color-attachment initial/final layouts are
   correct **by construction** (no missing transition was found in review), but
   this is the single most likely place a real runtime surfaces a Vulkan
   validation/layout error. **If the headset shows black/garbage but the verdict
   is `RAN`, suspect this first** and capture the runtime log.
2. **`STARTED-NO-FRAMES` (exit 13) loops.** If the session starts but never
   reaches a begun/focused state, every tick returns SKIPPED (the harness sleeps
   10ms between ticks) and the wall-clock deadline bounds the run. Cause is
   usually the headset asleep/un-donned or no `READY` event — re-don and re-run.
3. **Frame-timing fidelity.** `missed_frames` is a coarse 0-or-1 aggregate (not a
   per-frame miss count), and the refresh budget comes from the **profile**, not
   from the runtime's predicted display period (that field is not yet populated).
   Treat the timing numbers as host-measured wall-clock estimates, not
   runtime-reported pacing.
4. **In-`run_frame` hang.** The `-Seconds` deadline is only checked **between**
   frames; a wedged compositor could block **inside** a single present call. The
   launcher's outer process-timeout (`Seconds + 30` -> Kill -> exit 124) is the
   real no-hang backstop. **Run via the launcher, not the exe directly**, to get
   that guard.

If your first run fails, that is normal for never-run code. Capture
`artifacts/headset/<ts>/` + the stderr in `stdout.log` and share it — the
session/present path can then be iterated against real-runtime evidence.

---

## 8. Honesty ledger note (do not skip)

- **Built + headless-graceful-verified is NOT headset-verified.** The agent-side
  run only proves the harness builds, links the Release OpenXR loader, runs,
  detects "no runtime/no headset," writes a structured blocker, and exits cleanly
  (exit 10, no crash/hang).
- **No CORE/DIAG box is ticked by any agent-side run**, and the launcher does not
  touch `.planning/evidence/LEDGER.md`.
- **CORE-01..CORE-05, CORE-07, and DIAG-03 evidence is produced only by your run**
  on a Quest with an active OpenXR runtime, and **CORE-04** specifically needs
  your **eyes** on the headset. The first such run is **expected to need
  iteration**.

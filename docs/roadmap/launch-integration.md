# Roadmap: Launch integration (post-M1.4)

Captured 2026-07-09 from user product direction. Three separable items, in
dependency order. PROMOTE into `.planning/ROADMAP.md` once M1.4 (SteamVR fix)
closes — `.planning/` is inside the M1.4 plan's scope fence until its
durability review passes, so it must not be edited before then.

Product context: user wants (a) SteamVR as a launch path partly as a bet on a
SteamOS future, and (b) VRClient to coexist with mod managers (r2modman,
Thunderstore Mod Manager) so modded games launched from those clients still
get VR conversion + the M1.4 runtime pin.

## LI-1: Steam launch-option wrapper

**What:** A `vrclient wrap -- %command%` CLI mode. User (or `convert`) sets the
game's Steam Launch Options to `"<path>\vrclient" wrap -- %command%`. On any
Steam-routed launch (Steam UI, SteamOS game mode, mod clients that launch via
Steam), the wrapper applies the per-launch work — runtime cfg pin (M1.4
Branch A), safety re-check, comfort refresh, session evidence — then execs the
original command line unchanged, preserving other wrappers' and mod managers'
arguments (`%command%` stacking).

**Why first:** It is the universal Steam-routed interception point; LI-2 and
LI-3 both assume it exists.

**Acceptance:** launching R.E.P.O. from the Steam UI (not from vrclient) with
the wrapper installed produces `BepInEx/LogOutput.log` binding the pinned
runtime, and removing the launch option restores stock behavior. Wrapper adds
<2 s to launch. `dotnet test` stays green.

**Notes:** never persist machine state; all work per-launch, mirroring the
M1.4 pin design. Must tolerate `%command%` containing doorstop/injection args
from mod managers.

**Robustness (doublecheck, 2026-07-09):**
- *Process lifetime:* Steam derives playtime/overlay/running-state from the
  process it spawned — the wrapper must spawn the real command and WAIT on it
  (or exec-replace itself), never fire-and-exit.
- *Fail-open:* any wrapper-internal error (pin write fails, cfg missing, game
  not converted) logs and still launches `%command%` unmodified. The wrapper
  must never be able to block a flat launch.
- *Unconverted games:* wrapper on a game with no RepoXR install = silent
  passthrough (it is safe to set the launch option before converting).
- *Launch-option install:* AUTOMATED 2026-07-10 (user directive: no manual
  paste). `vrclient wrap-install [slug] [--remove|--dry-run]` edits the active
  account's `localconfig.vdf` itself: byte-preserving VDF splice, timestamped
  backup, gracefully shuts down a running Steam first (`steam.exe -shutdown`,
  it rewrites the file from memory on exit) and relaunches it after,
  idempotent, composes with pre-existing launch options (wraps an existing
  `%command%` chain; appends plain args after `%command%`).

**Flat-play toggle (user requirement, 2026-07-09):** the wrapper must never
force VR. Behavior: a `vr: on | off | auto` setting (default `auto`) surfaced
in the app and CLI. In `auto`, if no VR runtime is live (no VD streamer, no
SteamVR) the wrapper skips the pin and passes RepoXR's `--disable-vr` arg —
clean flat launch, no "VR startup failed" popup, no user action; the headset
being off IS the toggle. `off` forces the same regardless of runtimes; `on`
pins as normal. (RepoXR's flat fallback + `--disable-vr` verified in the
Phase 1 source findings, Q4.)

## LI-2: SteamOS / Linux port

**What:** linux-x64 publish of client + CLI (Avalonia and .NET 8 already
support it), port of the Windows-only runtime discovery
(`OpenXrRuntimeSelector` probes HKLM + `Program Files`), and a
diagnose-first investigation phase (same method as the SteamVR fix plan:
evidence, then experiments, then one fix branch) for the Proton boundary —
how a Windows Unity game under Proton resolves OpenXR manifests against
SteamVR-on-Linux, and whether the RepoXR cfg pin (a file write, OS-agnostic)
needs prefix-path translation.

**Why second:** rides LI-1's wrapper (the launch-option slot is the canonical
SteamOS integration pattern) and the M1.4 cfg-pin mechanism, which was chosen
over a Windows-registry switch precisely because it transfers.

**Acceptance:** phase-gated; first milestone is the investigation report with
a decision table, not a port. Hardware gate: requires a SteamOS/Linux test
machine + headset.

**Corrected assumption (doublecheck, 2026-07-09):** Virtual Desktop has no
Linux/SteamOS host — the VDXR runtime does not exist there. SteamOS streaming
stacks are Steam Link (SteamVR), ALVR, WiVRn/Monado. Consequence: SteamVR is
the PRIMARY runtime on SteamOS (strengthens the M1.4 SteamVR-first bet), and
the LI-2 investigation must survey the Linux runtime landscape from scratch
rather than porting the Windows VD/SteamVR pair 1:1.

## LI-3: Mod-manager coexistence (install-layer dedup)

**What:** Detect a mod-manager-profile launch (doorstop args in `%command%`
pointing outside the game dir) and avoid double-installing the VR stack:
either (a) skip our game-dir BepInEx/mod copy and pin-only, if the profile
already provides the VR mod, or (b) install the VR mod into the profile
instead of the game dir. Decide (a) vs (b) in a short discuss phase; (a) is
the smaller diff.

**Why third:** the conflict only becomes user-visible once LI-1 makes mixed
launches easy; needs LI-1's arg inspection anyway.

**Acceptance:** a game with an r2modman profile containing RepoXR, launched
from r2modman, boots with exactly one RepoXR instance loaded (LogOutput.log
shows a single `Starting RepoXR` and no duplicate-plugin warnings) and the
runtime pin applied. A game-dir-converted game with NO profile keeps current
behavior.

**Detection, not interrogation (user question, 2026-07-09):** identify the
mod manager from the `%command%` doorstop args themselves (r2modman /
Thunderstore Mod Manager / Gale inject profile paths that name the manager) —
auto-detect, surface the verdict in `doctor` and the app UI ("Detected:
r2modman, profile 'Default'"), and prompt the user only when the evidence is
ambiguous. Manager fingerprints are data (like the game catalog), so adding a
new manager is a data change, not code.

**Trust boundary (doublecheck, 2026-07-09):** when the VR mod is provided by
the mod manager's PROFILE (dedup option (a)), it is their copy, not our
hash-pinned install — the M5 safety gate and lockfile verification currently
assume our copy. LI-3 must extend verification to profile-provided mods
(hash-check the profile's RepoXR against known Thunderstore releases; verdict
`Warn` on unknown hashes) rather than silently skipping the safety engine.

**Known limit (documented, not solved):** launchers that spawn the game exe
directly without Steam bypass launch options and therefore the wrapper.

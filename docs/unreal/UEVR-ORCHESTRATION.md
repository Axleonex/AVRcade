# ECO-2 / T-Unreal: UEVR orchestration — investigation (2026-07-10)

> **STATUS: LEGACY UEVR FALLBACK / HISTORICAL RESEARCH.**
> Do not use this document to implement or verify the current native Meccha
> route. Start with `PASSOVER-NATIVE-MECCHA.md` and
> `docs/unreal/NATIVE-ARCHITECTURE.md`.

Goal: extend VRClient to Unreal Engine games by orchestrating praydog's UEVR
(universal UE 4.8–5.4 VR injector) the same way T-Unity orchestrates RepoXR.
First candidate target: MECCHA CHAMELEON (Steam app 4704690, UE5, June 2026).

## Verified facts

- **License:** UEVR's LICENSE is "Copyright (c) 2022-2025 praydog. All rights
  reserved." — NOT an open license. Consequences (hard constraints):
  - We may NEVER bundle, redistribute, or vendor UEVR binaries or source.
  - Orchestration = the USER'S machine downloads the official release from
    praydog's own GitHub releases at convert time (same posture as our BepInEx
    auto-fetch). If the author ever objects, we honor it immediately.
  - No source copying/porting. (We don't need any — we run the tool, we don't
    link it.)
- **Release channel:** `praydog/UEVR` latest stable = **1.05** (2024-11-16),
  assets `UEVR.zip` (7.4 MB) + **`UEVR.zip.sha256`** — the author ships a
  sha256 sidecar, which slots directly into our hash-verified download
  pipeline (pin tag + expected hash in catalog data). A `UEVR-nightly` repo
  exists for newer engine support.
- **Engine-version risk:** stable 1.05 supports UE 4.8–5.4. MECCHA CHAMELEON
  is a June-2026 UE5 title — its exact engine version is UNKNOWN until we can
  read the installed exe metadata. If it is UE 5.5+, the nightly channel (or
  waiting for the next stable) is required. FIRST CHECK after install.
- **Meccha Chameleon status:** NOT installed on this machine (no
  appmanifest_4704690.acf) — the live test is gated on the user installing it.
  Safety profile: no anti-cheat (defensive research 2026-07-10), EOS
  matchmaking, public lobbies AND private friend rooms; under the 2026-07-10
  safety default it authors as `online_risk = private_modded_coop` →
  Warn + acknowledge. Public-lobby play with mods stays the user's explicit
  exception (cheater-report climate in that game is hostile).

## How UEVR works (orchestration model)

UEVR is an **injector**: the game runs first (normal launch), then
UEVRInjector attaches to the running process and loads the VR runtime into it.
This differs from BepInEx (loads at boot via doorstop). Orchestration shape:

1. `convert <slug>`: download `UEVR.zip` from the pinned official release,
   verify against the published sha256, unpack to a VRClient-managed tools dir
   (NOT the game dir). Install the game's Steam launch option (existing LI-1
   machinery — the wrapper still owns runtime selection/flat toggle).
2. `launch <slug>` / wrapper path: start the game normally, wait for the
   process, then invoke the injector against it with the game's UEVR profile.
   Injection CLI options to evaluate at build time: UEVRInjector.exe
   command-line support in current releases vs. the community CLI injector
   (`keton/chihuahua`) vs. asking the user to click Inject in the UEVR
   frontend for v1 (zero-code fallback).
3. Per-game data: UEVR uses per-game profiles (rendering method
   native/synced/AFR, controller bindings, camera offsets). These are the
   T-Unreal analog of comfort maps — catalog data, shareable.
4. Safety gate: unchanged — same verdict engine, same acknowledge flow.

## What T-Unreal needs from the catalog (additive schema)

- `engine: "unreal"` field on the game config (additive; absent = unity).
- A `uevr` block: pinned release tag + sha256, rendering method, profile data.
- The existing `executable_names`, app id, safety fields work as-is.

## Readiness verification (2026-07-11) — everything not gated on the game is DONE

- **Fetch path proven live:** UEVR.zip (1.05) downloaded from the official
  release and sha256-verified against the published sidecar —
  `af4f2f91306802d7ee4e8497d483a547ac8e9a3067dbafb81324100524215d3c` (match).
  Implementation note: the sidecar is UTF-16LE with BOM and UPPERCASE hex.
- **Zip contents inspected:** `UEVRInjector.exe` (frontend), `UEVRBackend.dll`
  (the injected VR runtime), `UEVRPluginNullifier.dll`, `openvr_api.dll`,
  `openxr_loader.dll`, DISCLAIMER (standard no-warranty/non-affiliation — no
  extra use restrictions), LICENSE, plugin headers. Backend revision
  `1a810f69fe4c...`.
- **CLI automation confirmed viable:** the 1.05 injector binary contains an
  `--attach` command-line flag (strings-verified) — full launch→attach
  orchestration is possible without GUI clicks. Exact argument syntax to be
  confirmed against a running UE game at build time.
- **Catalog data authored:** `config/unreal/uevr-release.json` (pinned
  tag/hash/URL/license constraint) + `config/unreal/meccha-chameleon.uevr.json`
  (app id, safety posture per the 2026-07-10 private-co-op default, gates).
  Kept in a SEPARATE `config/unreal/` namespace on purpose: the
  game-fingerprint schema is `additionalProperties: false`, so `engine`/`uevr`
  fields on `config/games/*.json` must go through the B5 schema-versioning
  procedure at build time, not be hacked in.

## Remaining gates (all need the game)

1. User installs MECCHA CHAMELEON (Steam app 4704690).
2. Fill `executable_names` from the real install; read its UE version →
   decide stable 1.05 vs nightly.
3. Build the orchestration increment: `uevr-fetch` (download+verify+unpack per
   uevr-release.json) + launch→`--attach` flow + game-fingerprint schema bump.
4. Headset session: stereo + head tracking in a PRIVATE room = pass tier T1.

## BUILD COMPLETE + live-verified (2026-07-11)

Shipped: `UevrTools` (Core, release/game data layer) + two CLI verbs.

- **`uevr-fetch [--channel stable|nightly] [--force]`** — downloads the pinned
  release from the official praydog GitHub release, sha256-verifies (fail-closed
  on mismatch), unpacks to `%LOCALAPPDATA%\vrclient\tools\uevr\<tag>`. Live: the
  UE-5.6 nightly (38 MB) downloaded + verified + unpacked GREEN.
- **`uevr-launch <slug> [--acknowledge] [--dry-run] [--attach-delay N]
  [--attach-timeout N]`** — safety gate → auto-fetch if needed → `steam
  -applaunch <appid>` → wait for the SHIPPING process (not the launcher stub)
  → attach `UEVRInjector.exe --attach <proc>.exe`. Live against Meccha
  Chameleon: safety correctly refused without `--acknowledge`
  (private_modded_coop Warn); with it, Steam launched the game, the shipping
  process `PenguinHotel-Win64-Shipping` was detected (pid), and the injector
  was invoked with the right args. The `--attach` flag + `<proc>.exe` syntax
  is the confirmed CLI form.

**ONE MACHINE PREREQUISITE FOUND:** UEVRInjector.exe is a framework-dependent
**.NET 6 Desktop Runtime (x64)** app. This machine has 8.0/9.0 but not 6.0, so
the inject step returns `error=uevr_injector_dotnet_missing` with the download
link (https://dotnet.microsoft.com/download/dotnet/6.0, Desktop Runtime).
Verified this is a genuine dependency, not env pollution — the injector is
spawned with DOTNET_ROOT/multilevel-lookup env stripped (same env-hygiene
lesson as the Steam relaunch bug) and still requires 6.0. FUTURE (deployment
polish): VRClient could detect + offer to fetch the .NET 6 desktop runtime,
matching the plug-and-play principle. For now it is a documented prerequisite.

**Remaining gate (user):** install .NET 6 Desktop Runtime, then the headset QA
session (private room; T1 = stereo + head tracking). That run also verifies
whether the June nightly actually handles UE 5.6.

## FIRST VR CONFIRMATION + attach-syntax correction (2026-07-11, user session)

- **UE 5.6 WORKS with the pinned June nightly** — the user injected (manually,
  via the frontend GUI) into the running game and confirmed the headset view
  renders. Tier T1 substantially met; MECCHA CHAMELEON is the first Unreal
  title through the pipeline.
- **Attach syntax corrected from frontend source** (uevr-frontend
  MainWindow.xaml.cs, read-only): the ONLY parsed argument is `--attach=<exe>`
  (equals form, one argument). Our original two-argument form was silently
  ignored — which is why the GUI opened unattached pointing at an arbitrary
  process. With the equals form the frontend WAITS for the process,
  auto-injects when it appears, and exits itself when the game closes
  (resident-while-game-runs matches our success detection).
- **Runtime selection (OpenVR vs OpenXR radio) has NO CLI argument** — it is a
  persisted .NET user setting. It defaulted to OpenVR; the user switched to
  OpenXR (correct for Virtual Desktop/VDXR) and that choice persists per
  machine. Deployment polish (future): pre-seed the frontend's user.config so
  fresh machines default to OpenXR; until then it is a one-time radio click on
  first use per machine.
- **SteamVR support:** yes, two ways. OpenVR mode = SteamVR ALWAYS (UEVR ships
  openvr_api.dll; works for native SteamVR headsets and over VD's SteamVR
  mode). OpenXR mode = the system's active OpenXR runtime (VDXR here; SteamVR
  if set active in SteamVR Settings → Developer). Guidance: Quest-via-VD users
  stay on OpenXR/VDXR (shortest pipe, lowest latency); OpenVR is for native
  SteamVR headsets or SteamVR-feature needs. Asymmetry vs the Unity track:
  RepoXR gets a per-launch runtime pin from us; UEVR's runtime is the
  persisted radio — no posture-based auto-switch per launch.
- **Controllers**: UEVR's generic layer presents motion controllers as an
  emulated XInput GAMEPAD. Game menus that expect a mouse pointer do not
  respond to "laser pointing" — navigate with the emulated gamepad (sticks +
  A/B) or the desktop mouse. Full motion-control mappings are per-game UEVR
  profile work (tier T2), shareable as catalog data.

## T2: per-game profiles — the tune-once, ship-as-data loop (built 2026-07-11)

**Product standard (user directive 2026-07-11): VR profiles default to
FIRST-PERSON**, even for third-person games, with third-person reachable via
UEVR's camera controls where the game allows. Tool-aim (e.g. MC's paintbrush)
follows the motion controller, with the game's own targeting preview — or a
UEVR Lua script beam — showing where the action lands.

Plumbing:
- `uevr-launch` deploys `config/unreal/profiles/<slug>/` into UEVR's per-game
  dir (`%APPDATA%\UnrealVRMod\<ShippingProcess>\`) — copy-if-missing so
  in-headset tuning is never clobbered; `--push-profile` forces the catalog
  version.
- `uevr-profile-export <slug>` captures the tuned per-game dir back into the
  catalog. Loop: tune in-headset once → export → commit → every future launch
  (and every future user) gets it by default.

In-headset tuning steps for MECCHA CHAMELEON (do these inside the UEVR
overlay — open it with the Insert key on desktop or press L3+R3 together in
VR):
1. **First-person:** Camera section → adjust Camera Offset (X/Y/Z) until the
   view sits at the character's head; or Advanced → UObjectHook → find the
   head/camera component → attach camera. Enable Decoupled Pitch for comfort.
2. **Brush follows hand:** Input section → Aim Method → **Right Controller**
   (game cursor/paint targeting now tracks the hand; the game's paint preview
   becomes the landing indicator).
3. Optional: UObjectHook → select the brush/tool actor → Attach to right
   controller, so the visible brush model rides the hand.
4. Overlay → Save/Export profile, then run `uevr-profile-export
   meccha-chameleon` and commit — the tuning becomes the catalog default.

A literal VRChat-style beam from controller to paint-landing point, if the
game's own preview proves insufficient: UEVR nightly Lua scripting (a script
in the profile's `scripts/` dir draws the line each frame) — polish pass,
after the aim-method result is assessed in-headset.

## Reliability fix: elevation + file-signal verification (2026-07-11)

Symptom: `uevr-launch` opened the injector but the game never hooked
(config.txt never gained `Frontend_RequestedRuntime`, no backend files).

Root cause (evidence, not guess): the game process **blocks module
enumeration from a non-elevated caller** — a normal-integrity read returns
ZERO modules (window title still readable). UEVR's frontend decides a process
is injectable by reading its modules for `d3d11/d3d12.dll`
(`IsInjectableProcess`, source-verified) and injects via the same access. Run
non-elevated, that check silently fails for every poll → it never injects.
Our orchestration launched the injector non-elevated. The user's earlier
manual success was with UEVR elevated (its UI prompts "Restart as Admin").

Fixes in `uevr-launch`:
- **Launch the injector elevated** (`Verb=runas` → one UAC prompt). Declined
  UAC (Win32 1223) → clean `error=uevr_uac_declined`.
- **Verify via a file signal, not module reads** (which we also can't do): poll
  `%APPDATA%\UnrealVRMod\<proc>\config.txt` for `Frontend_RequestedRuntime`,
  which the frontend writes ONLY after a successful inject. Reports
  `backend=hooked` on success; actionable error otherwise.
- Readiness gate no longer reads D3D modules (impossible non-elevated) — waits
  for window title + a settle.

Cost: one UAC prompt per launch (inherent to injecting a protected game).
Fallback for user-friendliness if auto-attach is ever flaky on a title: the
launcher still fetches/profiles/launches and opens the injector pre-pointed at
the game via `--attach=`, so the worst case is a single manual "Inject" click,
never process-hunting.

## Honest expectations

UEVR gives camera-level VR (stereo, head tracking, optional first-person) on
hundreds of UE games generically; motion-controller depth varies per game and
per profile. For a hide-and-seek game like MC, T1 (look around in VR, play
with mouse/kb or gamepad) is the realistic first milestone — full motion
controls would be a per-game profile effort, exactly like RepoXR's per-game
interaction work on the Unity side.

# Mod-manager coexistence (r2modman / Thunderstore Mod Manager / Vortex)

LI-3, built 2026-07-10. How VRClient plays VR together with a mod manager's
profile mods, and what (little) a future game needs for this to work.

## Desktop app route (2026-09-29)

For R.E.P.O., Lethal Company, PEAK, Content Warning, and Big Walk, the desktop app defaults to its
own game-directory VR conversion. Users can install that conversion and launch
from the same page. Mod discovery and additional mod installation belong in
the selected external manager's own mod pages; AVRcade no longer presents a
mod-specific search or optional-mod install action. Existing AVRcade-installed
add-ons remain recognized for launch, but no files are removed by this UI change.
Beside **Set up VR with mods**, AVRcade identifies the exact VR package and
publisher for the selected game, shows instructions that change with the chosen
manager, and links to the creator's Thunderstore package page. r2modman and
Thunderstore Mod Manager users find the named package in that manager's mod
pages. Vortex users are directed to verify game and package-layout support
before importing the creator's archive and its dependencies; AVRcade does not
assume the package appears in Vortex's Nexus catalog.
An external manager profile is an optional alternate launch route.
Choose and save one only when using that manager's own mods. AVRcade reads the
external profile but does not install into or modify it.
If a data folder was moved, expand **VR with mods details** or **Mods, no VR details**,
then use its **Find moved profiles** control.

The game itself remains Steam-owned. AVRcade does not download or copy a game;
**Open game in Steam** opens its store page, while **Locate installed Steam game**
accepts an existing `steamapps/common/<game>` folder only when its Steam app
manifest and expected executable match. The chosen Steam library is remembered
for subsequent scans. **Get r2modman/Vortex/Thunderstore Mod Manager (official site)** opens
the respective official download page; AVRcade does not silently install those
applications. **Locate installed manager .exe** remembers an existing r2modman
or Vortex executable. Manager availability does not prove support for a
particular game.

**Install VR setup** downloads the credited community conversion and
dependencies into the detected Steam game folder. Additional mod browsing and
installation happen in the manager's own pages, not an AVRcade search bar.
Existing AVRcade-installed game-folder add-ons cannot be toggled per launch;
use separate manager profiles for easier switching. The manager window
is useful for separate profiles, dependency management, updates, conflict
review, and flat mod play. **Reread manager profiles** only re-discovers profiles
created in the manager; AVRcade does not create its own copies.

The four choices are **VR only**, **VR with mods**, **Mods, no VR**, and
**Vanilla**. Each launch button stays visible, with initially collapsed
mode-specific details immediately underneath it; opening one closes the other
three. AVRcade-managed installation
and removal are inside VR only. Manager downloads, profile selection, refresh,
and diagnostics live inside each relevant modded panel, not in a separate
shared-tools section.
VR only requires a managed conversion with no additional or untracked
BepInEx plugins/patchers. VR with mods uses a managed conversion with extra
mods or a VR-enabled manager profile. When neither is ready, its setup action
opens the chosen installed manager inside AVRcade when its window can be safely
identified and docked, even if no profile exists yet. After adding
the VR conversion and compatible mods in the manager, refresh AVRcade and
select that profile. Mods, no VR uses a separate manager profile with a
verified loader and at least one non-loader mod, but with the VR mod absent.
The Mods, no VR button opens the mod-manager interface inside AVRcade when safe
window docking is available. Launch the flat modded game from that manager;
the game itself remains a normal separate window. No headset is required.
The core also validates direct flat-profile Steam arguments, including an
explicit `--disable-vr` flag, but the desktop UI does not bypass the manager
for this mode.
This mode cannot prove arbitrary game-root hooks are disabled, so confirm the
game stays flat. Vanilla requests the Steam game with both Doorstop
argument styles disabled and `--disable-vr`; the Steam wrapper does not pin a
VR runtime or edit the mod cfg for that launch. Arbitrary manually replaced
game files cannot be ruled out, so a truly pristine state requires a known-clean
installation. A missing headset alone is not a vanilla
switch. These checks do not verify a game build or all mod combinations.

r2modman and Thunderstore Mod Manager users with a recognized VR mod and loader can launch the selected
profile directly from AVRcade: it derives standard Doorstop arguments from
that profile's loader path and requests launch through Steam. Profile-specific
custom arguments can still be saved from r2modman's Help view when needed;
AVRcade validates those before launch. Vortex can open the
selected game and profile, after which the user deploys and launches in Vortex.
These direct Steam requests are not reported as verified headset launches.
The in-app mod-manager window host is experimental. It docks r2modman or Vortex
only when the visible window belongs to the discovered executable, and restores
the window on close. Thunderstore Mod Manager's Overwolf shortcut does not
identify the child window reliably, so it opens separately. Embedding changes
neither the manager's profile ownership nor its launch behavior.
The embedded host attempts to hand keyboard focus to the manager when docking.
If typing does not work, choose **Focus manager typing**; if Windows still
refuses focus, close the panel to restore the manager's normal window.
The manager's own launch and deployment instructions take precedence if a
game uses a custom loader or has conflicting Steam launch options.

The Steam wrapper path below documents the earlier game-directory conversion
and CLI flow. It remains relevant to existing conversions; it is not required
by the new manager-profile path.

## How it works

Mod managers launch Steam games through Steam with Unity Doorstop arguments
that redirect BepInEx to the profile folder, e.g.

    REPO.exe --doorstop-enabled true
             --doorstop-target-assembly <profile>\BepInEx\core\BepInEx.Preloader.dll

Because the launch goes through Steam, VRClient's LI-1 wrap launch option runs
first. The wrapper (`vrclient wrap`) inspects the command:

- **Doorstop redirect present** → BepInEx will load from the PROFILE, so the
  per-launch OpenXR runtime pin is written into the profile's mod cfg
  (`<profile>\BepInEx\config\io.daxcess.repoxr.cfg` for RepoXR — the relative
  path comes from the game's comfort map, not hardcoded).
- **Opt-in gate (user directive 2026-07-10: never intrusive):** a profile is
  ONLY written to when its owner has installed the VR mod there themselves —
  the wrapper checks `BepInEx\plugins\<Namespace>-<Name>` against the game's
  modpack mods. No VR mod in the profile → the profile is left completely
  untouched (wrap.log records why). When the VR mod IS present but has not run
  yet (no cfg), a minimal cfg carrying only the pin is created; BepInEx merges
  it on first run.
- **No doorstop args** → game-dir behavior, unchanged (pin into the game-dir cfg).
- Doorstop 4 (`--doorstop-enabled/--doorstop-target-assembly`) and Doorstop 3
  (`--doorstop-enable/--doorstop-target`) arg styles are both recognized;
  `--doorstop-enabled false` = deliberate unmodded launch, no pin.
- Everything stays fail-open: wrapper errors never block the launch.

## What the USER does (any supported game)

1. Convert the game with VRClient once (or at minimum run `vrclient wrap-install <slug>`
   so the Steam launch option exists).
2. Install the game's **VR mod through the mod manager itself** (e.g. r2modman →
   Online → RepoXR → Download, with its dependencies) into the profile they play.
3. Choose VR with mods in AVRcade, or launch through the manager as usual.
   Choose Vanilla for a separate no-BepInEx, no-VR Steam request.

VRClient deliberately does NOT copy mod files into the profile behind the mod
manager's back — the manager tracks its own installs, and foreign files get
ignored or wiped on sync. The manager owns mod installation; VRClient owns the
runtime pin.

## What a FUTURE game needs (developer checklist)

Nothing beyond the normal ADD-A-GAME data (docs/onboarding/ADD-A-GAME.md):

- `config/games/<slug>.json` — `executable_names` is how the wrapper matches
  the launched exe to the game.
- `config/modpacks/<slug>.comfort-map.json` — `cfg_file` is where the pin goes
  (relative path, resolved against game dir or profile root alike).

If both exist, mod-manager coexistence works with zero extra code or config.

## Trust boundary (open item)

Profile-provided mods are installed by the mod manager, NOT hash-verified by
VRClient's safety engine (M5 verification covers our own game-dir installs).
Every doorstop launch logs this note. Extending hash verification to profile
mods is the remaining LI-3 work item (docs/roadmap/launch-integration.md).

## Troubleshooting

- Wrapper telemetry: `%LOCALAPPDATA%\vrclient\wrap.log` — one line per launch
  with the exact command Steam passed (including the mod manager's doorstop
  args), the pin decision, the cfg written, and the game's exit code. This is
  the first thing to read when a mod-manager launch misbehaves.
- Which BepInEx actually ran: compare LastWriteTime of
  `<game dir>\BepInEx\LogOutput.log` vs `<profile>\BepInEx\LogOutput.log`.
- VR active but no VR mod in the profile → install the VR conversion in that
  profile, refresh AVRcade, and select it for VR with mods.
- Runtime binding problems: docs/troubleshooting/steamvr-vd-runtime-conflict.md.

## Known limit

A launcher that starts the game exe directly WITHOUT going through Steam
bypasses launch options and therefore the wrapper (no pin). Rare in the
Thunderstore ecosystem; documented, not solved.

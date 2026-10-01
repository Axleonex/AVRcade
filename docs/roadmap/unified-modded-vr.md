# Unified modded VR: implementation plan

## Scope and evidence (2026-09-28)

VRClient remains a front end to installed mod managers. The manager owns profile installs, dependencies, deployment, and updates. VRClient may read profile metadata and remember a selection, but must not edit a manager's profile files or install another loader into a game directory when a manager profile is selected.

- [Vortex documents `--game`, `--profile`, and read-only `--get`](https://github.com/Nexus-Mods/Vortex/wiki/MODDINGWIKI-Users-Troubleshooting-Command-Line-Parameters). It does not document a switch that launches the game. Opening the selected profile is a handoff, not a completed launch.
- [r2modman's author describes copying the profile-specific launch parameters from Help](https://github.com/ebkr/r2modmanPlus/discussions/693); its [profiles guide](https://github.com/ebkr/r2modmanPlus/wiki/Profiles) confirms profile ownership. VRClient can launch through Steam only after the user supplies and VRClient validates those arguments for the selected profile.
- [Thunderstore Mod Manager documents profile selection and Start Modded](https://wiki.thunderstore.io/mod-manager/game-wont-launch-modded). Its data folder is user-configurable. No supported external profile-launch API is documented; show a guided handoff.
- [PeakVR](https://thunderstore.io/c/peak/p/Andrey04o/PeakVR/), [CWVR](https://thunderstore.io/c/content-warning/p/DaXcess/CWVR/), and [Big Walk VR](https://old.thunderstore.io/c/big-walk/p/CircuitLord/Big_Walk_VR/) are community packages. CWVR is deprecated; Big Walk requires SteamVR. Existing headset and arbitrary mod-combination verification remains unknown. RV There Yet? is an experimental UEVR route outside the mod-manager flow.

## Phases and acceptance criteria

1. **Discovery and saved choice.** Detect only installed Vortex, r2modman, and Thunderstore Mod Manager instances; enumerate readable profiles for supported games; record a per-game preferred manager/profile under VRClient's own settings. Acceptance: an absent manager cannot be selected; a stale saved profile is reported and never silently replaced; tests cover mixed managers, missing directories, and persistence.
2. **Preparation in the owning manager.** Show the credited VR package, description, preview, original page, and dependencies in a secondary view. Guide installing it into the chosen profile through that manager's UI. Do not write manager profile files or game-directory loaders. Acceptance: existing profile mods remain untouched; loader presence is reported without offering a second automatic install; CWVR deprecation and unverified combinations are visible.
3. **Launch and handoff.** For r2modman, accept only arguments copied from its Help view that bind to the selected profile, then launch via Steam's normal app route. For Vortex, open the selected game/profile and direct the user to deploy and launch there. For Thunderstore, direct the user to select the same profile and press Start Modded; open the app only when an installed app entry is verifiable. Acceptance: a handoff is never called a launched game; no direct game-exe launch or profile-file edit; stale/mismatched arguments fail closed; safety and headset readiness are checked first.
4. **Product integration and verification.** Present game → profile → readiness → launch in the main detail view. Keep mod inventory, conflicts, sources, and setup detail collapsed. Preserve the existing non-manager routes, including separate unverified RV There Yet? treatment. Acceptance: focused automated tests and .NET build pass; report the exact end-to-end evidence and manager-specific limits.

## Existing worktree boundary

The checkout is dirty, including the app view models, window, controller, and installer. Changes for this plan must be additive and preserve those edits. No commit, merge, or push is authorized.

## Implementation and verification notes

- Phases 1–4 are implemented in the .NET core and Avalonia library view. Discovery is read only. A manager's executable (and, for Thunderstore, its Overwolf launcher plus app shortcut) must exist before its profiles appear. Users can point VRClient at a moved r2modman/Thunderstore data folder in the secondary setup view. Saved selection and launch arguments live under VRClient's own local settings.
- r2modman accepts Help-generated launch arguments only when they point to the selected profile's existing BepInEx loader. Windows Doorstop 3 and 4 argument names and Mono/IL2CPP loaders are recognized. Steam owns the launch; the UI says it requested launch, not that VR was proven in-headset.
- Vortex uses its documented game/profile selection flags and a read-only `--get` profile query. The exact `persistent.profiles` state path is an implementation assumption requiring an installed-version check. No Vortex game-start flag was verified, so it remains a manager handoff. Thunderstore is also a manager handoff; users select the same profile and press Start Modded there.
- Profile mod names are read from enabled entries in the manager's `mods.yml` when present, with folder inventory as a fallback. The UI does not verify arbitrary mod combinations. Creator credit, details, warnings, a CDN icon preview when online, and the original page appear in the secondary view. RV There Yet? remains an independent experimental UEVR route.
- Local live `doctor` output: PEAK and Big Walk were on a disconnected `W:\SteamLibrary`; Content Warning was not installed. SteamVR was stopped. No real game, manager, or headset launch could be verified on this machine. The r2modman Steam launch was dry-run tested with fixtures; Vortex and Thunderstore handoffs were unit-tested only.

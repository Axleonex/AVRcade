# Historical in-app mod management proposal (superseded)

Superseded by the user's decision to keep mod-specific search and installation
in each external manager's own mod pages. AVRcade's current app UI no longer
offers a Thunderstore mod search or optional-mod install action. The design
below is retained as history, not an active delivery promise; existing
AVRcade-installed add-ons are not deleted by that UI change.

Status: first usable in-app route implemented in source. On PEAK, Content
Warning, and Big Walk pages, the default AVRcade-managed setup can install the
pinned VR conversion, search that game's Thunderstore catalog, install
additional BepInEx plugin packages and dependencies, and launch from AVRcade.
Optional packages have a separate manifest; the pinned conversion manifest is
not overwritten. Unsupported archive layouts, unsafe paths, and conflicting
dependency versions are refused. The installed C: AVRcade executable has not
yet been updated with these source changes.

This first route is a single game-directory setup, **not yet an isolated named
profile**. The isolated profile-location store exists but is not wired to
installation or launch. There is no enable/disable, update, removal UI, package
detail view, or dedicated Thunderstore/r2modman/Vortex page yet. External
manager profiles remain read-only optional launch choices. Do not describe the
full profile workflow below as shipped.

## User outcome

For PEAK, Content Warning, and Big Walk, the game page must offer an AVRcade
Mods area where a player can find a mod, inspect its author/version/dependencies,
install it into an AVRcade-owned profile, enable or disable it, and launch VR
with that same profile. The ordinary path must not require opening a separate
mod manager. The VR conversion stays selected by default; disabling it must be
an explicit flat-mode choice, not an accidental side effect of adding a mod.

## Ownership boundary

AVRcade profiles live under AVRcade's own application-data directory and have
their own manifest, package hashes, loader, configuration, and launch arguments.
The application must not write to r2modman, Thunderstore Mod Manager, or Vortex
profiles behind those applications' backs. Existing manager profiles remain
read-only inputs until an explicit import copies compatible content into a new
AVRcade profile; import must never move or delete the source.

The in-app Thunderstore and r2modman pages share Thunderstore's package source
but explain which external profile, if any, is being viewed. The Vortex page
shows detected Vortex profiles separately. It must not promise one-click
installation of Nexus/Vortex packages until an authenticated, supported API
and deployment adapter exist. All three pages stay inside AVRcade; an external
manager launcher is an optional advanced escape hatch, not the primary action.

## Delivery gates

1. **Profile engine:** isolated paths; explicit ownership manifest; safe ZIP
   extraction; dependency resolution; hash verification; atomic install and
   rollback; no overwrites of external profiles or unowned game files.
2. **Playable profile:** known-good VR conversion and loader installed in the
   profile; Steam-owned launch with that profile's Doorstop arguments; runtime
   and headset checks; clean flat/VR distinction.
3. **Mods UI:** in-app search, package details, install/update/remove,
   enable/disable, installed list, dependency and conflict warnings, status
   and error recovery. No manager app required for this route.
4. **Manager pages:** Thunderstore, r2modman, and Vortex views with detected
   profiles, read-only inventories, explicit import compatibility, and honest
   unsupported-state explanations. Never edit manager-owned state.
5. **Validation:** tests for traversal/zip bombs, dependency conflicts,
   rollback, concurrent managers, stale package metadata, profile launch args,
   and each game's VR mod pin; desktop UI smoke; headset launch checks for all
   three games before marking the route verified.

The current `ModdedGameViewModel`/`ModdedVrLauncher` manager handoff remains a
legacy/advanced route until these gates pass. Merely adding manager tabs or
links does not satisfy the promised in-app mod workflow.

Sources: [Thunderstore package API](https://github.com/thunderstore-io/Thunderstore),
[r2modman profile docs](https://github.com/ebkr/r2modmanPlus/wiki/Profiles),
[Vortex extension API](https://github.com/nexus-mods/vortex/blob/master/packages/vortex-api/README.md).

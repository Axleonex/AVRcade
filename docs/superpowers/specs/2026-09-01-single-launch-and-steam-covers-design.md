# VRClient single launch and Steam cover design

**Status:** Implemented and verified
**Date:** 2026-09-01

## Problem

VRClient currently has several technically launchable binaries. The Desktop
shortcut targets a July 8 self-contained publish, while the redesigned August
31 app lives in a different Release output. Debug, publish, and visual-QA
copies remain inside the repository, and a separate `VRClient Status.cmd`
helper adds another desktop entry. Users cannot tell which copy is current.

The catalogue also uses deterministic typographic fallback covers even when
Steam already has the games' official library artwork cached on the PC.

## Decision

VRClient will have one canonical installed executable under
`%LOCALAPPDATA%\Programs\VRClient`. Every user-facing shortcut will target that
same executable. Build, publish, and QA binaries may continue to exist as
developer artifacts, but nothing user-facing will point to them.

Catalogue cards will use Steam's local cached `library_600x900` artwork when a
numeric Steam app ID and cached cover are available. VRClient will not download
or redistribute artwork. The existing typographic cover remains the fallback
for manual profiles, missing Steam installations, or absent cache entries.

## Alternatives considered

1. **Launch directly from the repository.** Smallest change, but the target can
   become stale whenever a different build or publish folder is rebuilt.
2. **Bundle copied Steam covers.** Portable, but creates redistribution and
   staleness concerns for licensed artwork.
3. **Download Steam covers at runtime.** More complete, but introduces network,
   privacy, caching, and recovery states that the offline catalogue does not need.

The canonical local install plus local Steam-cache lookup avoids those costs.

## Experience contract

```text
EXPERIENCE DEPTH: operated / multi-state
USER + JOB: A Windows player launches the one current VRClient app and recognizes
            each supported game immediately from its cover.
EVIDENCE:
  known: The Desktop shortcut targets an older self-contained publish; the current
         redesign is in a newer Release output; Steam has cached portrait covers
         for Cyberpunk 2077, RDR2, Lethal Company, and R.E.P.O.
  inferred: Multiple developer binaries are acceptable internally, but only one
            canonical executable should be exposed as the installed application.
  assumed: Desktop and Start-menu shortcuts may coexist when both resolve to the
           exact same executable; "single point" means one canonical version.
  open: GTA: San Andreas may not have a usable local Steam cover and can fall back.
HAPPY PATH: Publish current app -> install to canonical directory -> replace
            shortcuts -> open either shortcut -> see the redesigned catalogue
            with cached covers -> open a game.
STATE MAP: cached cover -> artwork card; no cached cover -> typographic fallback;
           successful install -> shortcuts resolve to canonical executable;
           failed publish/install -> old working shortcut remains intact.
FAILURE + RECOVERY: Stage the new install before changing shortcuts. If publish or
                    launch verification fails, preserve the old shortcut and report
                    the failed stage. Missing artwork never blocks the catalogue.
ONBOARDING: none — the catalogue and normal Windows shortcuts are self-explanatory.
BEHAVIORAL SUCCESS: All user-facing VRClient shortcuts resolve to one installed
                    executable; that executable opens the current catalogue; cached
                    game covers render; missing covers use a readable fallback.
```

## Design plan

```text
SUBJECT: A personal VR game shelf where a player identifies a title visually and
         immediately sees whether it is ready.
TREATMENT: ui-tool with an editorial catalogue surface
PALETTE:
  ground:    #F3F7FA
  surface:   #FFFFFF
  ink:       #14212B
  ink-2:     #526471
  accent:    #176B87
  semantic:  good #1E7654 / warn #A86200 / bad #B42318
TYPE:
  display:   Segoe UI Variable Semibold
  body:      Segoe UI Variable
  utility:   Segoe UI Variable Medium
LAYOUT: Official portrait artwork fills the existing 200×228 cover stage and is
        cropped uniformly. Title and readiness remain in the quiet card footer.
        The fallback preserves the current engine label and monogram composition.
```

The focal point is the artwork. Readiness remains textual and semantic, so
cover colors never carry application state. No new accent, shadow system, or
motion is introduced.

## Components

### Canonical local publisher

A repository script publishes the current Avalonia app as a self-contained
Windows executable into a repository staging directory, copies the configuration
and required app resources, validates the staged app, and only then replaces the
contents of `%LOCALAPPDATA%\Programs\VRClient`.

The script creates or updates:

- Desktop `VRClient.lnk`;
- Start-menu `VRClient.lnk`;
- both with the same target and working directory.

The obsolete Desktop `VRClient Status.cmd` is removed after the canonical app
launch succeeds. Developer build and QA directories are not deleted because they
are build/test evidence rather than user-facing installations.

### Steam cover locator

A small app-layer service receives the game's `SteamAppId`. It ignores empty,
non-numeric, or `manual` identifiers. It checks known Steam roots and the Steam
library cache for that app ID, preferring `library_600x900.jpg` or PNG. Both the
flat and hash-subdirectory cache layouts are supported.

The service is read-only, performs no network call, and returns `null` when no
usable image exists. Resolution happens when catalogue item view models are
created, not during XAML rendering.

### Catalogue presentation

`GameItemViewModel` exposes an optional cover image and a `HasCoverImage` flag.
The card and opened-game poster display the image with uniform-to-fill cropping.
The existing deterministic cover is shown only when `HasCoverImage` is false.
The title, engine context, readiness badge, keyboard focus, and card action remain
unchanged.

## Error handling

- Publishing occurs in staging; a failed build cannot replace the working install.
- Shortcut replacement happens only after the staged executable opens a VRClient
  window successfully.
- A malformed or unreadable Steam cache entry is treated as a missing cover.
- Image decode failure falls back locally and never blocks navigation or launch.
- The local publisher refuses to replace directories outside the exact canonical
  install and repository staging paths.

## Verification

1. Unit/contract tests cover numeric app IDs, nested/flat Steam cache layouts,
   missing cache entries, manual games, XAML image/fallback bindings, and absence
   of network artwork code.
2. Release build and existing catalogue/Cyberpunk UI contracts remain green.
3. The current app is staged and opened before shortcut replacement.
4. Desktop and Start-menu shortcuts are resolved and proven to target the same
   `%LOCALAPPDATA%\Programs\VRClient\vrclient-app.exe`.
5. The installed app is launched and visually inspected in light and dark themes.
6. Cached Cyberpunk, RDR2, Lethal Company, and R.E.P.O. art is visible; GTA uses
   its fallback if no local art is available.

## Scope boundaries

- No remote image requests or Steam authentication.
- No deletion of repository build, publish, or evidence directories.
- No claim that fallback-only games have official art.
- No installer/release-version overhaul; this is a reliable local canonical
  installation for the current machine.

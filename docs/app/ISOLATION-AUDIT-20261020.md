# Game installation isolation audit — 20 October 2026

## Scope

Audited the ten-game public catalogue's configuration discovery, remembered Steam
installations, GUI/CLI launch handoff, controller routing, managed mod installation,
updates, removal and setup recovery. GTA IV/V and Fallout 3/New Vegas remain excluded
from the public catalogue. No runtime package dependency was added.

## Findings and fixes

| Area | Finding | Correction |
|---|---|---|
| Configuration | A legacy development-root variable could override installed configuration. | Bundled configuration wins over the legacy variable; explicit AVRcade overrides remain supported. |
| Child launcher | A helper could inherit another configuration root or rediscover another copy. | Bind both configuration variables and pass the selected game folder and its matching Steam manifest. |
| Remembered disks | Locating one title reordered discovery for every title. | Pin first discovery and explicit Locate selections by Steam app ID, retaining old library hints for discovery. |
| Missing media | An unavailable selected disk could silently fall back to another copy. | Keep the selection and offer reconnect/Locate; unreadable selections also fail closed. |
| Steam metadata | An unreadable manifest blocked healthy libraries; identities and folder boundaries were insufficiently checked. | Skip unavailable libraries, verify app IDs and reject paths outside the selected game's installation. |
| Update adapters | A malformed unrelated route blocked another game's updater. | Deserialize and validate only the requested route; reject adapters that do not match the selected game. |
| Controllers | Legacy routes were mistaken for another game's ownership; concurrent writes could lose routes. | Migrate only the same game's exact route paths and lock the shared route index while writing. |
| Settings writes | Multiple app copies shared one temporary filename; a failed replacement left temporary files. | Create unique temporary files exclusively and clean up only the current write's file. |
| Controller editor | Invalid saved assignments or defaults could prevent recovery controls from working. | Keep the editor usable, show a useful error and support selected-default/original-binding recovery without altering other games. |
| Linked folders | Install/removal and Cyberpunk paths could follow links into another location. | Use one containment guard, including directory/file links; validate the entire removal plan before deleting anything. |
| Initial install | Existing foreign loaders could be overwritten, leaving a partial install. | Refuse foreign ownership before applying files; use the existing journal/rollback mechanism for installation and removal, preserving owned configuration. |
| UEVR sessions | Cleanup could close an injector from another session; process-name matching could choose another disk's copy. | Record injector PID/start time/path ownership, leave active/unknown sessions alone and verify the game's full executable path before injection. |
| Cyberpunk sessions | Recovery could adopt a replacement from a mod manager as AVRcade's stored package. | Preserve both copies and report the conflict; mutable settings remain supported. |
| Recheck setup | The button only refreshed cached discovery and could retain a stale update error. | Clear the selected game's finished error, re-detect, recover its interrupted update/session and repair its controller routes. |

## User recovery

1. Open the affected game's page. Its installed location identifies the selected copy.
2. If the game moved, use **Locate installed game**. Cyberpunk, Unreal and YAPYAP
   now support this alongside the existing Unity and classic San Andreas routes.
3. Close that game and press **Recheck setup**. No download or controller reset is
   performed by this check. Safe journal recovery and generated-route cleanup are automatic.
4. If controller settings need attention, open **Controls**, then choose **Reset to
   my defaults** or **Original bindings**. Other titles' assignments remain intact.

The check preserves conflicting foreign files and reports why automatic recovery
stopped. Mod-manager profiles retain their own ownership. A shared UEVR injector
still requires sequential sessions; an active or untracked session is not killed.
Steam and mod managers retain control of their own launch configurations. UEVR
refuses to inject when the executable does not belong to the selected installation.

## Validation

- Baseline: 488 core cases, 487 passed and one existing URI host-casing assertion failed.
  The assertion was corrected for `System.Uri` host normalization; branding was retained.
- Regression coverage includes all ten actual public catalogue configurations,
  independent/moved/disconnected selections, different Windows drive letters and UNC
  shares, directory junctions, ownership conflicts, controller defaults, repeatable
  recovery, interrupted updates and concurrent leases.
- Added a separate app test project to the solution for controller recovery commands.
- Final managed solution result: **540 passed, 0 failed, 0 skipped** (538 core, 2 app).
- App and CLI are built from the audited source; local deployment records contain
  executable checksums and rollback locations.

Tests use temporary game fixtures. No agent-driven gameplay or headset acceptance
run was performed, and the results do not certify every upstream mod/game build.
No game save or mod-manager profile was changed. GitHub publication is separate
from this local audit and requires the owner's normal push confirmation.

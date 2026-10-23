# Game builds, VR packages and older installations

Every current catalogue entry has a **Game compatibility** card. Expand **Game versions and automatic VR updates** for installation selection, update preferences, package preparation, clean monitor play and recovery.

## Before VR play

AVRcade records the selected executable's SHA-256, its reported version and its own adjacent Steam build ID when available. It rechecks the executable before a VR action. A changed executable does not inherit headset confirmation from the previous build. Native exact-build checks and the package's explicit supported-version list remain mandatory. A user's headset confirmation cannot override them.

An initial observation is experimental, not a compatibility certification. A new build without reviewed compatibility is blocked until its compatibility is established. A headset confirmation records the user's own stereo, tracking and controls test for that installation, executable and approved-package revision; process survival is never recorded as headset proof.

**Automatically prepare approved VR package updates before VR play** is saved separately for each installation. Automatic preparation uses AVRcade's reviewed package pins. It does not resolve an arbitrary latest release and declare it compatible:

| Integration | Automatic preparation |
| --- | --- |
| Thunderstore / BepInEx | Installs the shipped hash-pinned lockfile through staging and file transactions. Existing configuration is preserved. Different unowned files and automatic downgrades are refused. |
| Cyberpunk | Uses the package's supported game versions and its reviewed backend/framework installer. A newer upstream release still needs a compatible integration; a newer manually installed package is not automatically downgraded. Nexus authorization remains a user action. |
| UEVR | Ensures the approved UEVR/runtime pair and reviewed game profile are prepared. Existing valid profile settings are retained. |
| Paired UUVR / YAPYAP | Checks the existing reviewed bridge pair. It does not substitute an unrelated UUVR revision. A compatible downloadable pair must exist before automatic installation is possible. |
| Native/local integrations | Retain their exact-build preparation. No update is fabricated when no approved downloadable package exists. |

There is no scheduled background updater. Checks happen in AVRcade and during the selected game's launch/preparation. Other games and mod-manager profiles are not silently updated. A package needed for the selected build must be installed before VR play; when automatic updates are off, **Prepare approved VR package** is the explicit manual action.

## Older game installations

When the installed build does not match the VR integration, **Use a compatible game version** opens a recovery popup. The blocked VR buttons also open this popup instead of attempting launch. It shows the installed build and the supported version from the package metadata (for example, Cyberpunk 2.5 versus VR package 0.1.7's supported 2.31).

**Open Steam game properties** opens that title's properties. Select **Game Versions & Betas** (**Betas** in older clients) and choose a matching publisher-provided branch, if offered. Steam controls availability and performs the download, replacing its registered installation. AVRcade does not claim that every historical version is available, choose an unverified branch, download depot files, or edit game manifests. See [Valve's branch documentation](https://partner.steamgames.com/doc/store/application/branches). A separate selected copy is explicitly distinguished from Steam's registered installation.

The popup also offers selection of an existing compatible copy, a recheck after the store finishes, and clean monitor play. Opening the popup or Steam properties does not change game files, update preferences or save data, or clear a VR compatibility block. A successful process request is not reported as a completed game rollback. Rechecking stays bound to the captured game and installation; changing the selected copy requires reopening the recovery options.

Choose **Choose another / older game installation** and select the folder containing that game's expected executable. The selector remembers multiple roots. Choosing a missing/disconnected copy never silently launches another installation.

Use a store-provided older-version branch when one exists, or select a legitimate local copy you already have. AVRcade does not obtain old game binaries, promise that a store offers every historical version, edit Steam manifests to pretend a downgrade happened, or bypass ownership/authentication. A separate older copy carries its own executable identity; it does not borrow another copy's Steam manifest. Unity/Cyberpunk/native launch paths use the selected folder. UEVR starts a selected separate copy through its normal executable and attaches only to that exact path; the default store installation retains the Steam route.

VR package files and game-folder settings remain in their own installation. Controller assignments and saved defaults receive an installation-specific profile, initially copied from the existing shared preferences without overwriting them. Runtime route discovery keeps its existing shared location. Mod-manager profiles remain managed by their manager and must target the selected game copy.

A mod-manager handoff that cannot guarantee the selected copy is refused. Use mods installed in that copy's folder, or configure and launch that copy in the manager itself. Mods parked for clean play must be restored before choosing play with mods.

**Back up saves before switching versions.** Newer saves may be incompatible with older game builds. AVRcade displays this warning and does not rewrite, downgrade or automatically load saves.

## Startup checks and package recovery

Automatic package changes have a game-local backup and before/after hashes. A confirmed early process failure can restore a provisional mod update and pause automatic updates for that installation. A launcher/authentication wait, inspection failure, or timeout is not treated as a confirmed crash. Successful process observation records startup only; check image, stereo, tracking and controls yourself.

**Restore previous VR package** restores the last recorded package changes and pauses automatic updates. It never rolls back the Steam game itself. Original and backup hashes are checked before restoration; foreign edits are preserved and reported instead of overwritten. An interrupted update that cannot be validated retains its backups and refuses further VR preparation until resolved. Existing interrupted file-transaction recovery still runs first.

## Clean monitor play

**Clean monitor play**, also used by **Vanilla**, parks detected mod loaders and supported deployed-mod locations under `.avrcade/monitor-session` inside that game's root. Cyberpunk's ASI/RED4ext loaders and redscript compilation setting are included, so incompatible frameworks cannot keep loading merely because VR was switched off. Unity Doorstop/BepInEx, detected Unreal mod paths and known native proxy loaders have their own routes.

Parked files stay backed up after the app closes or the game crashes. Close the game and use **Restore parked mods**, or prepare a compatible VR session to restore them. Concurrent updates/parking share a game-file lease. New deployments, directory links, traversal paths, changed backups and changed targets are refused without overwriting another party's files.

This cannot reverse arbitrary unmanaged modifications to original game assets. Those require a separate clean installation or the store's file verification. Verification is a file-repair request, not a game downgrade or VR compatibility guarantee.

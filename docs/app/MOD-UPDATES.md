# Manual game mod updates

Games with public VR conversions show **OBS Stream**, then **Update mods**, then
the title, game image and creator credits in the left column. Credits sit directly
below the image and above the status/install-location card. The update action belongs to
that game. Games using only AVRcade's own native conversion do not get a public
mod update control. OBS live/recording state appears separately, so the button
keeps its recognizable name while an output is active.

## Package policy

`config/mod-update-routes.json` selects a reusable adapter and official release
sources. Credit metadata does not execute an installer. Clicking Update mods
checks the configured upstream sources and then applies the adapter:

| Adapter | Behavior |
| --- | --- |
| Thunderstore | Resolves the current main mod and its dependency graph from the official community index. Downloads once, records local archive hashes, stages the contents, then updates the managed game-folder conversion. Shipped lockfiles remain unchanged. |
| Cyberpunk | Installs AVRcade's supported, hash-pinned official package when needed. Reports newer author releases separately. Preserves managed camera-selection state and existing HUD/turn preferences. Missing Nexus requirements use the existing authorization/download-detection flow. |
| UEVR | Ensures the supported tools/runtime and the selected game's reviewed profile are installed. Keeps an already-valid customized profile. Newer upstream tool releases are reported without replacing a version-specific integration with unreviewed binaries. |
| Paired UUVR | Checks the installed UUVR/AVRcade bridge pair and upstream source changes. An independent UUVR build is not substituted for that pair. A newer compatible pair must be supplied through AVRcade's integration. |

The button is not a claim that every upstream build is compatible. A new release
outside the supported integration is shown explicitly, including its upstream
and supported references. Failed API/rate-limit checks are reported as incomplete,
not as proof that no update exists. Nexus-only dependency revisions are managed
through their existing explicit supported-file policy; no paid account is added.
Mod-manager profiles remain owned and updated by their manager.

## State and recovery

Games must be closed. A game-local exclusive lease prevents concurrent manual
updates, and the UI retains progress/results by captured game slug and install
root when selection or the catalogue changes. Settings under
`%LOCALAPPDATA%\VRClient\controllers` are never rewritten by package updates.

Thunderstore updates replace only owned package files. An incoming collision
with a different unowned file is refused. Existing `BepInEx/config` files and
INI/CFG preferences are kept. Previous owned files no longer in the package are
retired, preventing duplicate old/new plugin DLLs. File backups and a pending
journal are stored under `.avrcade/mod-updates` inside the selected game folder.
Failed transactions restore their previous contents. An interrupted transaction
recovers before a later update; foreign changes discovered during recovery are
preserved and reported for resolution. VR launches refuse an unfinished update.

The downloaded archive's locally recorded SHA-256 detects staging changes; it
is not described as a publisher-provided Thunderstore checksum. Official pinned
integrations retain their stronger existing archive/file hash checks.

No background schedule is created. Checks and installations run only after the
user presses Update mods. The action does not update Steam games or other games'
profiles, choose a different mod manager, or erase controller defaults.

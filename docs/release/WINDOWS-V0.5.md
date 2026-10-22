# Windows v0.5.3 — packaged controller and compatibility fixes

This release includes the controller-tab consolidation and consumer game-version
compatibility fixes in the Windows installer and portable ZIP. It retains v0.5.2's
per-game Troubleshooting tab, on-demand diagnostics, setup recovery, Steam file
verification, native startup checks and clearer VR launch feedback.

## Changes

- Controller keymapping appears only in each game's bottom Controls tab. Custom
  assignments, selected defaults, reset and original bindings are retained.
- Cyberpunk checks the executable's product version against its VR package's
  declared supported versions. Developer-observed Steam build IDs and hashes no
  longer reject another player's supported copy. Exact-binary native integrations
  such as classic San Andreas retain their own adapter requirements.
- Update mods can check upstream releases while VR is blocked. Blocked checks do
  not write game files or install an unreviewed/incompatible package.
- Recheck preserves compatibility problems after successful native/controller
  checks. Diagnostics refreshes only the selected game's play buttons.
- An older installed Cyberpunk VR package must update before using a newer
  package's compatibility policy. Monitor launches do not require VR-only version
  compatibility. Damaged Cyberpunk configuration does not stop other game entries.

CyberpunkVR Port remains on the supported 0.1.7 integration, whose author requires
Cyberpunk 2.31. This release does not establish VR support for game version 2.5.
Steam validation repairs original files; it does not downgrade a game or certify
third-party mod compatibility. Newer author releases are reported separately.

## Downloads

Versioned files are `AVRcade-Setup-0.5.3.exe` and
`AVRcade-0.5.3-win-x64.zip`, with individual SHA-256 files. Publish identical copies
named `AVRcade-Setup.exe` and `AVRcade-win-x64.zip`, plus `SHA256SUMS.txt` covering
both permanent and versioned binary names. README installation links use
`releases/latest/download/<name>` and must resolve to the published v0.5.3 assets.

## Build and provenance

Run `scripts/package-windows.ps1 -Version 0.5.3` with the verified native inputs.
The app, CLI, controller lease and both controller-remapping architectures are
rebuilt. Retained native inputs must match the previous published portable
archive. The package manifest records the exact source commit and every payload
hash. No personal game files or Nexus-only dependencies are redistributed.

## Installation

Close AVRcade and run `AVRcade-Setup.exe`, then launch AVRcade from the Start menu
or desktop shortcut. New installs use `%LOCALAPPDATA%\Programs\AVRcade` without
administrator access. Saved game selections and controller preferences are kept.
For portable use, extract the complete ZIP into a new, empty folder and launch
`AVRcade.exe`. Do not use GitHub's Source code ZIP as an installer.

YAPYAP still requires its separately prepared paired UUVR/AVRcade bridge package.
Escape the Backrooms retains its game-specific VR setup. GTA IV/V and Fallout
3/New Vegas remain outside the public catalogue.

## Validation and publication

The implementation passed 658 core tests and all 30 app tests, including checks
across all ten public catalogue entries. App and CLI builds passed with zero
warnings/errors. A read-only preflight on the installed Cyberpunk 2.5 copy correctly
reported its unsupported VR version; its monitor-launch dry run passed without
starting the game. No new gameplay or headset acceptance run was performed.

Rebuild/package checks verify staged catalogue discovery, native safety-helper
discovery, GTA bridge pins, manifest hashes, ZIP contents, executable versions and
the matching permanent aliases. Record actual results with the prepared assets.

After the required owner confirmation, push the release documentation, create
`v0.5.3` on the manifest's exact commit, upload all seven assets and verify GitHub's
asset digests before marking the release latest. Verify the README installer,
portable and checksum links after publication. Do not represent a source push as
an updated downloadable installer.
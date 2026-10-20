# Windows v0.5.1 — current installation links

This release packages the public catalogue with per-game mod updates, OBS Stream
labels, creator cards below game images, current Cyberpunk setup, controller
preferences and AVRcade executable/install-directory branding. GTA IV/V and
Fallout 3/New Vegas remain excluded from the public catalogue.

## Downloads and future releases

The versioned installer is `AVRcade-Setup-0.5.1.exe`; the portable archive is
`AVRcade-0.5.1-win-x64.zip`. Each release also publishes identical copies named
`AVRcade-Setup.exe` and `AVRcade-win-x64.zip`. README links use GitHub's
`releases/latest/download/<name>` URLs and follow subsequent releases.
Every future release must include those same permanent asset names plus
`SHA256SUMS.txt`, covering both permanent and versioned binary names.
Version-specific archives and checksums remain available for reproducibility.

## Build and provenance

Use `scripts/package-windows.ps1 -Version 0.5.1` with the native inputs described
in the previous release guide. The package manifest records the source commit
and each payload hash. Retained native game/helper binaries must match their
previous published archive. Controller remapping and its lease helper are built
from current public source. No personal game files or Nexus-only dependencies
are redistributed.

## Installation

Close AVRcade, run `AVRcade-Setup.exe`, then launch AVRcade from the Start menu or
desktop shortcut. New installations use `%LOCALAPPDATA%\Programs\AVRcade` and
`AVRcade.exe`, accompanied by `avrcade-cli.exe`. Settings migration preserves
existing selections. Previous installations remain available for older Steam
links. For portable use, extract the entire ZIP into its own directory first.

Cyberpunk uses AVRcade's supported 0.1.7 integration and existing Nexus dependency
flow. Update mods reports newer upstream releases separately where integration
is required. Thunderstore routes resolve current dependencies; manager profiles
remain manager-owned. YAPYAP requires its paired UUVR/AVRcade bridge installation.

If a game moved, use **Locate installed game** on its page. Close the affected
game and press **Recheck setup** to refresh its selected installation and recover
interrupted setup. Conflicting foreign files are preserved and explained. An
unavailable selected disk never silently switches to another copy.

## Publication and validation limits

Upload all versioned files plus both permanent binary aliases and SHA256SUMS.txt
under `v0.5.1`, mark it latest, and keep the source tag on the manifest's commit.
Publish the prepared source/README and release as one approved update.
Build and file-integrity inspections do not certify headset/gameplay behavior.
Record which build, inspection and runtime checks were actually performed;
do not inherit prior releases' test claims as evidence for the new packages.

The audited release source passed **540 managed tests** (538 core and 2 app),
with zero failures. Coverage includes independent game selections, moved and
unavailable disks, linked folders, ownership conflicts and interrupted setup.
No gameplay or headset acceptance run was performed for release preparation.

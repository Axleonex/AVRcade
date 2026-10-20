# Windows v0.5.0 — current installation links

This release packages the public catalogue with per-game mod updates, OBS Stream
labels, creator cards below game images, current Cyberpunk setup, controller
preferences and AVRcade executable/install-directory branding. GTA IV/V and
Fallout 3/New Vegas remain excluded from the public catalogue.

## Downloads and future releases

The versioned installer is `AVRcade-Setup-0.5.0.exe`; the portable archive is
`AVRcade-0.5.0-win-x64.zip`. Each release also publishes identical copies named
`AVRcade-Setup.exe` and `AVRcade-win-x64.zip`. README links use GitHub's
`releases/latest/download/<name>` URLs and follow subsequent releases.
Every future release must include those same permanent asset names plus
`SHA256SUMS.txt`, covering both permanent and versioned binary names.
Version-specific archives and checksums remain available for reproducibility.

## Build and provenance

Use `scripts/package-windows.ps1 -Version 0.5.0` with the native inputs described
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

## Publication and validation limits

Upload all versioned files plus both permanent binary aliases and SHA256SUMS.txt
under `v0.5.0`, mark it latest, and keep the source tag on the manifest's commit.
Publish the prepared source/README and release as one approved update.
Build and file-integrity inspections do not certify headset/gameplay behavior.
Record which build, inspection and runtime checks were actually performed;
do not inherit prior releases' test claims as evidence for the new packages.

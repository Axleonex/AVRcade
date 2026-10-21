# Windows v0.5.2 — current installation links

This release adds a bottom Troubleshooting tab to each public catalogue game,
on-demand diagnostics scoped to its installation, selected-game setup recovery,
Steam original-file verification, native startup checks and clearer VR launch
feedback. Diagnostic results stay in memory; no logs or history are saved.
Existing per-game mod updates, OBS streaming, controller preferences and
Cyberpunk setup are retained. GTA IV/V and Fallout 3/New Vegas remain excluded
from the public catalogue.

## Downloads and future releases

The versioned installer is `AVRcade-Setup-0.5.2.exe`; the portable archive is
`AVRcade-0.5.2-win-x64.zip`. Each release also publishes identical copies named
`AVRcade-Setup.exe` and `AVRcade-win-x64.zip`. README links use GitHub's
`releases/latest/download/<name>` URLs and follow subsequent releases.
Every future release must include those same permanent asset names plus
`SHA256SUMS.txt`, covering both permanent and versioned binary names.
Version-specific archives and checksums remain available for reproducibility.

## Build and provenance

Use `scripts/package-windows.ps1 -Version 0.5.2` with the native inputs described
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

For a clean portable installation, extract the complete ZIP into a new, empty
folder. Existing installer users close AVRcade and run the latest installer;
saved selections and preferences are preserved.

If a game moved, use **Locate installed game** on its page. Close the affected
game and press **Recheck setup** to refresh its selected installation and recover
interrupted setup. Conflicting foreign files are preserved and explained. An
unavailable selected disk never silently switches to another copy. The bottom
**Troubleshooting** tab offers read-only diagnostics and explanatory issue cards.
Steam verification is requested explicitly for the selected registered installation;
Steam reports completion. It does not establish third-party mod compatibility.

## Publication and validation limits

Upload all versioned files plus both permanent binary aliases and SHA256SUMS.txt
under `v0.5.2`, mark it latest, and keep the source tag on the manifest's commit.
Publish the prepared source/README and release as one approved update.
Build and file-integrity inspections do not certify headset/gameplay behavior.
Record which build, inspection and runtime checks were actually performed;
do not inherit prior releases' test claims as evidence for the new packages.

The updated source passed **646 core tests**, with zero failures or skips. UI
and diagnostic checks covered all ten catalogue entries (nine installed games),
selection changes during a check, installation isolation, rejection of stale
actions and disabling actions while busy. The installed local app/CLI match the
committed update. No new gameplay or headset acceptance run was performed.
Release packaging performs its own staged catalogue, safety-helper and payload
integrity checks; record the resulting source commit and hashes with the assets.

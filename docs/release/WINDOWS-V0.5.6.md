# AVRcade 0.5.6 — Cyberpunk framework log recovery

This update fixes a reproduced Cyberpunk 2077 startup abort after pressing **Play in VR** in AVRcade. It retains the CyberpunkVR Port 0.1.9/game 2.5 integration and the [0.5.5 startup and package-health fixes](WINDOWS-V0.5.5.md).

## Cause and recovery

The captured native exception was `directory_entry::status: The path cannot be traversed because it contains an untrusted mount point.` ArchiveXL 1.28.3's matching debug symbols located the exception at its log-directory enumeration. The uncaught exception caused the C-runtime abort reported as `0xC0000409`. Terminal-style launches succeeded while the actual GUI launch failed, which the earlier terminal validation missed.

Before activating a managed VR launch, AVRcade now inspects the generated `ArchiveXL.log`, `Codeware.log` and `TweakXL.log` aliases. Recovery requires a matching pinned framework DLL hash and a recognized timestamped log filename in the same directory. It reads link metadata without opening the target, then parks the link itself outside RED4ext's scan under `.avrcade/cyberpunk-2077/log-link-backups/`. Each link receives a restoration receipt with its original path and target.

Regular log files, their contents, unknown link targets and linked framework directories are preserved. All aliases are checked before the first move. No framework DLL, game file, VR preference or Windows security policy is changed by this recovery. Dry-run and monitor launch do not perform this log-link recovery.

## Validation boundary

The failure was reproduced through the actual installed AVRcade Play button. Native first-chance and fatal exception captures established the logger failure. Preserving the three generated log aliases allowed the same GUI launch to load ArchiveXL, Codeware, the VR plugin, Input Loader and TweakXL, compile scripts and reach a responding Cyberpunk window.

Regression tests cover recognized and dangling log links, preservation of target contents, idempotence, regular logs, dry-run, foreign targets, unknown framework bytes and directory-link refusal. In-headset stereo, controls, save loading and sustained gameplay still require human acceptance.

Automated validation passed 680 core tests and all 30 UI tests with the shared UI dispatcher serialized.

## Install

Close Cyberpunk and AVRcade, then run the [v0.5.6 installer](https://github.com/Axleonex/AVRcade/releases/download/v0.5.6/AVRcade-Setup-0.5.6.exe). Portable users should extract the complete [v0.5.6 ZIP](https://github.com/Axleonex/AVRcade/releases/download/v0.5.6/AVRcade-0.5.6-win-x64.zip) into a new folder. [Checksums](https://github.com/Axleonex/AVRcade/releases/download/v0.5.6/SHA256SUMS.txt) cover both versioned assets and permanent download filenames. Game saves and mod-manager profiles do not need to be deleted.

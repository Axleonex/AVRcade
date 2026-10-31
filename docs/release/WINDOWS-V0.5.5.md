# AVRcade 0.5.5 — Cyberpunk startup repair

Download the [Windows installer](https://github.com/Axleonex/AVRcade/releases/download/v0.5.5/AVRcade-Setup-0.5.5.exe), [portable ZIP](https://github.com/Axleonex/AVRcade/releases/download/v0.5.5/AVRcade-0.5.5-win-x64.zip), and [SHA-256 checksums](https://github.com/Axleonex/AVRcade/releases/download/v0.5.5/SHA256SUMS.txt). The permanent [installer](https://github.com/Axleonex/AVRcade/releases/latest/download/AVRcade-Setup.exe) and [ZIP](https://github.com/Axleonex/AVRcade/releases/latest/download/AVRcade-win-x64.zip) links select the latest published release.

## Install or update

Close AVRcade and Cyberpunk 2077, then run the installer. It updates the per-user app without requiring administrator access. For portable use, extract the complete ZIP into a new folder and run `AVRcade.exe`. Keep the extracted files together. Do not delete game saves or mod-manager profiles.

Open Cyberpunk's page in AVRcade. Use **Install VR** or **Update mods** if the game-folder conversion still needs setup. Existing compatible installations receive the startup and repair behavior with this app update. CyberpunkVR Port remains pinned to the official 0.1.9 release for game 2.5. More Occluders still requires the separate authorized Nexus import. See the [2.5 integration notes](WINDOWS-V0.5.4.md) for framework versions and dependency scope.

## What changed

- Cyberpunk retains the original child process handle throughout startup and cleanup, preserving early exit codes.
- Inspection failures record the failed operation, exception and Windows error separately from a game crash. If the game is still alive, AVRcade reports the issue and continues supervising cleanup until it exits.
- Before a real VR launch, AVRcade recovers interrupted managed sessions and verifies its owned runtime files, including scripts. Missing or damaged files are restored from the same pinned official release, with archive and per-file SHA-256 verification.
- Changed stored files receive verified backups under `.avrcade/cyberpunk-2077/repair-backups/`. Personal camera choice, grip offsets and valid overlay settings survive repair. Unknown active files from another manager are preserved and refused.
- **Update mods** checks runtime health before reporting the current package as installed. Setup help describes port 0.1.9 and game 2.5.

A healthy launch does not download a repair. Preflight, dry-run and monitor launch do not run the repair. Framework updates and authorized Nexus downloads retain their separate flows; this repair does not automatically upgrade the VR port to an unreviewed version.

## Validation and limits

The startup repair passed 673 core tests and all 30 UI tests with their shared dispatcher serialized. The Cyberpunk release-integrity and framework-minimum checks passed. The patched installed CLI launched the real game, compiled scripts successfully and reached a responding Cyberpunk window. A normal exit returned the managed VR files to storage.

The original reported inspection error was intermittent; the old diagnostics discarded its Windows exception. These changes address demonstrated process-handle, diagnostic and package-health gaps. They do not identify that lost exception or guarantee compatibility with arbitrary mods.

In-headset stereo, tracking, controls, save loading and sustained gameplay still need human acceptance. No new clean-Windows install/uninstall acceptance is claimed. The installer remains unsigned. Unchanged native components are retained from the verified 0.5.4 baseline and checked against its release manifest during packaging.

## Reproduce the package

Publish both managed projects from this commit with `dotnet publish`, `-c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:EnableCompressionInSingleFile=true -p:Version=0.5.5`. Then run `[PowerShell] scripts/package-cyberpunk-hotfix.ps1 -BasePayload <intact-0.5.4-folder> -AppExecutable <published-AVRcade.exe> -CliExecutable <published-avrcade-cli.exe> -ReleasePackage`. The packager verifies the baseline, checks binary version/source identity, includes current README and release notes, and produces the installer, ZIP, permanent-name aliases and checksums. It does not upload or publish anything.

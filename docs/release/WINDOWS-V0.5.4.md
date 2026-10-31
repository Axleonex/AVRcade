# AVRcade 0.5.4 — Cyberpunk 2077 2.5

## Unpublished startup repair candidate — October 31, 2026

The `work/cyberpunk-launch-self-heal` branch builds the 0.5.5 candidate. It keeps the official 0.1.9/game 2.5 pins. Pushing source alone does not replace GitHub's downloadable installer or portable ZIP; those assets require a separately published release.

Cyberpunk now retains the original child process handle throughout startup and cleanup. Early exits retain their exit code. Inspection errors record the operation, exception and Windows error separately from a game crash. When inspection fails while the game is alive, the CLI reports the failure to the app immediately and continues supervising cleanup until the game exits.

Before a real VR launch, AVRcade recovers an interrupted managed session and checks every owned runtime file. Missing or damaged files are restored only from the same pinned, archive-verified and file-verified official release. Changed stored files receive hash-checked backups under `.avrcade/cyberpunk-2077/repair-backups/`. Camera choice, grip settings and valid overlay UserSettings remain user preferences. Another manager's unknown active deployment is preserved and refused. Healthy launches do not download anything. Preflight, dry-run and monitor launch do not run this repair. Framework upgrades and the authorized Nexus import keep their existing separate flows.

The Update mods action checks stored runtime health before reporting the current version as installed. Setup help now describes port 0.1.9/game 2.5.

To assemble a candidate without rebuilding unchanged native components, publish both managed projects with `dotnet publish`, `-r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:EnableCompressionInSingleFile=true -p:Version=0.5.5`. Then run `[PowerShell] scripts/package-cyberpunk-hotfix.ps1 -BasePayload <intact-0.5.4-folder> -AppExecutable <published-AVRcade.exe> -CliExecutable <published-avrcade-cli.exe>`. The script verifies all baseline manifest hashes, writes a fresh candidate manifest and checksums, and builds a ZIP and installer. Use a new output directory for each build. It does not publish or install the result.

The reported inspection error was intermittent: the unchanged installed CLI also launched successfully on retry. The repair addresses demonstrated process-handle, diagnostic and package-health gaps; it does not establish the original Windows exception, which the old diagnostics discarded. A real patched launch reached a responding Cyberpunk window and compiled scripts successfully. Headset stereo, tracking, controls, save loading and sustained gameplay still need human acceptance.

Install [AVRcade-Setup.exe](https://github.com/Axleonex/AVRcade/releases/latest/download/AVRcade-Setup.exe), or extract the complete [portable ZIP](https://github.com/Axleonex/AVRcade/releases/latest/download/AVRcade-win-x64.zip) into a new folder. [SHA256SUMS.txt](https://github.com/Axleonex/AVRcade/releases/latest/download/SHA256SUMS.txt) covers both permanent and versioned filenames. Version-specific assets are on the [v0.5.4 release page](https://github.com/Axleonex/AVRcade/releases/tag/v0.5.4).

## Cyberpunk setup

This release integrates the unmodified official CyberpunkVR Port 0.1.9 for Cyberpunk 2077 2.5. Earlier game version 2.31 is not supported by this package. Install the app update, then use **Install VR** or **Update mods** on the Cyberpunk page to update the game-folder conversion. Updating AVRcade alone does not replace an already-installed VR package.

Frameworks are pinned to official maintainer releases: RED4ext 1.31.0, Cyber Engine Tweaks 1.38.1, Codeware 1.21.1, ArchiveXL 1.28.3, TweakXL 1.12.0, redscript 0.5.32 and Input Loader 0.2.6. Recognized older files receive backup-preserving replacements. Unknown plugin/script versions are preserved and stop the upgrade. Modified preferences remain user-owned, and Vortex's staging files are not overwritten through hardlinks.

More Occluders Scripted v0.3 still requires the user's separate authorized Nexus download. AVRcade applies its compatibility transform only to the recognized original script. It preserves ordinary mesh occluders and skips the instanced-mesh branch that references fields removed in game 2.5. The original script, receipt and transform hashes are retained in the game-local managed store's `compatibility` folder. Skipping that optimization may reduce the dependency's performance benefit. Third-party script files are not bundled in AVRcade.

Visual Holsters, Equipment-EX and Nova Optics are separate author-listed feature dependencies. Compatibility of arbitrary Vortex mod combinations is not established by this release.

Hold both stick clicks for half a second to open the upstream VR menu; F10 or Insert opens its settings overlay.

## Verification boundary

A real game startup loaded the VR plugin, confirmed its native game-build match, compiled the scripts successfully after the dependency repair, and opened a responding game window. Engine stereo logged separate eye regions. VirtualDesktopXR reported no available headset system, so in-headset stereo, tracking, controls, save loading and performance remain unverified for this integration.

The release preserves the controller-tab, update-check and version-compatibility behavior from 0.5.3. Native components for other games are retained from the verified 0.5.3 release, with their hashes checked again during packaging. See the release notes for the exact automated checks and packaging results. No clean-Windows installation or new headset acceptance is claimed. The Windows installer remains unsigned.

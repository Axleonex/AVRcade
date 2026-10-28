# AVRcade 0.5.4 — Cyberpunk 2077 2.5

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

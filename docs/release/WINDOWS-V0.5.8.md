# AVRcade 0.5.8 adds Titanfall 2 VR

Titanfall 2 VR has its own library card and game page. AVRcade detects the Steam copy and checks for CircuitLord's separate `TF2VR` profile. Cover art loads from the local Steam artwork cache when available.

- **VR only** and **VR + mods** open CircuitLord's installer. Choose **Titanfall 2 VR**, install or update the profile, and use **Launch in VR**. Optional VR add-ons are managed in the installer's **Add-ons** tab.
- **Vanilla** starts the Steam copy through Steam. **Mods, no VR** is unavailable in this integration.
- You need an owned copy of Titanfall 2, a signed-in EA app, and an active OpenXR runtime. AVRcade detects Steam installs. CircuitLord's installer can locate EA app and Xbox copies for VR. [CircuitLord's setup guide](https://github.com/CircuitLord/CircuitLordVRModInstaller#titanfall-2-vr)

## Install or update

Close AVRcade and run the [Windows installer](https://github.com/Axleonex/AVRcade/releases/download/v0.5.8/AVRcade-Setup-0.5.8.exe), or extract the [portable ZIP](https://github.com/Axleonex/AVRcade/releases/download/v0.5.8/AVRcade-0.5.8-win-x64.zip) into an empty folder. [SHA-256 checksums](https://github.com/Axleonex/AVRcade/releases/download/v0.5.8/SHA256SUMS.txt)

The installer retains the [upgrade and downgrade protections from v0.5.7](WINDOWS-V0.5.7.md). Cyberpunk 2077 retains support for game version 2.5 with CyberpunkVR Port 0.1.9.

## Verification

Automated tests cover catalog discovery, missing and partial VR profiles, play-mode availability, and the Steam Vanilla command in a dry run. Titanfall 2 VR still needs a live gameplay and headset check on an installed copy.

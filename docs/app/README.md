# AVRcade desktop app — Windows v0.5.1

## Install

Download the [latest Windows installer](https://github.com/Axleonex/AVRcade/releases/latest/download/AVRcade-Setup.exe),
close AVRcade, run `AVRcade-Setup.exe`, then open **AVRcade** from the Windows
Start menu or desktop shortcut. Setup installs per user under
`%LOCALAPPDATA%\Programs\AVRcade` without administrator access.

For portable use, download the [latest ZIP](https://github.com/Axleonex/AVRcade/releases/latest/download/AVRcade-win-x64.zip),
extract the entire archive into its own folder, and run `AVRcade.exe`.
Keep `avrcade-cli.exe`, configuration, native helpers and documentation beside it.
These links follow the latest published release; version-specific downloads
remain on each release page. [SHA-256 checksums](https://github.com/Axleonex/AVRcade/releases/latest/download/SHA256SUMS.txt)
cover both permanent and versioned binary names.

Windows 10 (1809 or later) or Windows 11, 64-bit, is required. AVRcade includes
its .NET runtime. End users do not need Git, Visual Studio or a separate .NET
installation. Published SHA-256 files accompany both downloads.

## First launch

1. Install a supported game you own through Steam.
2. Connect your PC VR headset through SteamVR or Virtual Desktop.
3. Open the game's page and choose **VR only**. Complete the setup step shown
   on that button first if the VR conversion is not ready.

Each game offers **VR only**, **VR + mods**, **Mods, no VR**, and **Vanilla**.
The app explains missing setup and blocked launches on the game's page. VR
conversions and dependencies are downloaded only when requested, with pinned
versions and hashes. Games are never included or downloaded by AVRcade.

YAPYAP requires a separately prepared compatible UUVR/AVRcade bridge package.
Its launcher integration is included, but those game plugins are not bundled or
automatically downloaded. Escape the Backrooms requires its game-specific VR
setup. Existing game mods, mod-manager profiles and save data remain user-owned.

## Stream or record

Install OBS Studio 28 or newer separately. In OBS, enable **Tools → WebSocket
Server Settings** and copy its password. On any AVRcade game page, choose
**Stream**, connect to OBS, select your capture scene, and check the picture
and audio before **Record** or **Go live**. The password is session-only.

For SteamVR, open **Display VR View / Show VR View** from its desktop menu and
select that window in an OBS Window Capture source. Use **Windows 10 (1903 and
up)** capture if Automatic freezes the image. Keep VR View open. Configure game
audio and microphone separately in OBS.

The [full streaming guide](STREAMING.md) covers setup, runtime mirrors and
optional spectator steadycam. Closing the panel leaves OBS outputs running.

## Troubleshooting and safety

Follow the reason shown on the game's page when setup or launch is blocked.
Some integrations require a specific game build. Removing an AVRcade-managed
conversion does not remove the game or save data. Use modded modes only in
private sessions with friends who agreed to them. AVRcade refuses games whose
configured safety policy identifies anti-cheat risk.

Installed troubleshooting guides are under `docs/troubleshooting/`; the
[repository README](https://github.com/Axleonex/AVRcade#install-on-windows)
contains current download links and game-specific setup information.

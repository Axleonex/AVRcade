# AVRcade desktop app (Windows v0.2)

> **STATUS: HISTORICAL WINDOWS v0.2 PRODUCT DOCUMENT.**
> Its UEVR workflow remains a legacy fallback. For current native Meccha work,
> use `PASSOVER-NATIVE-MECCHA.md`.

The desktop app is one library for supported Unity community-mod routes and
Unreal UEVR routes. It reports the engine, safety posture, install location, and
the next truthful action without requiring a terminal.

## Install the packaged app

Run `artifacts/dist/AVRcade-Setup-0.2.0.exe`. The per-user installer retains
`%LOCALAPPDATA%\Programs\VRClient` to preserve existing installations, creates an AVRcade Start-menu shortcut, and offers
an optional desktop shortcut. It includes:

- the self-contained Avalonia app and CLI;
- the Unity and Unreal catalog/configuration tree;
- Release-built native safety/signature helpers and their app-local VC runtime;
- onboarding, safety, and runtime troubleshooting documentation.

The installed app does not depend on the source repository. UEVR and its .NET
Desktop prerequisite are not bundled; **Prepare UEVR** fetches the configured
official archives, verifies their hashes, and keeps .NET app-local rather than
installing an end-of-life global runtime.
For RV There Yet?, **Download and install VR setup** also fetches the reviewed
community profile from the pinned upstream commit and verifies each file before
installation. Its ZIP import is an optional offline fallback.

## Build and run from source

```powershell
$env:PATH="G:\AIStorage\dotnet;"+$env:PATH
dotnet run --project client/VrClient.App -c Release
```

## Build and verify the Windows installer

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\ci\build-and-test.ps1 `
  -BuildDir build/package-native -BuildType Release -Compiler msvc -OpenXR off `
  -Note "Windows v0.2 unified catalog shipping helpers"

powershell -NoProfile -ExecutionPolicy Bypass -File scripts\package-windows.ps1 `
  -Version 0.2.0 -NativeBuildDir build/package-native `
  -GtaSaBridgePath build\gtasa-x86-hand-fix-v3\vrclient_gtasa_theater.asi

powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify-windows-installer.ps1 `
  -InstallerPath artifacts\dist\AVRcade-Setup-0.2.0.exe
```

Inno Setup 6 is a build-time dependency only. Install it with
`winget install --id JRSoftware.InnoSetup -e --scope user`.

## Library states

- **Needs game** - the catalog supports the game, but Steam did not find it.
- **Needs VR mod** - a Unity game is installed and its verified mod is available.
- **Needs UEVR** - an Unreal game needs pinned UEVR/.NET prerequisites fetched.
- **Needs headset** - its software route is prepared but no VR runtime is active.
- **Profile unverified** - UEVR is prepared, but game-specific headset evidence is open.
- **Ready** - the route is installed and all required readiness gates are green.
- **Blocked** - the safety policy refuses the route and explains why.

Removing a Unity conversion requires confirmation and removes only
AVRcade-managed files plus that game's Steam wrapper option. It does not remove
the game or save data.

## Runtime note

On the development PC (integrated graphics), the proven R.E.P.O. path is Virtual Desktop with
SteamVR closed. The SteamVR and Virtual Desktop paths were both validated on
the RTX desktop. See `docs/troubleshooting/steamvr-vd-runtime-conflict.md`.

## Adding a game

The v0.2 library ships R.E.P.O., Lethal Company, and MECCHA CHAMELEON. Add a
community Unity title through `docs/onboarding/ADD-A-GAME.md`; add an Unreal
title as a validated `config/unreal/*.uevr.json` profile.

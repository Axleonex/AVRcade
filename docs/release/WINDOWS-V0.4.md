# Windows v0.4.0 — OBS streaming release

This release packages the current public source with catalog-wide OBS controls
and SteamVR VR View capture instructions. OBS Studio is installed separately.

## Build

Build requirements: .NET 8 SDK, Inno Setup 6, and the native helpers, FNV files,
and pinned GTA San Andreas bridge expected by `scripts/package-windows.ps1`.
Existing native artifacts must pass their configured hash gates. Include only
the complete pinned Cyberpunk backend or omit it entirely.

[PowerShell]

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package-windows.ps1 `
  -Version 0.4.0 -NativeBuildDir build/package-native `
  -FnvNativeBuildDir build/fnv-x86 `
  -GtaSaBridgePath build/gtasa-x86-hand-fix-v3/vrclient_gtasa_theater.asi

powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify-windows-installer.ps1 `
  -InstallerPath artifacts/dist/AVRcade-Setup-0.4.0.exe
```

The installer verifier refuses to replace a registered non-test AVRcade install.
Use a clean test user when one is present. It installs into a bounded test
directory, verifies files and CLI behavior, opens the app, and uninstalls.

Outputs are the installer and portable ZIP, their SHA-256 files, and a payload
manifest naming the source commit and hashes of all packaged files. The
streaming guide must be present as `docs/STREAMING.md` in both distributions.

## Release checks

- Run the managed tests and catalog-wide streaming UI checks.
- Check the installer version, package manifests, file hashes and runtime helpers.
- Verify a fresh portable extraction without a source checkout.
- Run installation, app-window and removal checks on the exact setup executable.
- Scan packaged text for private paths, credentials and development-only evidence.
- Publish the installer, portable ZIP and both checksums under `v0.4.0`.
- Point the README's direct download links at the same version and mark it latest.

No user OBS scene collection, server password, stream key, recordings or private
test evidence belongs in the release. The installer remains unsigned.

## Validation boundaries

The streaming control path and a live SteamVR Home mirror recording were tested.
Windows Graphics Capture fixed a frozen image observed with Automatic capture.
That test does not certify gameplay capture, audible game/microphone audio,
audio synchronization, platform ingest or headset performance for every game.
Steadycam depends on an installed compatible game adapter.

# Windows v0.2 release runbook

> **STATUS: HISTORICAL WINDOWS v0.2 RELEASE DOCUMENT.**
> The UEVR route described here remains a fallback; it is not Phase 10 native
> Meccha evidence. Current handoff: `PASSOVER-NATIVE-MECCHA.md`.

## Scope

Windows v0.2 ships one per-user application for the supported AVRcade catalog.
Fallout: New Vegas is included as a selectable native OpenXR title when the
user owns and installs the game. The installer includes AVRcade's x86 native
adapter and metadata, but no Bethesda assets, FNVR package, or other third-party
mod binaries. Game-specific readiness remains honest until headset evidence is
recorded.

The output is a local release candidate until the application, CLI, native
helpers, and setup executable are signed with a trusted Windows certificate.

## Build-time requirements

- .NET 8 SDK
- Visual Studio Build Tools 2022 with the x64 C++ toolchain
- Python plus `jsonschema`
- Inno Setup 6

End users receive a self-contained app and app-local native runtime. UEVR and a
supported .NET Desktop runtime are fetched only when requested and are
hash-verified before use; no global .NET installation is made.

## Release commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\ci\build-and-test.ps1 `
  -BuildDir build/package-native -BuildType Release -Compiler msvc -OpenXR off `
  -Note "Windows v0.2 unified catalog shipping helpers"

powershell -NoProfile -ExecutionPolicy Bypass -File scripts\package-windows.ps1 `
  -Version 0.2.0 -NativeBuildDir build/package-native `
  -FnvNativeBuildDir build/fnv-x86 `
  -GtaSaBridgePath build\gtasa-x86-hand-fix-v3\vrclient_gtasa_theater.asi

powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify-windows-installer.ps1 `
  -InstallerPath artifacts\dist\AVRcade-Setup-0.2.0.exe
```

Outputs:

- `artifacts/dist/AVRcade-Setup-0.2.0.exe`
- `artifacts/dist/AVRcade-Setup-0.2.0.sha256`
- `artifacts/dist/AVRcade-0.2.0-win-x64.zip`
- `artifacts/dist/AVRcade-0.2.0-win-x64.sha256`
- `build/package/windows/payload/release-manifest.json`
- `build/package/installer-smoke-result.json`

When Inno Setup is unavailable, add `-SkipInstaller` to still produce and
validate the self-contained portable ZIP. The staged catalog and native FNV
payload gates run before either distribution format is emitted.

## Load-bearing release gates

| Gate | Claim | Concrete failing input |
|---|---|---|
| Managed tests | readiness and refusal decisions still work | invert one state-matrix expectation |
| Native Release tests | shipping helpers and validators pass | mutate a signed fixture or remove the catalog CTest |
| Unified catalog | installed CLI lists `fallout-new-vegas` | omit the shipped FNV adapter descriptor |
| Safety helper | installed managed CLI finds its native helper | omit `native/vrclient_safety_cli.exe` |
| Packaged FNV path | the installed app resolves the x86 launcher and companion DLLs | omit any file under `native/fallout-new-vegas` |
| Installer smoke | exact setup installs, opens, and removes | omit a required file or leave the install root behind |
| Window smoke | real packaged XAML creates the main window | break XAML or packaged catalog loading |

The UEVR package gate is deliberately a dry run: it proves the installed
orchestration path without claiming injection or headset success on the release
machine. Meccha's separate RTX evidence gate owns those claims.

## Public-release follow-up

1. Code-sign the app, CLI, native helpers, and setup executable.
2. Repeat the exact smoke on a clean Windows user without developer tooling.
3. Publish the installer hash, machine facts, and smoke JSON with the release.

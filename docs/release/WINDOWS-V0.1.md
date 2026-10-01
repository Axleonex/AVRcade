# Windows v0.1 release runbook

## Scope

Windows v0.1 turns the headset-confirmed R.E.P.O. path into one installable,
per-user product. It is intentionally narrower than the original native Phase 8
distribution backend: it packages the T-Unity app, CLI, configuration, and the
two native verification helpers needed by the managed client.

The output is a local release candidate until its executable files are signed
with a trusted Windows code-signing certificate. An unsigned build may show a
Microsoft SmartScreen warning on another machine.

## Build-time requirements

- .NET 8 SDK
- VS Build Tools 2022 with the x64 C++ toolchain
- Python plus `jsonschema` for the native validation gate
- Inno Setup 6 (`winget install --id JRSoftware.InnoSetup -e --scope user`)

These are compiler-machine requirements. The installed product carries its own
.NET and VC runtimes; the player installs none of them.

## Release commands

Run from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\ci\build-and-test.ps1 `
  -BuildDir build/package-native -BuildType Release -Compiler msvc -OpenXR off `
  -Note "Windows v0.1 shipping native helpers"

powershell -NoProfile -ExecutionPolicy Bypass -File scripts\package-windows.ps1 `
  -Version 0.1.0 -NativeBuildDir build/package-native

powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify-windows-installer.ps1 `
  -InstallerPath artifacts\dist\VRClient-Setup-0.1.0.exe
```

Outputs:

- `artifacts/dist/VRClient-Setup-0.1.0.exe`
- `artifacts/dist/VRClient-Setup-0.1.0.sha256`
- `build/package/windows/payload/release-manifest.json`
- `build/package/installer-smoke-result.json`

## Load-bearing release gates

| Gate | Claim | Concrete failing input |
|---|---|---|
| Managed test gate | client decisions and refusal paths still work | a changed safety/modpack expectation fails xUnit |
| Native Release gate | shipping helpers compile and pass all native checks | CRLF-mutated signed fixture fails artifact verification |
| Payload catalog gate | installed layout resolves its own catalog | omit `config/modpacks/repo.modpack.json` |
| Native-helper gate | installed managed client finds the native safety CLI | omit `native/vrclient_safety_cli.exe` |
| Installer smoke | setup installs runnable files and uninstaller removes them | omit a required file or leave the install root behind |
| Window smoke | the packaged app constructs its real main window | invalid XAML or missing packaged config exits/no window |

The verifier installs the exact setup binary under `build/package/installer-smoke`,
runs only read-only `games`, `safety`, and `doctor` CLI checks, opens and closes
the GUI, runs the generated uninstaller, and confirms the directory is gone.
It refuses to replace an existing non-smoke VRClient installation.

## Public-release follow-up

1. Acquire a Windows code-signing certificate.
2. Sign `vrclient-app.exe`, `vrclient.exe`, the native helpers, and the final
   setup executable in the packaging pipeline.
3. Repeat the installer smoke on a clean Windows user profile without developer
   tooling, then record the installer hash and machine facts.

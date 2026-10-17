# Escape the Backrooms: tested source checkpoint

This integration preserves the Quest configuration accepted on 2026-10-17:
stereoscopic VR, stationary menu rooms, a central HUD, an in-headset keyboard,
and controller-driven arms with body-aligned shoulders and recovery after
temporary pose errors. The five Lua scripts match the accepted local checkpoint
`backrooms-vr-tested-2026-10-17` byte-for-byte. Their SHA256 values and reference
camera calibration are recorded in
[the checkpoint manifest](../../config/unreal/checkpoints/escape-the-backrooms-2026-10-17.json).

Valve Index physical hardware, private co-op and other game builds remain
unverified. The controller driver uses UEVR's semantic left/right controller
APIs for both Quest and Valve devices. This is controller tracking; optical
bare-hand tracking is not included.

## Setup from source

This catalogue addition is source code, not a new AVRcade installer release.
Adding the catalogue entry alone does not install the keyboard DLL or comfort
scripts. Complete these steps before playing from a fresh checkout.

Requirements: the Steam-owned game, .NET 8 SDK, Visual Studio C++ Build Tools
and a connected PC VR headset through SteamVR or Virtual Desktop. The native
build downloads the UEVR SDK headers at the pinned revision. It does not download
game files.

1. Build AVRcade, run it and select **Escape the Backrooms**. Download its VR
   tools and import the pinned community profile through the game's setup flow.
   The upstream profile remains user-import-only and is not bundled here.
2. Close the game, then build and install the keyboard and comfort helpers:

   ```powershell
   # [PowerShell] Run from the repository root.
   powershell -NoProfile -ExecutionPolicy Bypass -File mods/backrooms-vr/build.ps1 -RunTests
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/install-backrooms-vr-comfort.ps1 -SkipInstalledCatalogue
   dotnet build client/VrClient.sln -c Release
   dotnet run --project client/VrClient.App -c Release
   ```

3. Launch Backrooms from that build of AVRcade. The CLI embeds and installs the
   hand driver when the imported profile exists, preserving its enable/disable
   preference and externally edited scripts. The launch uses `-dx11` and attaches
   to `Backrooms-Win64-Shipping`, rather than the Steam launcher stub.

The helper installer targets
`%APPDATA%/UnrealVRMod/Backrooms-Win64-Shipping`. Use `-ProfileDirectory` for an
alternate location. It backs up replaced helper/config files under that profile.
Keep an existing calibrated profile when restoring the accepted setup: the
manifest's camera offsets are a local reference, not universal defaults.

## Controls and behavior

See [keyboard and comfort controls](BACKROOMS-VR-COMFORT.md) and
[controller hands](BACKROOMS-CONTROLLER-HANDS.md). The catalogue includes Quest
Touch and Valve Index controller reference layouts.

Only Backrooms is added to the catalogue. Existing game entries, profiles and
controller maps are preserved. No game assets, imported community-profile data,
private profile backup or compiled binaries are included.

## Verification

The public catalogue integration passed 145 Lua regression tests, 437 managed
Core tests, a full solution build with zero warnings/errors, the CLI launch dry
run, a fresh native keyboard build/test run, catalogue schema checks and helper
installation into a temporary profile. The original local checkpoint had 458
managed tests, including local work excluded from this public addition. Tests
exercise simulated controllers and game widgets; they do not establish
additional hardware compatibility.

To repeat source checks, install Python 3 with `lupa` and `jsonschema`, then run:

```powershell
# [PowerShell]
python -m unittest discover -s mods/backrooms-vr/tests -p 'test_*.py'
python tests/native/tooling/validate_orchestration_catalog.py
dotnet test client/VrClient.Core.Tests/VrClient.Core.Tests.csproj -c Release
dotnet build client/VrClient.Cli/VrClient.Cli.csproj -c Release
powershell -NoProfile -ExecutionPolicy Bypass -File mods/backrooms-vr/build.ps1 -RunTests
```

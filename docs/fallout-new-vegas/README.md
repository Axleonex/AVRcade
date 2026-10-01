# Fallout: New Vegas VR in VRClient

Status: native OpenXR stereo, profile preparation, prerequisite checks, and combined FNVR/native launch orchestration are implemented. End-to-end automatic third-party dependency installation is not implemented. Physical headset validation remains mandatory; vorpX is not used.

## Desktop setup (no commands)

1. Open VRClient and select **Fallout: New Vegas** in the game library.
2. Choose **Set up headset test**. VRClient automatically detects the supported
   Steam/GOG game and common locations for SteamVR, Virtual Desktop, MO2, FNVR
   Tracker, and the VRClient x86 native adapter. Use **Browse** for anything installed elsewhere.
3. When the game is detected, the dialog automatically prepares a fresh **New Vegas VR**
   profile in VRClient's local application data. No existing MO2 profile or manual
   instance-folder selection is required. To clone an existing profile instead,
   select it under **Advanced: game and profile locations**, then choose **Prepare profile**.
4. Existing VR-profile files are never overwritten, the normal profile is not
   reordered, and saves are not copied. Creating profile files does not install
   MO2 or configure its active instance; that integration remains incomplete for a fresh PC.
5. Install or enable the official, user-owned FNVR dependencies in the isolated
   profile. Choose **Check readiness** until every required row passes.
6. Connect the headset and choose **Launch headset test**. VRClient starts the
   visible Virtual Desktop route when selected, SteamVR, FNVR Tracker,
   MO2/New Vegas, and the native OpenXR attachment in the required order.
7. After leaving the game, choose **End test session**. VRClient stops only the
   FNVR Tracker and native adapter processes that it started; SteamVR and Virtual Desktop
   remain under user control.

The desktop flow deliberately does not download restricted packages. Those remain visible user-owned installation steps.

VRClient combines an in-repository DirectX 9-to-OpenXR native stereo adapter with the open-source FNVR gameplay stack. It does not use vorpX, download restricted packages, or modify Bethesda assets. Steam and GOG are supported; native attachment is fail-closed to explicitly verified executable builds. Manual paths are accepted with an explicit unverified-edition warning. Epic, Microsoft Store/Game Pass, and Bethesda.net layouts are rejected because the supported xNVSE/4GB stack does not support them.

## Architecture

| Surface | Implementation | Safety property |
|---|---|---|
| Discovery | `FalloutNewVegasDiscovery` | Steam AppID 22380, GOG registry/common paths, manual override, SHA-256 build identity, wrong-game diagnostics |
| Preflight | `FalloutNewVegasPrerequisiteValidator` | PE LAA check, xNVSE version floor, MO2 virtualized-mod warnings, FNVR enablement, runtime/path diagnostics |
| Conversion | `FalloutNewVegasMo2ProfileService` | dry-run, acknowledgement gate, separate profile, copy-if-missing, managed-file backups, idempotent repair journal, conservative restore |
| Compatibility | `FalloutNewVegasCompatibilityAnalyzer` | verified/warned/incompatible/unknown report; no automatic disabling or reordering |
| Comfort | `FalloutNewVegasComfortPlanner` | guidance/report only; no invented FNVR keys |
| Launch | `FalloutNewVegasLaunchOrchestrator` | Virtual Desktop when selected, SteamVR, FNVR Tracker, MO2 `-p <profile> nvse_loader.exe`, then native attachment; per-component logs |
| CLI | `VrClient.FalloutNewVegas.Cli` | JSON output, actionable exit status, mutation acknowledgement, dry-run launch |

The integration remains self-contained in `VrClient.Core`; both the desktop app
and standalone CLI call the same discovery, preflight, isolated-profile, and
launch services.

## Build and use

`[PowerShell] dotnet build client\VrClient.FalloutNewVegas.Cli\VrClient.FalloutNewVegas.Cli.csproj`

Discover installs:

`[PowerShell] dotnet run --project client\VrClient.FalloutNewVegas.Cli -- discover`

Preflight an isolated profile:

`[PowerShell] dotnet run --project client\VrClient.FalloutNewVegas.Cli -- preflight --game-dir "C:\Games\Fallout New Vegas" --storefront steam --mo2-exe "C:\MO2\ModOrganizer.exe" --profile-dir "C:\MO2\profiles\New Vegas VR" --tracker "C:\FNVR\Fallout-New_Virtual_Reality.exe" --native-adapter "<repo>\build\fnv-x86\fnv-test-launcher.exe" --steamvr "C:\Program Files (x86)\Steam\steamapps\common\SteamVR"`

Always preview conversion first:

`[PowerShell] dotnet run --project client\VrClient.FalloutNewVegas.Cli -- convert --mo2-instance "C:\MO2" --source-profile "Normal Modded" --vr-profile "New Vegas VR" --game-dir "C:\Games\Fallout New Vegas" --dry-run`

After reviewing the JSON operations, repeat with `--acknowledge`. Use the same arguments with `repair` to recover or reconcile managed state. `restore --acknowledge` removes only `vrclient-fallout-new-vegas.json`; it retains the cloned profile and every mod/profile file for conservative recovery.

Generate compatibility and comfort reports before launch:

`[PowerShell] dotnet run --project client\VrClient.FalloutNewVegas.Cli -- compatibility --profile-dir "C:\MO2\profiles\New Vegas VR" --game-dir "C:\Games\Fallout New Vegas" --rules config\modpacks\fallout-new-vegas.compatibility-rules.json --output artifacts\fallout-new-vegas\compatibility.json`

`[PowerShell] dotnet run --project client\VrClient.FalloutNewVegas.Cli -- comfort --play-mode seated --turn-mode snap --movement-orientation controller --dominant-hand right --performance-preset balanced --output artifacts\fallout-new-vegas\comfort.json`

Launch first as a dry run, then omit `--dry-run`. For Quest streaming, add `--virtual-desktop --virtual-desktop-exe <path-to-Streamer>`; SteamVR remains the PC runtime path.

## Compatibility behavior

- Source and VR profiles remain separate. Profile cloning copies configuration only and never touches MO2's `mods` directory or save directories.
- Enabled mod names are matched against evidence-bounded category rules. Warnings never mutate the profile.
- `d3d9.dll`, `dxgi.dll`, `dinput8.dll`, and `opengl32.dll` in the game root are reported as possible renderer/input-hook conflicts; VRClient never deletes them.
- Normal ESP/ESM and xNVSE behavior remains under MO2. `FNVR.esp` must be enabled explicitly.
- Unknown means unverified, not incompatible. No blanket compatibility claim is made.

## Actionable diagnostics

- `wrong_directory` / `wrong_game_executable_detected`: select the folder containing `FalloutNV.exe` and `FalloutNVLauncher.exe`.
- `unsupported_edition`: use a legitimate Steam or GOG install supported by xNVSE and FNV 4GB Patcher.
- `large_address_aware_missing`: run official FNV 4GB Patcher 1.5, or apply a verified large-address-aware flag to an exact supported build when launching explicitly through `nvse_loader.exe`, then recheck. Always retain a hash-verified pristine backup.
- `xnvse_missing` / `xnvse_version_too_old`: install current xNVSE in the game root; do not install its root files as an ordinary MO2 mod.
- `jip_pp_ln_nvse_missing`: install JIP PP LN from Nexus 88687. FNVR V2 links this maintained fork; it still installs as `jip_nvse.dll`.
- `*_mo2_virtualized`: the enabled mod is visible in `modlist.txt`, but its DLL/ESP can only be proven after MO2 creates the VFS; check launch logs/version in-game.
- `fnvr_esp_disabled`: enable `FNVR.esp` in the isolated profile; VRClient will not change load order.
- `steamvr_openxr_process_override`: another global OpenXR runtime is active. VRClient leaves it unchanged and sets `XR_RUNTIME_JSON` to SteamVR only on the MO2/xNVSE process, so the native renderer and FNVR tracker share SteamVR coordinates without disrupting other VR applications.
- `virtual_desktop_streamer_missing`: connect Virtual Desktop Streamer before selecting the VD-to-SteamVR route.
- `launch failed at <component>`: inspect that component's log in the selected log directory; earlier user-owned processes are not terminated.

## Evidence boundary

Automated tests use text fixtures and synthetic PE headers. They prove orchestration policy, file safety, parsing, idempotency, report structure, and failure behavior. They do not prove stereo rendering, projection, tracking, controller input, weapon alignment, comfort, save compatibility, or commercial software activation. Follow [HUMAN-VERIFICATION.md](HUMAN-VERIFICATION.md) for those gates.

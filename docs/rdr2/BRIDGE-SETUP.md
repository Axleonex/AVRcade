# RDR2 Story bridge setup

The first RDR2 bridge is a Windows/D3D12 smoke payload. It is deliberately
Story Mode only and does not include ScriptHookRDR2, Rockstar files, or any
Online path.

## Build and stage

From a mapped `G:` checkout (CMake cannot generate Ninja builds from the UNC
path), configure and build the optional bridge:

```powershell
$env:Path = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64;C:\Program Files\LLVM\bin;' + $env:Path
cmake -S <repo> -B <repo>/build/rdr2-bridge-check-g -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH='<repo>/external/openxr;<repo>/external/vulkan' `
  -DVRCLIENT_BUILD_OPENXR_RUNTIME=ON -DVRCLIENT_BUILD_TESTS=OFF -DVRCLIENT_BUILD_RDR2_BRIDGE=ON
cmake --build <repo>/build/rdr2-bridge-check-g --target vrclient_rdr2_bridge
```

The build stages `vrclient_rdr2_bridge.dll`, `vulkan-1.dll`, and
`runtime-profile.json` below `artifacts/adapters/rdr2/`. The profile command
renames the bridge to the `.asi` extension required by ScriptHookRDR2 and keeps
all staged files outside the game root:

```powershell
dotnet run --project client/VrClient.Cli/VrClient.Cli.csproj -c Release --no-build -- rdr2-profile create baseline
dotnet run --project client/VrClient.Cli/VrClient.Cli.csproj -c Release --no-build -- rdr2-profile stage-bridge baseline
dotnet run --project client/VrClient.Cli/VrClient.Cli.csproj -c Release --no-build -- rdr2-profile validate baseline
```

## User-owned prerequisite and guarded launch

Install a compatible, user-obtained `ScriptHookRDR2.dll` and its ASI loader
`dinput8.dll` in
`X:\SteamLibrary\steamapps\common\Red Dead Redemption 2`. ScriptHook alone
does not load `.asi` plugins. Do not use this
profile for Red Dead Online. The guarded command applies the validated profile,
passes the Story marker and `-dx12` through Steam's normal ownership chain, and
refuses if any prerequisite is absent:

```powershell
dotnet run --project client/VrClient.Cli/VrClient.Cli.csproj -c Release --no-build -- rdr2-launch --mode story --profile baseline
```

After closing the game, restore the exact pre-profile files:

```powershell
dotnet run --project client/VrClient.Cli/VrClient.Cli.csproj -c Release --no-build -- rdr2-profile disable baseline
```

During the one early smoke run, capture live evidence while the game is stable:

```powershell
dotnet run --project client/VrClient.Cli/VrClient.Cli.csproj -c Release --no-build -- rdr2-live-diagnostics --mode story --run-id smoke-story-mode-01
```

The current payload is a monoscopic scene relay rendered to both OpenXR eyes,
with orientation-based camera forwarding and keyboard fallback controls. A
successful build is not a verified claim of stereo, tracking, or full
playability; those remain the explicit headset smoke gate.

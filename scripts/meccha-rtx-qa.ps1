#Requires -Version 5.1
# STATUS: LEGACY UEVR FALLBACK QA ONLY.
# This script cannot close Phase 10 native NUE-* requirements. Use
# PASSOVER-NATIVE-MECCHA.md for the current native route.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Probe', 'Apply', 'Record')]
    [string]$Mode,

    [string]$HeadBone,
    [string]$PaintProperty,
    [ValidateSet('pawn', 'controller')]
    [string]$PaintOwner = 'pawn',
    [AllowNull()][string]$PaintEquals = $null,
    [string]$SessionDir,

    [switch]$PrivateRoomConfirmed,
    [switch]$StereoConfirmed,
    [switch]$HeadTrackingConfirmed,
    [switch]$PaintSwitchConfirmed
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$Slug = 'meccha-chameleon'
$GameConfig = Join-Path $RepoRoot "config\unreal\$Slug.uevr.json"
$ProfileRoot = Join-Path $RepoRoot "config\unreal\profiles\$Slug"
$LuaPath = Join-Path $ProfileRoot 'scripts\first_person_head.lua'
$ArtifactRoot = Join-Path $RepoRoot "artifacts\unreal\$Slug"

function Resolve-Cli {
    $packaged = Join-Path $RepoRoot 'build\package\windows\payload\vrclient.exe'
    if (Test-Path -LiteralPath $packaged) { return $packaged }
    $dev = Join-Path $RepoRoot 'client\VrClient.Cli\bin\Release\net8.0\vrclient.exe'
    if (Test-Path -LiteralPath $dev) { return $dev }
    throw 'vrclient.exe not found; build the Release CLI or the Windows package first'
}

function Get-GpuFacts {
    @(Get-CimInstance Win32_VideoController | ForEach-Object {
        [ordered]@{ name = $_.Name; driver_version = $_.DriverVersion; adapter_ram = $_.AdapterRAM }
    })
}

function Assert-Rtx([object[]]$Gpus) {
    if (-not ($Gpus | Where-Object { $_.name -match 'NVIDIA.*RTX|RTX.*NVIDIA|GeForce RTX' })) {
        throw "RTX gate failed: this workflow must run on the RTX machine; found $($Gpus.name -join ', ')"
    }
}

function Get-SteamRoots {
    $roots = New-Object System.Collections.Generic.List[string]
    foreach ($candidate in @(
        (Get-ItemPropertyValue 'HKCU:\Software\Valve\Steam' SteamPath -ErrorAction SilentlyContinue),
        'C:\Program Files (x86)\Steam',
        'C:\Program Files\Steam'
    )) {
        if ($candidate -and (Test-Path -LiteralPath $candidate) -and -not $roots.Contains($candidate)) {
            $roots.Add($candidate)
        }
    }
    foreach ($steam in @($roots)) {
        $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
        if (-not (Test-Path -LiteralPath $vdf)) { continue }
        foreach ($match in [regex]::Matches((Get-Content -Raw -LiteralPath $vdf), '"path"\s+"([^"]+)"')) {
            $path = $match.Groups[1].Value -replace '\\\\', '\'
            if ((Test-Path -LiteralPath $path) -and -not $roots.Contains($path)) { $roots.Add($path) }
        }
    }
    return $roots
}

function Find-MecchaInstall {
    foreach ($root in Get-SteamRoots) {
        $game = Join-Path $root 'steamapps\common\MECCHA CHAMELEON'
        $shipping = Join-Path $game 'Chameleon\Binaries\Win64\PenguinHotel-Win64-Shipping.exe'
        $manifest = Join-Path $root 'steamapps\appmanifest_4704690.acf'
        if (Test-Path -LiteralPath $shipping) {
            $buildId = $null
            if (Test-Path -LiteralPath $manifest) {
                $m = [regex]::Match((Get-Content -Raw -LiteralPath $manifest), '"buildid"\s+"(\d+)"')
                if ($m.Success) { $buildId = $m.Groups[1].Value }
            }
            return [ordered]@{ game_dir = $game; shipping_exe = $shipping; manifest = $manifest; build_id = $buildId }
        }
    }
    throw 'MECCHA CHAMELEON shipping executable was not found in a Steam library on this machine'
}

function Get-VrProcesses {
    $names = @('vrserver', 'vrmonitor', 'vrcompositor', 'VirtualDesktop.Streamer', 'VirtualDesktop.Server')
    @($names | Where-Object { Get-Process -Name $_ -ErrorAction SilentlyContinue } | Select-Object -Unique)
}

function Convert-ToLuaLiteral([AllowNull()][string]$Value) {
    if ($null -eq $Value -or $Value -eq '') { return 'nil' }
    if ($Value -match '^(true|false)$') { return $Value.ToLowerInvariant() }
    $number = 0.0
    if ([double]::TryParse($Value, [Globalization.NumberStyles]::Float,
        [Globalization.CultureInfo]::InvariantCulture, [ref]$number)) {
        return $number.ToString([Globalization.CultureInfo]::InvariantCulture)
    }
    return '"' + $Value.Replace('\', '\\').Replace('"', '\"') + '"'
}

function Require-Replace([string]$Text, [string]$Pattern, [string]$Replacement, [string]$Label) {
    $matches = [regex]::Matches($Text, $Pattern, [Text.RegularExpressions.RegexOptions]::Multiline)
    if ($matches.Count -ne 1) { throw "$Label patch expected one match, found $($matches.Count)" }
    return [regex]::Replace($Text, $Pattern, $Replacement, [Text.RegularExpressions.RegexOptions]::Multiline)
}

function Write-JsonUtf8([string]$Path, [object]$Value) {
    $json = $Value | ConvertTo-Json -Depth 12
    [IO.File]::WriteAllText($Path, $json + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
}

if ($Mode -eq 'Apply') {
    if ([string]::IsNullOrWhiteSpace($HeadBone) -or [string]::IsNullOrWhiteSpace($PaintProperty)) {
        throw 'Apply requires both -HeadBone and -PaintProperty from the live UObjectHook probe'
    }
    $lua = Get-Content -Raw -LiteralPath $LuaPath
    $escapedBone = $HeadBone.Replace('\', '\\').Replace('"', '\"')
    $escapedProperty = $PaintProperty.Replace('\', '\\').Replace('"', '\"')
    $lua = Require-Replace $lua '^local CANDIDATES = \{.*\}$' "local CANDIDATES = { `"$escapedBone`" }" 'head bone'
    $lua = Require-Replace $lua '^\s+on = "(?:pawn|controller)",$' "    on = `"$PaintOwner`"," 'paint owner'
    $lua = Require-Replace $lua '^\s+property = (?:nil|"[^"]*"),$' "    property = `"$escapedProperty`"," 'paint property'
    $lua = Require-Replace $lua '^\s+equals = (?:nil|true|false|-?[0-9.]+|"[^"]*"),$' "    equals = $(Convert-ToLuaLiteral $PaintEquals)," 'paint equals'
    $lua = Require-Replace $lua '^local PROBE_STATE = (?:true|false).*$' 'local PROBE_STATE = false  -- locked after RTX/private-room discovery' 'probe flag'
    [IO.File]::WriteAllText($LuaPath, $lua, [Text.UTF8Encoding]::new($false))

    $config = Get-Content -Raw -LiteralPath $GameConfig | ConvertFrom-Json
    $config.verification.injection_stable = $false
    $config.verification.headset_t1 = $false
    $config.verification.profile_status = 'unverified'
    $config.verification.head_bone = $HeadBone
    $config.verification.paint_mode_property = "$PaintOwner.$PaintProperty"
    $config.verification.evidence = $null
    Write-JsonUtf8 $GameConfig $config
    Write-Host "RESULT: meccha-qa apply ok head_bone=$HeadBone paint=$PaintOwner.$PaintProperty equals=$(Convert-ToLuaLiteral $PaintEquals) status=unverified"
    exit 0
}

$gpus = Get-GpuFacts
Assert-Rtx $gpus
$install = Find-MecchaInstall
$vrProcesses = Get-VrProcesses
if ($vrProcesses.Count -eq 0) {
    throw 'headset gate failed: start Virtual Desktop or SteamVR with the headset connected'
}
if ($Mode -eq 'Probe') {
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    $SessionDir = Join-Path $ArtifactRoot $stamp
    New-Item -ItemType Directory -Force -Path $SessionDir | Out-Null
    $facts = [ordered]@{
        schema = 'vrclient-meccha-rtx-probe/1'
        captured_at_utc = [DateTime]::UtcNow.ToString('o')
        machine = $env:COMPUTERNAME
        os = [Environment]::OSVersion.VersionString
        gpu = $gpus
        steam = $install
        vr_processes = $vrProcesses
        private_room_planned = [bool]$PrivateRoomConfirmed
        catalog_profile_sha256 = (Get-FileHash -LiteralPath $LuaPath -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    Write-JsonUtf8 (Join-Path $SessionDir 'machine-preflight.json') $facts

    $cli = Resolve-Cli
    & $cli uevr-prepare $Slug 2>&1 | Tee-Object -FilePath (Join-Path $SessionDir 'prepare.log')
    if ($LASTEXITCODE -ne 0) { throw "uevr-prepare failed with exit code $LASTEXITCODE" }
    & $cli uevr-launch $Slug --acknowledge --push-profile --attach-timeout 120 --attach-delay 10 2>&1 |
        Tee-Object -FilePath (Join-Path $SessionDir 'inject.log')
    if ($LASTEXITCODE -ne 0) { throw "uevr-launch failed with exit code $LASTEXITCODE" }

    $profile = Join-Path $env:APPDATA 'UnrealVRMod\PenguinHotel-Win64-Shipping'
    if (Test-Path -LiteralPath $profile) {
        $snapshot = Join-Path $SessionDir 'deployed-profile'
        Copy-Item -LiteralPath $profile -Destination $snapshot -Recurse
    }
    Write-Host "RESULT: meccha-qa probe ok session=$SessionDir"
    Write-Host 'NEXT: discover the printed head bone and paint-mode field, run Apply, then repeat Probe to test the locked profile.'
    exit 0
}

if (-not $PrivateRoomConfirmed) {
    throw 'private-room gate failed: Record only after the completed QA run stayed in a private friend room'
}
if (-not $StereoConfirmed -or -not $HeadTrackingConfirmed -or -not $PaintSwitchConfirmed) {
    throw 'Record requires -StereoConfirmed -HeadTrackingConfirmed -PaintSwitchConfirmed and -PrivateRoomConfirmed'
}
if (-not $SessionDir) {
    $SessionDir = Get-ChildItem -LiteralPath $ArtifactRoot -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $SessionDir -or -not (Test-Path -LiteralPath $SessionDir)) { throw 'no Meccha QA session directory found' }
$injectLog = Join-Path $SessionDir 'inject.log'
if (-not (Test-Path -LiteralPath $injectLog)) { throw "missing injection transcript: $injectLog" }
$injectText = Get-Content -Raw -LiteralPath $injectLog
if ($injectText -notmatch 'RESULT: uevr-launch ok .*backend=hooked') {
    throw 'injection-stability gate failed: the session transcript does not contain backend=hooked'
}

$config = Get-Content -Raw -LiteralPath $GameConfig | ConvertFrom-Json
if ([string]::IsNullOrWhiteSpace($config.verification.head_bone) -or
    [string]::IsNullOrWhiteSpace($config.verification.paint_mode_property)) {
    throw 'profile lock gate failed: run Apply with the discovered head bone and paint property first'
}
$deployedLua = Join-Path $env:APPDATA 'UnrealVRMod\PenguinHotel-Win64-Shipping\scripts\first_person_head.lua'
if (-not (Test-Path -LiteralPath $deployedLua)) { throw "deployed Lua profile missing: $deployedLua" }
$catalogHash = (Get-FileHash -LiteralPath $LuaPath -Algorithm SHA256).Hash.ToLowerInvariant()
$deployedHash = (Get-FileHash -LiteralPath $deployedLua -Algorithm SHA256).Hash.ToLowerInvariant()
if ($catalogHash -ne $deployedHash) { throw 'profile parity gate failed: deployed Lua does not match the catalog Lua; repeat Probe after Apply' }

$record = [ordered]@{
    schema = 'vrclient-meccha-headset-evidence/1'
    recorded_at_utc = [DateTime]::UtcNow.ToString('o')
    machine = $env:COMPUTERNAME
    gpu = $gpus
    steam = $install
    vr_processes = $vrProcesses
    profile = [ordered]@{
        head_bone = $config.verification.head_bone
        paint_mode_property = $config.verification.paint_mode_property
        catalog_lua_sha256 = $catalogHash
        deployed_lua_sha256 = $deployedHash
    }
    gates = [ordered]@{
        private_room = $true
        backend_hook_signal = $true
        stereo_user_observed = $true
        head_tracking_user_observed = $true
        paint_switch_user_observed = $true
    }
    source_session = (Resolve-Path -LiteralPath $SessionDir).Path
    injection_transcript_sha256 = (Get-FileHash -LiteralPath $injectLog -Algorithm SHA256).Hash.ToLowerInvariant()
}
$evidencePath = Join-Path $SessionDir 'headset-evidence.json'
Write-JsonUtf8 $evidencePath $record
$relativeEvidence = $evidencePath.Substring($RepoRoot.Length + 1).Replace('\', '/')
$config.verification.injection_stable = $true
$config.verification.headset_t1 = $true
$config.verification.profile_status = 'verified'
$config.verification.evidence = $relativeEvidence
Write-JsonUtf8 $GameConfig $config
Write-Host "RESULT: meccha-qa record ok evidence=$relativeEvidence profile=verified"

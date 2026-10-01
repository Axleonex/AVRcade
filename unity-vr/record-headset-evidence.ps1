#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidateSet('T1', 'T2')][string]$Tier,
    [Parameter(Mandatory = $true)][string]$GameDir,
    [Parameter(Mandatory = $true)][ValidatePattern('^[a-z0-9][a-z0-9-]*$')][string]$Slug,
    [string]$SteamBuildId = '',
    [switch]$StereoConfirmed,
    [switch]$HeadTrackingConfirmed,
    [switch]$InputActivityConfirmed,
    [switch]$InGameInputConfirmed
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$GameDir = [IO.Path]::GetFullPath($GameDir)
$logPath = Join-Path $GameDir 'BepInEx\LogOutput.log'
$pluginDir = Join-Path $GameDir 'BepInEx\plugins\VRClient-UnityVR'
$pluginPath = Join-Path $pluginDir 'VrClient.UnityVR.dll'
$logicPath = Join-Path $pluginDir 'VrClient.UnityVR.Logic.dll'
$configPath = Join-Path $GameDir "BepInEx\config\vrclient-unityvr.$Slug.json"

foreach ($required in @($logPath, $pluginPath, $logicPath, $configPath)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "evidence input missing: $required" }
}
$log = Get-Content -Raw -LiteralPath $logPath
foreach ($token in @('UXR-BOOT-OK', 'UXR-RIG:', 'UXR-READY:')) {
    if ($log -notmatch [regex]::Escape($token)) { throw "T1 log gate failed: missing $token" }
}

$artifactRoot = Join-Path $RepoRoot "artifacts\unity-vr\$Slug"
if ($Tier -eq 'T1') {
    if (-not $StereoConfirmed -or -not $HeadTrackingConfirmed) {
        throw 'T1 requires -StereoConfirmed and -HeadTrackingConfirmed after direct headset observation'
    }
}
else {
    $priorT1 = Get-ChildItem -LiteralPath $artifactRoot -Recurse -Filter 't1-evidence.json' -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    if (-not $priorT1) { throw 'T2 ordering gate failed: record T1 stereo/tracking evidence first' }
    foreach ($token in @('UXR-INPUT: bound', 'UXR-INPUT: controller activity observed')) {
        if ($log -notmatch [regex]::Escape($token)) { throw "T2 log gate failed: missing $token" }
    }
    if (-not $InputActivityConfirmed -or -not $InGameInputConfirmed) {
        throw 'T2 requires controller activity plus observed in-game look/locomotion; framework input alone is insufficient'
    }
}

$stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
$session = Join-Path $artifactRoot $stamp
New-Item -ItemType Directory -Force -Path $session | Out-Null
Copy-Item -LiteralPath $logPath -Destination (Join-Path $session 'BepInEx-LogOutput.log')
$gpus = @(Get-CimInstance Win32_VideoController | ForEach-Object {
    [ordered]@{ name=$_.Name; driver_version=$_.DriverVersion; adapter_ram=$_.AdapterRAM }
})
$evidence = [ordered]@{
    schema = 'vrclient-unityvr-headset-evidence/1'
    tier = $Tier
    recorded_at_utc = [DateTime]::UtcNow.ToString('o')
    slug = $Slug
    steam_build_id = $SteamBuildId
    machine = $env:COMPUTERNAME
    gpu = $gpus
    files = [ordered]@{
        plugin_sha256 = (Get-FileHash -LiteralPath $pluginPath -Algorithm SHA256).Hash.ToLowerInvariant()
        logic_sha256 = (Get-FileHash -LiteralPath $logicPath -Algorithm SHA256).Hash.ToLowerInvariant()
        config_sha256 = (Get-FileHash -LiteralPath $configPath -Algorithm SHA256).Hash.ToLowerInvariant()
        log_sha256 = (Get-FileHash -LiteralPath $logPath -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    gates = [ordered]@{
        boot_ok = $true
        stereo_rig_active = $true
        stereo_user_observed = [bool]$StereoConfirmed
        head_tracking_user_observed = [bool]$HeadTrackingConfirmed
        controller_activity_logged = ($Tier -eq 'T2')
        in_game_input_user_observed = [bool]$InGameInputConfirmed
    }
}
$name = if ($Tier -eq 'T1') { 't1-evidence.json' } else { 't2-evidence.json' }
$json = $evidence | ConvertTo-Json -Depth 8
[IO.File]::WriteAllText((Join-Path $session $name), $json + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
Write-Host "RESULT: unityvr-evidence ok tier=$Tier session=$session"

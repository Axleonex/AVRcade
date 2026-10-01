#Requires -Version 5.1
[CmdletBinding()]
param(
    [switch]$PrivateOfflineConfirmed,
    [string]$BuildDir = 'build/meccha-observer',
    [int]$EvidenceTimeoutSeconds = 90
)

$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Set-Location -LiteralPath $root

function Stop-WithBlocker {
    param([Parameter(Mandatory = $true)][string]$Message)
    Write-Host "RESULT: BLOCKED - $Message" -ForegroundColor Red
    exit 20
}

if (-not $PrivateOfflineConfirmed) {
    Stop-WithBlocker 'pass -PrivateOfflineConfirmed only for the approved private/offline Meccha test'
}

$runningGame = @(Get-Process -ErrorAction SilentlyContinue |
    Where-Object { $_.ProcessName -match '^PenguinHotel($|-Win64-Shipping$)' })
if ($runningGame.Count -gt 0) {
    Stop-WithBlocker 'Meccha is already running; close it normally before this controlled test'
}

$blockedProcesses = @(Get-Process -ErrorAction SilentlyContinue |
    Where-Object {
        $_.ProcessName -match 'EasyAntiCheat|BEService|BattlEye|UEVR|UnrealVRMod'
    })
if ($blockedProcesses.Count -gt 0) {
    Stop-WithBlocker ('an anti-cheat or external-injector process is active: ' +
        (($blockedProcesses | Select-Object -ExpandProperty ProcessName -Unique) -join ', '))
}

$blockedServices = @(Get-CimInstance Win32_Service -ErrorAction SilentlyContinue |
    Where-Object {
        $_.State -eq 'Running' -and
        ($_.Name -match 'EasyAntiCheat|BEService|BattlEye' -or
         $_.DisplayName -match 'Easy Anti-Cheat|BattlEye')
    })
if ($blockedServices.Count -gt 0) {
    Stop-WithBlocker ('an anti-cheat service is running: ' +
        (($blockedServices | Select-Object -ExpandProperty Name -Unique) -join ', '))
}

$preflightOutput = @(
    dotnet run --project client\VrClient.Cli\VrClient.Cli.csproj -- `
        native-preflight meccha-chameleon 2>&1
)
if ($LASTEXITCODE -ne 0) {
    $preflightOutput | ForEach-Object { Write-Host $_ }
    Stop-WithBlocker 'exact Meccha build/hash preflight failed'
}
$executableLine = $preflightOutput |
    Where-Object { $_ -like 'NATIVE-PREFLIGHT: executable=*' } |
    Select-Object -Last 1
if (-not $executableLine) {
    Stop-WithBlocker 'preflight did not resolve the shipping executable'
}
$shippingExe = $executableLine.Substring('NATIVE-PREFLIGHT: executable='.Length)

Set-ExecutionPolicy -Scope Process Bypass -Force
. (Join-Path $root 'tools\setup-build-env.ps1') -Compiler msvc -Quiet
$resolvedBuildDir = [System.IO.Path]::GetFullPath((Join-Path $root $BuildDir))
& $env:VRCLIENT_CMAKE_EXE -S $root -B $resolvedBuildDir -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$($env:VRCLIENT_NINJA_EXE)" `
    -DVRCLIENT_BUILD_OPENXR_RUNTIME=OFF `
    -DVRCLIENT_BUILD_TESTS=ON `
    -DVRCLIENT_ENABLE_DETOURS_SMOKE=ON `
    -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) {
    Stop-WithBlocker 'camera observer configure failed'
}
& $env:VRCLIENT_CMAKE_EXE --build $resolvedBuildDir --target `
    vrclient_meccha_camera_observer_payload vrclient_dll_attach_loader `
    vr_native_unreal_tests vr_hookdisc_unit_tests
if ($LASTEXITCODE -ne 0) {
    Stop-WithBlocker 'camera observer build failed'
}
& (Join-Path $resolvedBuildDir 'vr_native_unreal_tests.exe')
if ($LASTEXITCODE -ne 0) { Stop-WithBlocker 'native Unreal tests failed' }
& (Join-Path $resolvedBuildDir 'vr_hookdisc_unit_tests.exe')
if ($LASTEXITCODE -ne 0) { Stop-WithBlocker 'hardware observer tests failed' }

$payload = Join-Path $resolvedBuildDir 'vrclient_meccha_camera_observer_payload.dll'
$attachLoader = Join-Path $resolvedBuildDir 'vrclient_dll_attach_loader.exe'
if (-not (Test-Path -LiteralPath $payload) -or
    -not (Test-Path -LiteralPath $attachLoader)) {
    Stop-WithBlocker 'camera payload or attach loader was not produced'
}

$timestamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
$artifactDir = Join-Path $root "artifacts\meccha-camera-observer\$timestamp"
New-Item -ItemType Directory -Path $artifactDir -Force | Out-Null
$evidenceFile = Join-Path $artifactDir 'session-evidence.json'
$preflightOutput | Set-Content -LiteralPath (Join-Path $artifactDir 'preflight.log')

Start-Process 'steam://run/4704690'
$shippingProcess = $null
$launchDeadline = (Get-Date).AddSeconds(120)
do {
    Start-Sleep -Milliseconds 500
    $shippingProcess = Get-Process -ErrorAction SilentlyContinue |
        Where-Object { $_.ProcessName -eq 'PenguinHotel-Win64-Shipping' } |
        Select-Object -First 1
} while (-not $shippingProcess -and (Get-Date) -lt $launchDeadline)
if (-not $shippingProcess) {
    Stop-WithBlocker 'Steam did not start the Meccha shipping process within 120 seconds'
}
if (-not $shippingProcess.Path -or
    -not [System.IO.Path]::GetFullPath($shippingProcess.Path).Equals(
        [System.IO.Path]::GetFullPath($shippingExe),
        [System.StringComparison]::OrdinalIgnoreCase)) {
    Stop-WithBlocker "Steam started an unexpected shipping process: $($shippingProcess.Path)"
}

Start-Sleep -Seconds 8
$attachOutput = @(
    & $attachLoader --pid $shippingProcess.Id --dll $payload `
        --evidence $evidenceFile 2>&1
)
$attachOutput | Set-Content -LiteralPath (Join-Path $artifactDir 'attach.log')
if ($LASTEXITCODE -ne 0) {
    $attachOutput | ForEach-Object { Write-Host $_ }
    Stop-WithBlocker 'camera observation-only attach failed'
}

$deadline = (Get-Date).AddSeconds($EvidenceTimeoutSeconds)
do {
    Start-Sleep -Milliseconds 500
    if ($shippingProcess.HasExited) {
        Stop-WithBlocker 'Meccha exited before camera evidence was ready'
    }
    if (Test-Path -LiteralPath $evidenceFile) {
        try {
            $evidence = Get-Content -LiteralPath $evidenceFile -Raw | ConvertFrom-Json
            if ($evidence.status -ne 'observing') {
                Stop-WithBlocker "camera provider refused: $($evidence.status)"
            }
            $stableValidationInterval =
                $evidence.consecutive_clean_capture_count -ge 3 -and
                $evidence.recent_candidate_count -eq 1 -and
                $evidence.validation_rejected_sample_count -eq 0 -and
                $evidence.validation_capture_fault_count -eq 0 -and
                $evidence.validation_missing_observation_count -eq 0 -and
                $evidence.validation_ambiguous_observation_count -eq 0
            if ($evidence.camera_ready -and
                $evidence.decoded_sample_count -ge 3 -and
                $stableValidationInterval) {
                Write-Host "CAMERA: id=$($evidence.camera_id) samples=$($evidence.decoded_sample_count)"
                Write-Host "CAMERA: location=$($evidence.location_uu -join ',')"
                Write-Host "CAMERA: rotation=$($evidence.rotation_degrees -join ',')"
                Write-Host "CAMERA: vfov=$($evidence.vertical_fov_degrees) aspect=$($evidence.aspect_ratio)"
                Write-Host "CAMERA: near=$($evidence.near_clip_uu) far=$($evidence.far_clip_uu)"
                Write-Host "EVIDENCE: $artifactDir"
                Write-Host 'RESULT: READY - exact-build read-only Meccha camera provider is stable'
                exit 0
            }
        } catch {
            # The payload atomically replaces the JSON; retry a transient read.
        }
    }
} while ((Get-Date) -lt $deadline)

if (Test-Path -LiteralPath $evidenceFile) {
    Get-Content -LiteralPath $evidenceFile -Raw | Write-Host
}
Stop-WithBlocker "no stable camera evidence appeared within $EvidenceTimeoutSeconds seconds"

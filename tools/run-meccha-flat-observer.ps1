#Requires -Version 5.1
[CmdletBinding()]
param(
    [switch]$PrivateOfflineConfirmed,
    [string]$BuildDir = 'build/meccha-observer',
    [int]$EvidenceTimeoutSeconds = 60
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
    Stop-WithBlocker 'pass -PrivateOfflineConfirmed only after choosing a private/offline Meccha test'
}

$runningGame = @(Get-Process -ErrorAction SilentlyContinue |
    Where-Object { $_.ProcessName -match '^PenguinHotel($|-Win64-Shipping$)' })
if ($runningGame.Count -gt 0) {
    Stop-WithBlocker 'Meccha is already running; close it normally before the launch-time observer test'
}

$blockedProcesses = @(Get-Process -ErrorAction SilentlyContinue |
    Where-Object {
        $_.ProcessName -match
            'EasyAntiCheat|BEService|BattlEye|UEVR|UnrealVRMod|VRClient'
    })
if ($blockedProcesses.Count -gt 0) {
    Stop-WithBlocker (
        'an anti-cheat or external-injector process is active: ' +
        (($blockedProcesses | Select-Object -ExpandProperty ProcessName -Unique) -join ', ')
    )
}

$blockedServices = @(Get-CimInstance Win32_Service -ErrorAction SilentlyContinue |
    Where-Object {
        $_.State -eq 'Running' -and
        ($_.Name -match 'EasyAntiCheat|BEService|BattlEye' -or
         $_.DisplayName -match 'Easy Anti-Cheat|BattlEye')
    })
if ($blockedServices.Count -gt 0) {
    Stop-WithBlocker (
        'an anti-cheat service is running: ' +
        (($blockedServices | Select-Object -ExpandProperty Name -Unique) -join ', ')
    )
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
if (-not (Test-Path -LiteralPath $shippingExe)) {
    Stop-WithBlocker "resolved shipping executable is missing: $shippingExe"
}

$withDll = Join-Path $root 'Detours-4.0.1\Detours-4.0.1\bin.X64\withdll.exe'
if (-not (Test-Path -LiteralPath $withDll)) {
    Stop-WithBlocker 'the approved user-built Detours withdll.exe is unavailable'
}

Set-ExecutionPolicy -Scope Process Bypass -Force
. (Join-Path $root 'tools\setup-build-env.ps1') -Compiler msvc -Quiet

$resolvedBuildDir = [System.IO.Path]::GetFullPath((Join-Path $root $BuildDir))
& $env:VRCLIENT_CMAKE_EXE -S $root -B $resolvedBuildDir -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$($env:VRCLIENT_NINJA_EXE)" `
    -DVRCLIENT_BUILD_OPENXR_RUNTIME=OFF `
    -DVRCLIENT_BUILD_TESTS=ON `
    -DVRCLIENT_ENABLE_DETOURS_SMOKE=ON `
    "-DVRCLIENT_DETOURS_WITHDLL=$($withDll.Replace('\', '/'))" `
    -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) {
    Stop-WithBlocker 'observer configure failed'
}
& $env:VRCLIENT_CMAKE_EXE --build $resolvedBuildDir `
    --target vrclient_meccha_observer_payload vrclient_dll_attach_loader
if ($LASTEXITCODE -ne 0) {
    Stop-WithBlocker 'observer payload build failed'
}

$payload = Join-Path $resolvedBuildDir 'vrclient_meccha_observer_payload.dll'
$attachLoader = Join-Path $resolvedBuildDir 'vrclient_dll_attach_loader.exe'
if (-not (Test-Path -LiteralPath $payload) -or
    -not (Test-Path -LiteralPath $attachLoader)) {
    Stop-WithBlocker 'observer payload or attach loader was not produced'
}

$timestamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
$artifactDir = Join-Path $root "artifacts\meccha-observer\$timestamp"
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

$actualProcessPath = $shippingProcess.Path
if (-not $actualProcessPath -or
    -not [System.IO.Path]::GetFullPath($actualProcessPath).Equals(
        [System.IO.Path]::GetFullPath($shippingExe),
        [System.StringComparison]::OrdinalIgnoreCase)) {
    Stop-WithBlocker "Steam started an unexpected shipping process: $actualProcessPath"
}

$postLaunchBlockedProcesses = @(Get-Process -ErrorAction SilentlyContinue |
    Where-Object {
        $_.ProcessName -match
            'EasyAntiCheat|BEService|BattlEye|UEVR|UnrealVRMod'
    })
$postLaunchBlockedServices = @(Get-CimInstance Win32_Service -ErrorAction SilentlyContinue |
    Where-Object {
        $_.State -eq 'Running' -and
        ($_.Name -match 'EasyAntiCheat|BEService|BattlEye' -or
         $_.DisplayName -match 'Easy Anti-Cheat|BattlEye')
    })
if ($postLaunchBlockedProcesses.Count -gt 0 -or
    $postLaunchBlockedServices.Count -gt 0) {
    Stop-WithBlocker 'an anti-cheat or external-injector signal appeared after Steam launch'
}

$attachOutput = @(
    & $attachLoader --pid $shippingProcess.Id --dll $payload `
        --evidence $evidenceFile 2>&1
)
$attachOutput | Set-Content -LiteralPath (Join-Path $artifactDir 'attach.log')
if ($LASTEXITCODE -ne 0) {
    $attachOutput | ForEach-Object { Write-Host $_ }
    Stop-WithBlocker 'observation-only attach failed'
}

$deadline = (Get-Date).AddSeconds($EvidenceTimeoutSeconds)
do {
    Start-Sleep -Milliseconds 500
    if ($shippingProcess.HasExited) {
        Stop-WithBlocker "Meccha exited before observer evidence was ready"
    }
    if (Test-Path -LiteralPath $evidenceFile) {
        try {
            $evidence = Get-Content -LiteralPath $evidenceFile -Raw |
                ConvertFrom-Json
            if ($evidence.ready -and $evidence.present_count -ge 4) {
                Write-Host "OBSERVER: renderer=$($evidence.renderer)"
                Write-Host "OBSERVER: size=$($evidence.width)x$($evidence.height)"
                Write-Host "OBSERVER: buffers=$($evidence.buffer_count)"
                Write-Host "OBSERVER: presents=$($evidence.present_count)"
                Write-Host "OBSERVER: resizes=$($evidence.resize_count)"
                Write-Host "EVIDENCE: $artifactDir"
                Write-Host 'RESULT: READY - observer captured the live Meccha D3D12 presentation path'
                exit 0
            }
        } catch {
            # The payload replaces the JSON atomically enough for bounded
            # observation, but a read may land between CreateFile and Flush.
        }
    }
} while ((Get-Date) -lt $deadline)

Stop-WithBlocker "no ready observer evidence appeared within $EvidenceTimeoutSeconds seconds"

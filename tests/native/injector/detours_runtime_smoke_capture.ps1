#Requires -Version 5.1
<#
.SYNOPSIS
    Optional Phase 3 / INJ-06 no-headset runtime proof using user-supplied
    Microsoft Detours `withdll.exe`.

.DESCRIPTION
    The C++ driver routes Detours launch-time DLL delivery through the existing
    `runBootstrapSmoke()` safety spine. The loaded payload calls the real
    public `vr_runtime_*` ABI from inside `vrclient_smoke_host.exe` without
    starting an OpenXR session, then writes a tokenized sentinel. The driver also
    proves a known anti-cheat safety verdict refuses before the Detours loader is
    invoked.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Driver,
    [Parameter(Mandatory = $true)][string]$WithDll,
    [Parameter(Mandatory = $true)][string]$SmokeHost,
    [Parameter(Mandatory = $true)][string]$PayloadDll,
    [Parameter(Mandatory = $true)][string]$SentinelFile,
    [Parameter(Mandatory = $true)][string]$BlockedSentinelFile,
    [Parameter(Mandatory = $true)][string]$RuntimeProfile,
    [Parameter(Mandatory = $true)][string]$GameConfig,
    [Parameter(Mandatory = $true)][string]$SafetyRules,
    [Parameter(Mandatory = $true)][string]$LogDir,
    [int]$TimeoutSeconds = 60
)

$ErrorActionPreference = 'Stop'

function Fail {
    param([string]$Message)
    Write-Host "FAIL: $Message"
    exit 1
}

function Require-Path {
    param([string]$Path, [string]$Label)
    if (-not (Test-Path -LiteralPath $Path)) {
        Fail "$Label not found: $Path"
    }
}

function Invoke-Guarded {
    param(
        [Parameter(Mandatory = $true)][string]$Exe,
        [string[]]$Arguments = @(),
        [int]$Timeout = 60
    )
    $outLog = [System.IO.Path]::GetTempFileName()
    $errLog = [System.IO.Path]::GetTempFileName()
    try {
        $proc = Start-Process -FilePath $Exe -ArgumentList $Arguments -NoNewWindow `
            -PassThru -RedirectStandardOutput $outLog -RedirectStandardError $errLog
        $null = $proc.Handle
        if (-not $proc.WaitForExit($Timeout * 1000)) {
            try { $proc.Kill() } catch { }
            $null = $proc.WaitForExit(5000)
            return @{
                ExitCode = -1
                TimedOut = $true
                StdOut = Get-Content -LiteralPath $outLog -Raw -ErrorAction SilentlyContinue
                StdErr = Get-Content -LiteralPath $errLog -Raw -ErrorAction SilentlyContinue
            }
        }
        $proc.WaitForExit()
        return @{
            ExitCode = $proc.ExitCode
            TimedOut = $false
            StdOut = Get-Content -LiteralPath $outLog -Raw -ErrorAction SilentlyContinue
            StdErr = Get-Content -LiteralPath $errLog -Raw -ErrorAction SilentlyContinue
        }
    }
    finally {
        Remove-Item -LiteralPath $outLog, $errLog -Force -ErrorAction SilentlyContinue
    }
}

function Require-Match {
    param([string]$Text, [string]$Pattern, [string]$Message)
    if (-not [regex]::IsMatch($Text, $Pattern, [Text.RegularExpressions.RegexOptions]::Multiline)) {
        Fail $Message
    }
}

Require-Path $Driver 'runtime smoke driver'
Require-Path $WithDll 'Detours withdll.exe'
Require-Path $SmokeHost 'smoke host'
Require-Path $PayloadDll 'runtime smoke payload'
Require-Path $RuntimeProfile 'runtime profile'
Require-Path $GameConfig 'game config'
Require-Path $SafetyRules 'safety rules'

foreach ($path in @($SentinelFile, $BlockedSentinelFile)) {
    $dir = Split-Path -Parent $path
    if ($dir -and -not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
    }
    Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
}
if (-not (Test-Path -LiteralPath $LogDir)) {
    New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
}

Write-Host "=== Detours runtime smoke proof ==="
Write-Host "driver  : $Driver"
Write-Host "withdll : $WithDll"
Write-Host "host    : $SmokeHost"
Write-Host "payload : $PayloadDll"
Write-Host "sentinel: $SentinelFile"

$args = @(
    '--withdll', $WithDll,
    '--smoke-host', $SmokeHost,
    '--payload-dll', $PayloadDll,
    '--sentinel', $SentinelFile,
    '--blocked-sentinel', $BlockedSentinelFile,
    '--profile', $RuntimeProfile,
    '--game-config', $GameConfig,
    '--safety-rules', $SafetyRules,
    '--log-dir', $LogDir,
    '--timeout-ms', ([string]($TimeoutSeconds * 1000))
)

$run = Invoke-Guarded -Exe $Driver -Arguments $args -Timeout $TimeoutSeconds
if ($run.StdOut) { $run.StdOut.TrimEnd().Split("`n") | ForEach-Object { Write-Host "  [driver] $_" } }
if ($run.StdErr) { $run.StdErr.TrimEnd().Split("`n") | ForEach-Object { Write-Host "  [driver:err] $_" } }
if ($run.TimedOut) { Fail "runtime smoke driver timed out after ${TimeoutSeconds}s" }
if ($run.ExitCode -ne 0) { Fail "runtime smoke driver exited $($run.ExitCode), expected 0" }

$stdout = ([string]$run.StdOut) -replace "`r", ''
Require-Match $stdout '^bootstrap_result=loaded$' 'bootstrap result did not report loaded'
Require-Match $stdout '^runtime_load_result=loaded$' 'runtime load result did not report loaded'
Require-Match $stdout '^pose_timing_api=available$' 'bootstrap did not report pose/timing API availability'
Require-Match $stdout '^game_id=vrclient-smoke-host$' 'bootstrap diagnostics missing smoke game id'
Require-Match $stdout '^build_id=smoke-2026-06-11$' 'bootstrap diagnostics missing smoke build id'
Require-Match $stdout '^safety_reason_code=approved$' 'bootstrap diagnostics missing approved safety reason'
Require-Match $stdout '^runtime_sentinel_confirmed=true$' 'runtime sentinel was not confirmed'
Require-Match $stdout '^runtime_create=VR_RUNTIME_OK$' 'runtime create did not succeed'
Require-Match $stdout '^frame_data=VR_RUNTIME_OK$' 'frame data query did not succeed'
Require-Match $stdout '^blocked_runtime_load_attempted=false$' 'blocked path attempted runtime load'
Require-Match $stdout '^blocked_loader_invoked=false$' 'blocked path invoked the Detours loader'
Require-Match $stdout '^blocked_sentinel_exists=false$' 'blocked path created a sentinel'

$tokenMatch = [regex]::Match($stdout, '(?m)^runtime_token=([^\r\n]+)$')
if (-not $tokenMatch.Success) { Fail 'driver did not print runtime token' }
$token = $tokenMatch.Groups[1].Value

if (-not (Test-Path -LiteralPath $SentinelFile)) {
    Fail 'runtime payload did not write sentinel file'
}
$sentinel = Get-Content -LiteralPath $SentinelFile -Raw
Write-Host "  [sentinel] $($sentinel.Trim())"
Require-Match $sentinel "token=$([regex]::Escape($token))" 'sentinel token mismatch'
Require-Match $sentinel 'module=vrclient_runtime_smoke_payload\.dll' 'sentinel module marker mismatch'
Require-Match $sentinel 'runtime_create=VR_RUNTIME_OK' 'sentinel missing runtime_create success'
Require-Match $sentinel 'runtime_state=stopped' 'sentinel did not record stopped runtime state'
Require-Match $sentinel 'headset_state=VR_RUNTIME_OK' 'sentinel missing headset state success'
Require-Match $sentinel 'frame_data=VR_RUNTIME_OK' 'sentinel missing frame data success'
Require-Match $sentinel 'eye_count=2' 'sentinel did not record two-eye frame data'
Require-Match $sentinel 'head_pose_orientation_valid=1' 'sentinel missing default head pose'
Require-Match $sentinel 'pose_timing_api=available' 'sentinel missing pose/timing availability'

if (Test-Path -LiteralPath $BlockedSentinelFile) {
    Fail 'blocked safety path wrote a sentinel even though the loader must not run'
}

$logMatch = [regex]::Match($stdout, '(?m)^diagnostics_log=([^\r\n]+)$')
if (-not $logMatch.Success) { Fail 'driver did not print diagnostics log path' }
$logPath = $logMatch.Groups[1].Value.Trim()
Require-Path $logPath 'diagnostics log'
$log = Get-Content -LiteralPath $logPath -Raw
Require-Match $log '"event":"bootstrap_smoke_result"' 'diagnostics log missing bootstrap result event'
Require-Match $log '"game_id":"vrclient-smoke-host"' 'diagnostics log missing game id'
Require-Match $log '"build_id":"smoke-2026-06-11"' 'diagnostics log missing build id'
Require-Match $log '"bootstrap_result":"loaded"' 'diagnostics log missing loaded result'
Require-Match $log '"runtime_load_result":"loaded"' 'diagnostics log missing loaded runtime result'
Require-Match $log '"pose_timing_api":"available"' 'diagnostics log missing pose/timing availability'
Require-Match $log '"safety_reason_code":"approved"' 'diagnostics log missing safety reason'
Require-Match $log '"reason_code":"anti_cheat_detected"' 'diagnostics log missing blocked refusal reason'

Write-Host 'PASS: Detours runtime smoke payload loaded through bootstrap and safety refusal stayed pre-launch'
exit 0

#Requires -Version 5.1
<#
.SYNOPSIS
    Optional Phase 3 / INJ-01 attach-to-running proof using a user-supplied
    EasyHook-compatible AttachLoader.exe.

.DESCRIPTION
    The C++ driver starts the controlled smoke host as an already-running
    process, discovers it through AttachRunning, evaluates the existing Phase 7
    safety verdict, and only then invokes the external attach loader with:

        AttachLoader.exe --pid <pid> --dll <payload.dll>

    The payload writes a tokenized sentinel from inside the target process. A
    blocked anti-cheat observation proves the loader is not invoked.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Driver,
    [Parameter(Mandatory = $true)][string]$AttachLoader,
    [Parameter(Mandatory = $true)][string]$SmokeHost,
    [Parameter(Mandatory = $true)][string]$PayloadDll,
    [Parameter(Mandatory = $true)][string]$SentinelFile,
    [Parameter(Mandatory = $true)][string]$BlockedSentinelFile,
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

Require-Path $Driver 'EasyHook attach smoke driver'
Require-Path $AttachLoader 'EasyHook-compatible attach loader'
Require-Path $SmokeHost 'smoke host'
Require-Path $PayloadDll 'proof payload'
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

Write-Host "=== EasyHook attach smoke proof ==="
Write-Host "driver       : $Driver"
Write-Host "attach loader: $AttachLoader"
Write-Host "host         : $SmokeHost"
Write-Host "payload      : $PayloadDll"
Write-Host "sentinel     : $SentinelFile"

$args = @(
    '--attach-loader', $AttachLoader,
    '--smoke-host', $SmokeHost,
    '--payload-dll', $PayloadDll,
    '--sentinel', $SentinelFile,
    '--blocked-sentinel', $BlockedSentinelFile,
    '--game-config', $GameConfig,
    '--safety-rules', $SafetyRules,
    '--log-dir', $LogDir,
    '--timeout-ms', ([string]($TimeoutSeconds * 1000))
)

$run = Invoke-Guarded -Exe $Driver -Arguments $args -Timeout $TimeoutSeconds
if ($run.StdOut) { $run.StdOut.TrimEnd().Split("`n") | ForEach-Object { Write-Host "  [driver] $_" } }
if ($run.StdErr) { $run.StdErr.TrimEnd().Split("`n") | ForEach-Object { Write-Host "  [driver:err] $_" } }
if ($run.TimedOut) { Fail "EasyHook attach smoke driver timed out after ${TimeoutSeconds}s" }
if ($run.ExitCode -ne 0) { Fail "EasyHook attach smoke driver exited $($run.ExitCode), expected 0" }

$stdout = ([string]$run.StdOut) -replace "`r", ''
Require-Match $stdout '^bootstrap_result=loaded$' 'bootstrap result did not report loaded'
Require-Match $stdout '^runtime_load_result=loaded$' 'runtime load result did not report loaded'
Require-Match $stdout '^pose_timing_api=available$' 'bootstrap did not report pose/timing API availability'
Require-Match $stdout '^game_id=vrclient-smoke-host$' 'bootstrap diagnostics missing smoke game id'
Require-Match $stdout '^build_id=smoke-2026-06-11$' 'bootstrap diagnostics missing smoke build id'
Require-Match $stdout '^safety_reason_code=approved$' 'bootstrap diagnostics missing approved safety reason'
Require-Match $stdout '^attach_sentinel_confirmed=true$' 'attach sentinel was not confirmed'
Require-Match $stdout '^sentinel_module=vrclient_proof_payload\.dll$' 'sentinel module marker mismatch'
Require-Match $stdout '^blocked_runtime_load_attempted=false$' 'blocked path attempted runtime load'
Require-Match $stdout '^blocked_loader_invoked=false$' 'blocked path invoked the EasyHook attach loader'
Require-Match $stdout '^blocked_sentinel_exists=false$' 'blocked path created a sentinel'

$tokenMatch = [regex]::Match($stdout, '(?m)^attach_token=([^\r\n]+)$')
if (-not $tokenMatch.Success) { Fail 'driver did not print attach token' }
$token = $tokenMatch.Groups[1].Value
$pidMatch = [regex]::Match($stdout, '(?m)^attach_pid=([0-9]+)$')
if (-not $pidMatch.Success) { Fail 'driver did not print attach pid' }
$pid = $pidMatch.Groups[1].Value

if (-not (Test-Path -LiteralPath $SentinelFile)) {
    Fail 'proof payload did not write sentinel file'
}
$sentinel = Get-Content -LiteralPath $SentinelFile -Raw
Write-Host "  [sentinel] $($sentinel.Trim())"
Require-Match $sentinel "token=$([regex]::Escape($token))" 'sentinel token mismatch'
Require-Match $sentinel "pid=$pid" 'sentinel pid mismatch'
Require-Match $sentinel 'module=vrclient_proof_payload\.dll' 'sentinel module marker mismatch'
Require-Match $sentinel 'pose_timing_api=available' 'sentinel missing pose/timing availability marker'

if (Test-Path -LiteralPath $BlockedSentinelFile) {
    Fail 'blocked safety path wrote a sentinel even though the loader must not run'
}

$logMatch = [regex]::Match($stdout, '(?m)^diagnostics_log=([^\r\n]+)$')
if (-not $logMatch.Success) { Fail 'driver did not print diagnostics log path' }
$logPath = $logMatch.Groups[1].Value.Trim()
Require-Path $logPath 'diagnostics log'
$log = Get-Content -LiteralPath $logPath -Raw
Require-Match $log '"event":"process_discovery_result"' 'diagnostics log missing attach discovery event'
Require-Match $log '"flow":"attach_running"' 'diagnostics log missing attach flow'
Require-Match $log '"event":"bootstrap_smoke_result"' 'diagnostics log missing bootstrap result event'
Require-Match $log '"game_id":"vrclient-smoke-host"' 'diagnostics log missing game id'
Require-Match $log '"build_id":"smoke-2026-06-11"' 'diagnostics log missing build id'
Require-Match $log '"bootstrap_result":"loaded"' 'diagnostics log missing loaded result'
Require-Match $log '"runtime_load_result":"loaded"' 'diagnostics log missing loaded runtime result'
Require-Match $log '"safety_reason_code":"approved"' 'diagnostics log missing safety reason'
Require-Match $log '"reason_code":"anti_cheat_detected"' 'diagnostics log missing blocked refusal reason'

Write-Host 'PASS: EasyHook-compatible attach loader loaded proof payload into a running controlled smoke host and safety refusal stayed pre-attach'
exit 0

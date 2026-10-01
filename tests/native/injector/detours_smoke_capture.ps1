#Requires -Version 5.1
<#
.SYNOPSIS
    Optional Phase 3 / INJ-06 controlled smoke proof using the user-supplied
    Microsoft Detours `withdll.exe` launch-time loader.

.DESCRIPTION
    This test deliberately does not implement an injector primitive in VRClient.
    It invokes a user-built Detours sample tool to launch only
    `vrclient_smoke_host --once` with a benign proof payload DLL. The payload
    writes a per-run token, process id, process image path, and local
    pose/timing-API availability marker to a sentinel file from inside the target
    process. The script refuses stale sentinels and verifies that the payload ran
    inside `vrclient_smoke_host.exe`.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$WithDll,
    [Parameter(Mandatory = $true)][string]$SmokeHost,
    [Parameter(Mandatory = $true)][string]$PayloadDll,
    [Parameter(Mandatory = $true)][string]$SentinelFile,
    [int]$TimeoutSeconds = 30
)

$ErrorActionPreference = 'Stop'

function Fail {
    param([string]$Message)
    Write-Host "FAIL: $Message"
    exit 1
}

function Invoke-Guarded {
    param(
        [Parameter(Mandatory = $true)][string]$Exe,
        [string[]]$Arguments = @(),
        [int]$Timeout = 30
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

if (-not (Test-Path -LiteralPath $WithDll)) { Fail "Detours withdll.exe not found: $WithDll" }
if (-not (Test-Path -LiteralPath $SmokeHost)) { Fail "smoke host not found: $SmokeHost" }
if (-not (Test-Path -LiteralPath $PayloadDll)) { Fail "proof payload not found: $PayloadDll" }

$sentinelDir = Split-Path -Parent $SentinelFile
if ($sentinelDir -and -not (Test-Path -LiteralPath $sentinelDir)) {
    New-Item -ItemType Directory -Force -Path $sentinelDir | Out-Null
}
if (Test-Path -LiteralPath $SentinelFile) {
    Remove-Item -LiteralPath $SentinelFile -Force
}

$token = [Guid]::NewGuid().ToString('N')
$oldEnv = $env:VRCLIENT_PROOF_SENTINEL
$env:VRCLIENT_PROOF_SENTINEL = "$SentinelFile|$token"

try {
    Write-Host "=== Detours controlled smoke proof ==="
    Write-Host "withdll : $WithDll"
    Write-Host "host    : $SmokeHost"
    Write-Host "payload : $PayloadDll"
    Write-Host "sentinel: $SentinelFile"

    $run = Invoke-Guarded -Exe $WithDll -Arguments @("/d:$PayloadDll", $SmokeHost, "--once") -Timeout $TimeoutSeconds
    if ($run.StdOut) { $run.StdOut.TrimEnd().Split("`n") | ForEach-Object { Write-Host "  [withdll] $_" } }
    if ($run.StdErr) { $run.StdErr.TrimEnd().Split("`n") | ForEach-Object { Write-Host "  [withdll:err] $_" } }
    if ($run.TimedOut) { Fail "withdll smoke launch timed out after ${TimeoutSeconds}s" }
    if ($run.ExitCode -ne 0) { Fail "withdll exited $($run.ExitCode), expected 0" }

    if (-not (Test-Path -LiteralPath $SentinelFile)) {
        Fail "proof payload did not write sentinel file"
    }
    $sentinel = Get-Content -LiteralPath $SentinelFile -Raw
    Write-Host "  [sentinel] $($sentinel.Trim())"

    if ($sentinel -notmatch "token=$token") {
        Fail "sentinel token mismatch; stale or wrong-process sentinel"
    }
    if ($sentinel -notmatch 'module=vrclient_proof_payload\.dll') {
        Fail "sentinel missing proof payload module marker"
    }
    if ($sentinel -notmatch 'exe=.*vrclient_smoke_host\.exe') {
        Fail "sentinel does not prove execution inside vrclient_smoke_host.exe"
    }
    if ($sentinel -notmatch 'pose_timing_api=available') {
        Fail "sentinel missing pose/timing API availability marker"
    }
    if ($sentinel -notmatch 'pid=\d+') {
        Fail "sentinel missing in-target process id"
    }

    Write-Host "PASS: Detours user-supplied launch-time loader executed proof payload in controlled smoke host"
}
finally {
    if ($null -eq $oldEnv) {
        Remove-Item Env:VRCLIENT_PROOF_SENTINEL -ErrorAction SilentlyContinue
    }
    else {
        $env:VRCLIENT_PROOF_SENTINEL = $oldEnv
    }
}

exit 0

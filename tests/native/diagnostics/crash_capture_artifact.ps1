#Requires -Version 5.1
<#
.SYNOPSIS
    CTest driver for the DIAG-02 closure (Phase 2): prove the diagnostics
    crash-capture writes its self-contained FALLBACK artifact when a REAL process
    crash drives the installed top-level SEH filter.

.DESCRIPTION
    SetUnhandledExceptionFilter is process-wide and last-chance, so the only
    faithful proof is to crash a CHILD process and inspect what it left behind. A
    crash inside the test process would abort the runner.

    This driver:
      1. Launches vr_crash_harness --crash with a TEMP artifact dir. The child
         installs CrashCapture, seeds identity, then deliberately null-derefs
         (EXCEPTION_ACCESS_VIOLATION). The installed SEH filter writes
         crash-*.json, then the process terminates ABNORMALLY.
      2. Asserts the child exited abnormally (the access-violation code, NOT 0) —
         proves a REAL crash actually happened.
      3. Locates the newest crash-*.json (the SEH path does NOT update
         lastArtifactPath(), so we scan the dir), parses it, and asserts
         artifact_type == fallback_crash_report, reason == unhandled_exception,
         a nonzero exception_code, and the seeded game/build/launch identity.
      4. Runs vr_crash_harness --graceful to prove the FINAL LOG FLUSH leg:
         writeFinalArtifact embeds the flushed recent log records and the logger
         flushes on stop. Asserts that artifact carries the recent record.

    B7/CRASH-01: this driver also proves a REAL OS minidump is produced. The
    same fault that drives the SEH filter now also writes a crash-*.dmp via
    MiniDumpWriteDump (additive; the fallback JSON is unchanged). Step 3b
    asserts the .dmp exists, is non-empty, and carries the MDMP magic header.

    HONESTY: this proves BOTH the FALLBACK self-contained JSON artifact
    (artifact_type: fallback_crash_report) AND the real OS minidump (.dmp).
    All scratch lands under the CMake-provided -WorkDir (a build-tree temp);
    cleaned up on success.

    Paths are injected by CMake generator expressions (mirrors
    hookdisc_obs_capture.ps1). A timeout guard kills + fails on a hang.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Harness,   # $<TARGET_FILE:vr_crash_harness>
    [Parameter(Mandatory = $true)][string]$WorkDir,   # scratch root (build tree)
    [int]$TimeoutSeconds = 60
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
        [int]$Timeout = 60
    )
    $outLog = [System.IO.Path]::GetTempFileName()
    $errLog = [System.IO.Path]::GetTempFileName()
    try {
        $proc = Start-Process -FilePath $Exe -ArgumentList $Arguments -NoNewWindow `
            -PassThru -RedirectStandardOutput $outLog -RedirectStandardError $errLog
        $handle = $proc.Handle  # cache so ExitCode stays readable after exit (PS 5.1)
        if (-not $proc.WaitForExit($Timeout * 1000)) {
            try { $proc.Kill() } catch { }
            $null = $proc.WaitForExit(5000)
            return @{ ExitCode = -1; TimedOut = $true
                StdOut = (Get-Content -LiteralPath $outLog -Raw -ErrorAction SilentlyContinue)
                StdErr = (Get-Content -LiteralPath $errLog -Raw -ErrorAction SilentlyContinue) }
        }
        $proc.WaitForExit()
        return @{ ExitCode = $proc.ExitCode; TimedOut = $false
            StdOut = (Get-Content -LiteralPath $outLog -Raw -ErrorAction SilentlyContinue)
            StdErr = (Get-Content -LiteralPath $errLog -Raw -ErrorAction SilentlyContinue) }
    }
    finally {
        Remove-Item -LiteralPath $outLog, $errLog -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "=== DIAG-02 forced-crash artifact (vr_crash_harness) ==="
Write-Host "harness : $Harness"
Write-Host "workdir : $WorkDir"

if (-not (Test-Path -LiteralPath $Harness)) { Fail "crash harness binary not found: $Harness" }

# Fresh, unique scratch root so a pass can never be inherited from a prior run.
$root = Join-Path $WorkDir ("crash-" + [System.Guid]::NewGuid().ToString('N'))
$crashDir = Join-Path $root 'crashes'
if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
New-Item -ItemType Directory -Force -Path $crashDir | Out-Null

$crashSession = 'diag02-crash'
$gracefulSession = 'diag02-graceful'

# -------------------------------------------------------------------------
# 1+2. Launch the crash harness; it MUST crash (abnormal, nonzero exit).
# -------------------------------------------------------------------------
Write-Host "`n--- launch crash harness (timeout ${TimeoutSeconds}s) ---"
$run = Invoke-Guarded -Exe $Harness -Arguments @($root, $crashSession, '--crash') -Timeout $TimeoutSeconds
if ($run.StdOut) { $run.StdOut.TrimEnd().Split("`n") | ForEach-Object { Write-Host "  [child] $_" } }
if ($run.TimedOut) { Fail "crash harness hung past ${TimeoutSeconds}s and was killed (it should crash fast)" }
if ($run.ExitCode -eq 0) {
    Fail "crash harness exited 0 - it did NOT actually crash (no real fault occurred)"
}
# Access violation surfaces as 0xC0000005 (-1073741819 as signed int32 / 3221225477 unsigned).
Write-Host "  child exited abnormally with code $($run.ExitCode) (real crash confirmed)"

# -------------------------------------------------------------------------
# 3. Locate + assert the SEH-written fallback artifact.
#    The SEH path does not update lastArtifactPath(); scan for newest crash-*.json.
# -------------------------------------------------------------------------
Write-Host "`n--- assert fallback crash artifact ---"
if (-not (Test-Path -LiteralPath $crashDir)) { Fail "crash dir not created by handler: $crashDir" }
$artifact = Get-ChildItem -LiteralPath $crashDir -Filter 'crash-*.json' -File -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
if (-not $artifact) { Fail "handler did not write a crash-*.json artifact in $crashDir" }
Write-Host "  artifact: $($artifact.FullName)"

$doc = Get-Content -LiteralPath $artifact.FullName -Raw | ConvertFrom-Json
if ($doc.artifact_type -ne 'fallback_crash_report') {
    Fail "artifact_type must be fallback_crash_report (got '$($doc.artifact_type)')"
}
if ($doc.reason -ne 'unhandled_exception') {
    Fail "reason must be unhandled_exception (got '$($doc.reason)')"
}
if ([int64]$doc.exception_code -eq 0) {
    Fail "exception_code must be nonzero for a real fault (got $($doc.exception_code))"
}
if ($doc.session_id -ne $crashSession) {
    Fail "session_id must be the seeded $crashSession (got '$($doc.session_id)')"
}
if ($doc.game_id -ne 'crash-probe-game') {
    Fail "game_id must survive into the artifact (got '$($doc.game_id)')"
}
if ($doc.build_id -ne 'crash-probe-build') {
    Fail "build_id must survive into the artifact (got '$($doc.build_id)')"
}
if ($doc.launch_path -notmatch 'vr_crash_harness') {
    Fail "launch_path must carry the live process identity (got '$($doc.launch_path)')"
}
# The SEH path intentionally omits recent records (async-safe) -> empty array.
if ($null -ne $doc.recent_log_records -and @($doc.recent_log_records).Count -ne 0) {
    Fail "SEH fallback artifact should carry an empty recent_log_records array (async-safe)"
}
Write-Host "  fallback artifact OK: reason=$($doc.reason) code=$($doc.exception_code) game=$($doc.game_id)"

# -------------------------------------------------------------------------
# 3b. B7/CRASH-01: assert a REAL OS minidump was written by the SEH filter
#     during the same fault, alongside (additive to) the fallback JSON.
#     The handler writes crash-<ts>-<session>.dmp into the SAME $crashDir.
#     Scan for the newest crash-*.dmp; assert it exists, is non-empty, and
#     begins with the MDMP magic header (0x4D 0x44 0x4D 0x50 = "MDMP").
# -------------------------------------------------------------------------
Write-Host "`n--- assert real minidump (.dmp) ---"
$dmp = Get-ChildItem -LiteralPath $crashDir -Filter 'crash-*.dmp' -File -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
if (-not $dmp) { Fail "SEH filter did not write a crash-*.dmp minidump in $crashDir" }
Write-Host "  minidump: $($dmp.FullName) ($($dmp.Length) bytes)"
if ($dmp.Length -le 0) { Fail "minidump is empty (0 bytes): $($dmp.FullName)" }
$magic = [System.IO.File]::ReadAllBytes($dmp.FullName)[0..3]
if (-not ($magic[0] -eq 0x4D -and $magic[1] -eq 0x44 -and $magic[2] -eq 0x4D -and $magic[3] -eq 0x50)) {
    $hex = ($magic | ForEach-Object { '0x{0:X2}' -f $_ }) -join ' '
    Fail "minidump does not start with MDMP magic (got $hex): $($dmp.FullName)"
}
Write-Host "  minidump OK: MDMP magic present, $($dmp.Length) bytes (real OS dump)"

# -------------------------------------------------------------------------
# 4. Final-log-flush leg: graceful mode embeds flushed recent records.
# -------------------------------------------------------------------------
Write-Host "`n--- final log flush (graceful writeFinalArtifact) ---"
$graceful = Invoke-Guarded -Exe $Harness -Arguments @($root, $gracefulSession, '--graceful') -Timeout $TimeoutSeconds
if ($graceful.StdOut) { $graceful.StdOut.TrimEnd().Split("`n") | ForEach-Object { Write-Host "  [child] $_" } }
if ($graceful.TimedOut) { Fail "graceful harness hung past ${TimeoutSeconds}s and was killed" }
if ($graceful.ExitCode -ne 0) {
    if ($graceful.StdErr) { Write-Host "  [child:err] $($graceful.StdErr.TrimEnd())" }
    Fail "graceful harness exited $($graceful.ExitCode) (expected 0 clean flush)"
}
$gracefulArtifact = Get-ChildItem -LiteralPath $crashDir -Filter "crash-*-$gracefulSession.json" -File -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
if (-not $gracefulArtifact) { Fail "graceful path did not write its crash-*.json artifact" }
$gdoc = Get-Content -LiteralPath $gracefulArtifact.FullName -Raw | ConvertFrom-Json
if ($gdoc.reason -ne 'graceful_shutdown_flush') {
    Fail "graceful artifact reason must be graceful_shutdown_flush (got '$($gdoc.reason)')"
}
# The graceful path DOES embed flushed recent records -> our seeded event present.
$recentEvents = @($gdoc.recent_log_records | ForEach-Object { $_.event })
if ($recentEvents -notcontains 'crash_probe_recent_event') {
    Fail "graceful artifact must embed the flushed recent log record (final-flush proof)"
}
# Confirm the logger flushed its own log file too.
$logFiles = Get-ChildItem -LiteralPath (Join-Path $root 'logs') -Filter '*.jsonl' -File -Recurse -ErrorAction SilentlyContinue
if (-not $logFiles -or @($logFiles).Count -lt 1) { Fail "logger did not flush a log file on shutdown" }
Write-Host "  final flush OK: recent record embedded + log file flushed"

# -------------------------------------------------------------------------
# Cleanup (scratch is under the build tree / temp anyway).
# -------------------------------------------------------------------------
Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "`n=== PASS: real crash drove the SEH handler; fallback artifact written; final flush proven ==="
exit 0

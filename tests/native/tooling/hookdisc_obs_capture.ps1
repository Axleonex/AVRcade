#Requires -Version 5.1
<#
.SYNOPSIS
    CTest driver for the cooperative in-target hook-surface capture proof
    (Bolt-on Phase B2). Launches vrclient_hookdisc_obs_host in a SEPARATE,
    approved process, asserts it captures real in-target evidence and emits a
    schema-valid hook-surface JSON with its own known function promoted
    candidate->validated, then re-validates that JSON with the existing
    validators. Also asserts the `vrclient_hookdisc inspect repo` refusal
    invariant is intact (regression-protect the safety gate next to the new
    in-target proof).

.DESCRIPTION
    Paths are injected by CMake generator expressions so the driver needs no
    knowledge of the build layout. The host runs the FULL pipeline (self safety
    gate -> RE-01 in-target inspection -> RE-02 HW-breakpoint observation across
    >=2 worker threads -> promote -> save). A timeout guard kills + fails on a
    hang so a wedged observer can never block CI.

    Honest scope: this proves COOPERATIVE in-target capture in a real second
    process. It does NOT prove uncooperative delivery and exercises no
    cross-process injection primitive. See obs_host_main.cpp header.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ObsHost,        # $<TARGET_FILE:vrclient_hookdisc_obs_host>
    [Parameter(Mandatory = $true)][string]$HookdiscCli,    # $<TARGET_FILE:vrclient_hookdisc>
    [Parameter(Mandatory = $true)][string]$OutFile,        # emitted hook-surface JSON path (temp, build dir)
    [Parameter(Mandatory = $true)][string]$PythonExe,      # Python3 interpreter
    [Parameter(Mandatory = $true)][string]$ValidateScript, # tests/native/tooling/validate_hook_surface.py
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
    # Capture stdout/stderr to temp files; enforce a hard timeout so a hung
    # observer (e.g. an unhandled single-step deadlock) cannot wedge CI.
    $outLog = [System.IO.Path]::GetTempFileName()
    $errLog = [System.IO.Path]::GetTempFileName()
    try {
        $proc = Start-Process -FilePath $Exe -ArgumentList $Arguments -NoNewWindow `
            -PassThru -RedirectStandardOutput $outLog -RedirectStandardError $errLog
        # Use the underlying Process object so the bool WaitForExit(ms) overload is
        # unambiguous AND the .ExitCode property is populated reliably under PS 5.1.
        $handle = $proc.Handle  # caching the handle keeps ExitCode readable after exit
        if (-not $proc.WaitForExit($Timeout * 1000)) {
            try { $proc.Kill() } catch { }
            $null = $proc.WaitForExit(5000)
            $stdout = (Get-Content -LiteralPath $outLog -Raw -ErrorAction SilentlyContinue)
            $stderr = (Get-Content -LiteralPath $errLog -Raw -ErrorAction SilentlyContinue)
            return @{ ExitCode = -1; TimedOut = $true; StdOut = $stdout; StdErr = $stderr }
        }
        # Parameterless WaitForExit() after the timed wait flushes the exit state so
        # ExitCode is reliably available (PS 5.1 quirk with the ms overload).
        $proc.WaitForExit()
        $exit = $proc.ExitCode
        $stdout = (Get-Content -LiteralPath $outLog -Raw -ErrorAction SilentlyContinue)
        $stderr = (Get-Content -LiteralPath $errLog -Raw -ErrorAction SilentlyContinue)
        return @{ ExitCode = $exit; TimedOut = $false; StdOut = $stdout; StdErr = $stderr }
    }
    finally {
        Remove-Item -LiteralPath $outLog, $errLog -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "=== Cooperative in-target capture (vrclient_hookdisc_obs_host) ==="
Write-Host "host       : $ObsHost"
Write-Host "cli        : $HookdiscCli"
Write-Host "out        : $OutFile"

if (-not (Test-Path -LiteralPath $ObsHost)) { Fail "observation host binary not found: $ObsHost" }
if (-not (Test-Path -LiteralPath $HookdiscCli)) { Fail "hookdisc CLI binary not found: $HookdiscCli" }

# Clean any stale artifact so a pass cannot be inherited from a previous run.
if (Test-Path -LiteralPath $OutFile) { Remove-Item -LiteralPath $OutFile -Force }
$outDir = Split-Path -Parent $OutFile
if ($outDir -and -not (Test-Path -LiteralPath $outDir)) {
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}

# -------------------------------------------------------------------------
# 1. Launch the in-target capture host under a timeout guard.
# -------------------------------------------------------------------------
Write-Host "`n--- launch host (timeout ${TimeoutSeconds}s) ---"
$run = Invoke-Guarded -Exe $ObsHost -Arguments @($OutFile) -Timeout $TimeoutSeconds
if ($run.StdOut) { $run.StdOut.TrimEnd().Split("`n") | ForEach-Object { Write-Host "  [host] $_" } }
if ($run.TimedOut) {
    if ($run.StdErr) { Write-Host "  [host:err] $($run.StdErr.TrimEnd())" }
    Fail "observation host hung past ${TimeoutSeconds}s timeout and was killed"
}
if ($run.ExitCode -ne 0) {
    if ($run.StdErr) { Write-Host "  [host:err] $($run.StdErr.TrimEnd())" }
    Fail "observation host exited $($run.ExitCode) (expected 0 - clean in-target capture)"
}
Write-Host "  host exited 0 (clean teardown)"

# -------------------------------------------------------------------------
# 2. Assert the emitted JSON exists and contains the promoted-validated hook.
# -------------------------------------------------------------------------
Write-Host "`n--- assert emitted hook-surface ---"
if (-not (Test-Path -LiteralPath $OutFile)) { Fail "host did not emit $OutFile" }
$doc = Get-Content -LiteralPath $OutFile -Raw | ConvertFrom-Json

if ($doc.version -ne 1) { Fail "emitted doc version must be 1 (got $($doc.version))" }
if ($doc.game_id -ne 'vrclient-smoke-host') { Fail "emitted doc game_id must be vrclient-smoke-host (got $($doc.game_id))" }
if ($doc.build_id -ne 'smoke-2026-06-11') { Fail "emitted doc build_id must be smoke-2026-06-11 (got $($doc.build_id))" }
if (-not $doc.hooks -or @($doc.hooks).Count -lt 1) { Fail "emitted doc must contain >=1 hook" }

$validated = @($doc.hooks | Where-Object { $_.status -eq 'validated' })
if ($validated.Count -lt 1) { Fail "emitted doc must contain a validated hook (candidate->validated proof)" }
$hook = $validated[0]
Write-Host "  validated hook: $($hook.name) (status=$($hook.status))"

if (-not $hook.validation) { Fail "validated hook missing validation evidence object" }
if ($hook.validation.method -ne 'hardware_breakpoint') {
    Fail "validated hook method must be hardware_breakpoint (got $($hook.validation.method))"
}
if ([int]$hook.validation.hit_count -lt 1) {
    Fail "validated hook hit_count must be >= 1 (got $($hook.validation.hit_count))"
}
if (-not $hook.validation.captured_at) { Fail "validated hook missing captured_at timestamp" }
$hasCadence = ($null -ne $hook.validation.cadence_hz) -and ([double]$hook.validation.cadence_hz -gt 0)
$hasPerFrame = ($null -ne $hook.validation.per_frame)
if (-not ($hasCadence -or $hasPerFrame)) {
    Fail "validated hook needs a positive cadence_hz or a per_frame flag"
}
Write-Host "  evidence: method=$($hook.validation.method) hit_count=$($hook.validation.hit_count) thread_context=$($hook.validation.thread_context)"

# The artifact must be honestly labeled as cooperative in-target capture so it
# can never be mistaken for proof of uncooperative R.E.P.O.-style delivery.
if ($doc.generated_by -notmatch 'in-target') {
    Fail "emitted doc generated_by must label cooperative in-target capture (got '$($doc.generated_by)')"
}

# -------------------------------------------------------------------------
# 3. Schema-validate the emitted JSON with the EXISTING validators
#    (Python validator + C++ doc-validate). Single source of truth = schema.
# -------------------------------------------------------------------------
Write-Host "`n--- schema validation (existing validators) ---"
$py = Invoke-Guarded -Exe $PythonExe -Arguments @($ValidateScript, $OutFile) -Timeout 60
if ($py.StdOut) { Write-Host "  [py] $($py.StdOut.TrimEnd())" }
if ($py.StdErr) { Write-Host "  [py:err] $($py.StdErr.TrimEnd())" }
if ($py.ExitCode -ne 0) { Fail "validate_hook_surface.py rejected the emitted JSON (exit $($py.ExitCode))" }

$cv = Invoke-Guarded -Exe $HookdiscCli -Arguments @('doc-validate', $OutFile) -Timeout 60
if ($cv.StdOut) { Write-Host "  [doc-validate] $($cv.StdOut.TrimEnd())" }
if ($cv.StdErr) { Write-Host "  [doc-validate:err] $($cv.StdErr.TrimEnd())" }
if ($cv.ExitCode -ne 0) { Fail "vrclient_hookdisc doc-validate rejected the emitted JSON (exit $($cv.ExitCode))" }

# -------------------------------------------------------------------------
# 4. Regression: `inspect repo` MUST still refuse (gate stays absolute).
#    Autonomous gate only auto-approves the controlled smoke target; a
#    commercial target refuses. Nonzero exit + a refusal reason is required.
# -------------------------------------------------------------------------
Write-Host "`n--- regression: inspect repo must refuse ---"
$repo = Invoke-Guarded -Exe $HookdiscCli -Arguments @('inspect', 'repo') -Timeout 60
$repoText = "$($repo.StdOut)`n$($repo.StdErr)"
if ($repo.ExitCode -eq 0) {
    Fail "vrclient_hookdisc inspect repo unexpectedly SUCCEEDED - the safety gate regressed"
}
if ($repoText -notmatch 'REFUSED' -and $repoText -notmatch 'refuse' -and
    $repoText -notmatch 'not_controlled_smoke_target' -and
    $repoText -notmatch 'private_multiplayer_not_confirmed') {
    Fail "inspect repo exited nonzero but emitted no recognizable refusal reason: $repoText"
}
Write-Host "  inspect repo refused (exit $($repo.ExitCode)) - gate intact"

Write-Host "`n=== PASS: cooperative in-target capture proven; gate regression intact ==="
exit 0

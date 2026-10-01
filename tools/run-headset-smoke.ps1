#Requires -Version 5.1
<#
.SYNOPSIS
    B3 Headset Validation Harness launcher. ONE command the USER runs on a machine
    with a Quest + active OpenXR runtime: imports the build environment, configures +
    builds vrclient_headset_smoke with -OpenXR ON (Release), runs it for a bounded
    duration, and writes the structured session-evidence JSON + a copy of stdout +
    the runtime session log into artifacts/headset/<timestamp>/. Prints a short
    PASS / areas-to-check summary + where the evidence landed.

.DESCRIPTION
    The headset smoke harness opens the FULL OpenXR session lifecycle (instance ->
    system -> session -> reference spaces -> stereo swapchains), runs a bounded
    frame loop rendering the built-in Vulkan test scene to BOTH eyes, then tears
    down cleanly. It captures lifecycle/state transitions (CORE-01/CORE-05),
    per-eye swapchain dims + (via the runtime log) format/count (CORE-02), host-
    measured frame timing (CORE-03/CORE-07), a host-reconstructed overlay snapshot
    (DIAG-03), and runtime/system identity (via the runtime log).

    HONESTY: on THIS (agent/CI) machine there is NO active OpenXR runtime and NO
    headset, so the harness detects that at xrCreateInstance/xrGetSystem and exits
    with code 10 ("no OpenXR runtime / no headset") after recording a graceful
    blocker in the JSON. That verifies ONLY that the harness builds, links the
    Release OpenXR loader, runs, and fails gracefully without crashing/hanging.
    It does NOT tick any CORE/DIAG box. Real headset evidence (CORE-01..05,
    CORE-07, DIAG-03) is produced ONLY when the USER runs this on a Quest with an
    active OpenXR runtime - and the FIRST such run is expected to surface bring-up
    bugs in the never-before-executed session/swapchain/present path (notably the
    swapchain image layout transition for present).

    This launcher does NOT append to .planning/evidence/LEDGER.md and does NOT
    write a vrclient-ci-run JSON - the headset smoke evidence is a SEPARATE
    artifact lane under artifacts/headset/, never the OpenXR-OFF green gate.

.PARAMETER BuildDir
    CMake binary dir (relative to repo root or absolute). Default: build/headset
    (separate from build/ci so it never clobbers the default-build cache).

.PARAMETER Seconds
    Wall-clock deadline for the bounded frame loop (no-hang guarantee). Default 10.

.PARAMETER Frames
    Optional frame cap (0 = bound only by -Seconds / signal / terminal state).

.PARAMETER ProfilePath
    Runtime-profile JSON. Default: config/defaults/runtime-profile.json.

.PARAMETER Compiler
    msvc | clang-cl | auto (default auto). Mirrors build-and-test.ps1.

.PARAMETER ProcessTimeoutSeconds
    Outer process-timeout guard (belt-and-suspenders over the harness's own
    bounded loop). Default: Seconds + 30.

.NOTES
    [PowerShell] powershell -NoProfile -ExecutionPolicy Bypass -File tools\run-headset-smoke.ps1
    [PowerShell] tools\run-headset-smoke.ps1 -Seconds 10
#>
[CmdletBinding()]
param(
    [string]$BuildDir = 'build/headset',
    [double]$Seconds = 10,
    [int]$Frames = 0,
    [string]$ProfilePath = 'config/defaults/runtime-profile.json',
    [ValidateSet('msvc', 'clang-cl', 'auto')][string]$Compiler = 'auto',
    [int]$ProcessTimeoutSeconds = 0
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDirFull = $BuildDir }
else { $BuildDirFull = Join-Path $RepoRoot $BuildDir }
if ($ProcessTimeoutSeconds -le 0) { $ProcessTimeoutSeconds = [int][math]::Ceiling($Seconds) + 30 }

function Write-Step { param([string]$Message) Write-Host "`n=== $Message ===" }

# Runs a native exe with stderr merged, never throwing on stderr writes (mirrors
# build-and-test.ps1). Returns @{ ExitCode; Output(string[]) }.
function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)][string]$Exe,
        [string[]]$Arguments = @(),
        [string]$WorkDir = $RepoRoot
    )
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    Push-Location -LiteralPath $WorkDir
    try {
        $output = & $Exe @Arguments 2>&1 | ForEach-Object { "$_" }
        $code = $LASTEXITCODE
    }
    finally {
        Pop-Location
        $ErrorActionPreference = $prev
    }
    return @{ ExitCode = $code; Output = @($output) }
}

# ---------------------------------------------------------------------------
# Step 1: build environment
# ---------------------------------------------------------------------------
Write-Step "Step 1/6: import build environment (tools/setup-build-env.ps1) [compiler: $Compiler]"
. (Join-Path $RepoRoot 'tools\setup-build-env.ps1') -Compiler $Compiler
$CMakeExe = $env:VRCLIENT_CMAKE_EXE
$NinjaExe = $env:VRCLIENT_NINJA_EXE
$compilerResolved = $env:VRCLIENT_COMPILER
if (-not $compilerResolved) { $compilerResolved = 'msvc' }
$ClangClExe = $env:VRCLIENT_CLANG_CL_EXE
if (-not $CMakeExe) { throw 'headset-smoke: VRCLIENT_CMAKE_EXE not set after setup-build-env.' }
if (-not $NinjaExe) { throw 'headset-smoke: VRCLIENT_NINJA_EXE not set after setup-build-env.' }
Write-Host "cmake   : $CMakeExe"
Write-Host "compiler: $compilerResolved"

$cmakeSupportsLinkerType = $false
$cv = Invoke-Native -Exe $CMakeExe -Arguments @('--version')
if (($cv.Output -join "`n") -match 'cmake version (\d+)\.(\d+)\.(\d+)') {
    $cmv = New-Object System.Version([int]$matches[1], [int]$matches[2], [int]$matches[3])
    $cmakeSupportsLinkerType = ($cmv -ge (New-Object System.Version(3, 29, 0)))
}

# OpenXR ON requires the project-local Release loader prefix (tools/build-openxr.ps1).
$openxrLocalPrefix = Join-Path $RepoRoot 'external\openxr'
$openxrConfig = Get-ChildItem -LiteralPath $openxrLocalPrefix -Recurse -Filter 'OpenXRConfig.cmake' -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $openxrConfig) {
    throw "headset-smoke: no OpenXRConfig.cmake under $openxrLocalPrefix. Run tools/build-openxr.ps1 first (the OpenXR-ON build needs the bootstrapped loader)."
}

# ---------------------------------------------------------------------------
# Step 2: CMake configure (-OpenXR ON, Release) -> dedicated build dir
# ---------------------------------------------------------------------------
Write-Step "Step 2/6: CMake configure (VRCLIENT_BUILD_OPENXR_RUNTIME=ON, Release) -> $BuildDirFull"
$configureFlags = @(
    '-S', $RepoRoot,
    '-B', $BuildDirFull,
    '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_MAKE_PROGRAM=$($NinjaExe.Replace('\','/'))",
    '-DVRCLIENT_BUILD_OPENXR_RUNTIME=ON',
    '-DVRCLIENT_BUILD_TESTS=OFF',
    "-DCMAKE_PREFIX_PATH=$($openxrLocalPrefix.Replace('\','/'))"
)
if ($compilerResolved -eq 'clang-cl' -and $ClangClExe) {
    $cc = $ClangClExe.Replace('\', '/')
    $configureFlags += @(
        "-DCMAKE_C_COMPILER=$cc",
        "-DCMAKE_CXX_COMPILER=$cc",
        '-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld-link',
        '-DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld-link',
        '-DCMAKE_MODULE_LINKER_FLAGS=-fuse-ld=lld-link'
    )
    if ($cmakeSupportsLinkerType) { $configureFlags += '-DCMAKE_LINKER_TYPE=LLD' }
}
$env:CMAKE_BUILD_PARALLEL_LEVEL = '2'
$r = Invoke-Native -Exe $CMakeExe -Arguments $configureFlags
$r.Output | ForEach-Object { Write-Host "  $_" }
if ($r.ExitCode -ne 0) { throw "headset-smoke: configure failed (exit $($r.ExitCode))." }

# ---------------------------------------------------------------------------
# Step 3: build the smoke target (RAM-tight: --parallel 2)
# ---------------------------------------------------------------------------
Write-Step 'Step 3/6: build vrclient_headset_smoke'
$r = Invoke-Native -Exe $CMakeExe -Arguments @('--build', $BuildDirFull, '--config', 'Release', '--target', 'vrclient_headset_smoke', '--parallel', '2')
$r.Output | ForEach-Object { Write-Host "  $_" }
if ($r.ExitCode -ne 0) { throw "headset-smoke: build failed (exit $($r.ExitCode))." }

$exe = Get-ChildItem -LiteralPath $BuildDirFull -Recurse -Filter 'vrclient_headset_smoke.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $exe) { throw "headset-smoke: built exe not found under $BuildDirFull." }
Write-Host "exe: $($exe.FullName)"

# ---------------------------------------------------------------------------
# Step 4: prepare the artifact dir
# ---------------------------------------------------------------------------
$stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
$artifactDir = Join-Path $RepoRoot "artifacts\headset\$stamp"
New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
$evidencePath = Join-Path $artifactDir 'session-evidence.json'
$stdoutPath = Join-Path $artifactDir 'stdout.log'
Write-Step "Step 4/6: artifact dir -> $artifactDir"

# ---------------------------------------------------------------------------
# Step 5: run the harness under an OUTER process-timeout guard. The harness has
# its own bounded loop (-Seconds); the outer guard is belt-and-suspenders so a
# hang at any layer cannot wedge the launcher. CWD = exe dir so the staged
# vulkan-1.dll loads and the harness writes its session log next to the exe.
# ---------------------------------------------------------------------------
Write-Step "Step 5/6: run vrclient_headset_smoke (seconds=$Seconds frames=$Frames, outer timeout=${ProcessTimeoutSeconds}s)"
$exeDir = Split-Path -Parent $exe.FullName
$profileResolved = $ProfilePath
if (-not [System.IO.Path]::IsPathRooted($profileResolved)) {
    $profileResolved = Join-Path $RepoRoot $ProfilePath
}
$harnessArgs = @(
    '--seconds', "$Seconds",
    '--frames', "$Frames",
    '--profile', $profileResolved,
    '--out', $evidencePath
)

# Run via System.Diagnostics.Process so ExitCode is reliably populated after the
# timeout-bounded wait (Start-Process -PassThru's ExitCode can come back $null).
# stdout/stderr are redirected to files; EnableRaisingEvents makes ExitCode cache.
$stderrPath = Join-Path $artifactDir 'stderr.log'
# PowerShell 5.1 runs on .NET Framework, where ProcessStartInfo exposes only the
# single .Arguments string (no .ArgumentList). Quote each arg that needs it.
function Format-Arg { param([string]$Value)
    if ($Value -match '[\s"]') { return '"' + ($Value -replace '"', '\"') + '"' }
    return $Value
}
$argString = ($harnessArgs | ForEach-Object { Format-Arg $_ }) -join ' '
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $exe.FullName
$psi.Arguments = $argString
$psi.WorkingDirectory = $exeDir
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$proc = New-Object System.Diagnostics.Process
$proc.StartInfo = $psi
$proc.EnableRaisingEvents = $true
# Async readers avoid a redirected-pipe deadlock if either stream fills its buffer.
$stdoutBuf = New-Object System.Text.StringBuilder
$stderrBuf = New-Object System.Text.StringBuilder
$outEvt = Register-ObjectEvent -InputObject $proc -EventName OutputDataReceived -MessageData $stdoutBuf -Action {
    if ($null -ne $EventArgs.Data) { [void]$Event.MessageData.AppendLine($EventArgs.Data) }
}
$errEvt = Register-ObjectEvent -InputObject $proc -EventName ErrorDataReceived -MessageData $stderrBuf -Action {
    if ($null -ne $EventArgs.Data) { [void]$Event.MessageData.AppendLine($EventArgs.Data) }
}
# Capture the run-start instant (with a small grace margin) so the log-copy step
# below can filter to ONLY the logs this run produced, never stale prior-run logs.
$runStart = (Get-Date).AddSeconds(-5)
[void]$proc.Start()
$proc.BeginOutputReadLine()
$proc.BeginErrorReadLine()
$timedOut = $false
if (-not $proc.WaitForExit($ProcessTimeoutSeconds * 1000)) {
    $timedOut = $true
    Write-Host "headset-smoke: OUTER TIMEOUT after ${ProcessTimeoutSeconds}s - killing harness (this should not happen; the loop is bounded)."
    try { $proc.Kill() } catch { }
    $proc.WaitForExit()
}
# Drain any buffered async output, then unregister the event subscribers.
Start-Sleep -Milliseconds 100
$harnessExit = $proc.ExitCode
Unregister-Event -SourceIdentifier $outEvt.Name -ErrorAction SilentlyContinue
Unregister-Event -SourceIdentifier $errEvt.Name -ErrorAction SilentlyContinue
[System.IO.File]::WriteAllText($stdoutPath, $stdoutBuf.ToString())
[System.IO.File]::WriteAllText($stderrPath, $stderrBuf.ToString())

# Merge stderr into stdout.log for a single capture artifact.
if (Test-Path -LiteralPath $stderrPath) {
    $stderrLines = Get-Content -LiteralPath $stderrPath -ErrorAction SilentlyContinue
    if ($stderrLines) {
        Add-Content -LiteralPath $stdoutPath -Value "`n--- stderr ---"
        Add-Content -LiteralPath $stdoutPath -Value $stderrLines
    }
    Remove-Item -LiteralPath $stderrPath -ErrorAction SilentlyContinue
}
if (Test-Path -LiteralPath $stdoutPath) {
    Get-Content -LiteralPath $stdoutPath | ForEach-Object { Write-Host "  $_" }
}

# Copy the runtime/session log(s) the harness produced into the artifact dir so
# the swapchain-format/identity log lines travel with the evidence. The AsyncLogger
# writes to <cwd>/logs/diagnostics/vrclient-smoke-*.jsonl (the smoke harness's own
# state-transition log); the OpenXrRuntime DiagnosticsSystem may also emit
# vrclient-runtime-*.jsonl. Search the exe dir AND logs/diagnostics under both the
# exe dir and the repo root; match BOTH .jsonl and .log; copy the freshest matches.
$logCandidates = @()
$searchDirs = @($exeDir, (Join-Path $exeDir 'logs\diagnostics'), (Join-Path $RepoRoot 'logs\diagnostics'))
foreach ($dir in ($searchDirs | Select-Object -Unique)) {
    if (-not (Test-Path -LiteralPath $dir)) { continue }
    $logCandidates += Get-ChildItem -LiteralPath $dir -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^vrclient-(smoke|runtime)-.*\.(jsonl|log)$' }
}
$logCandidates = @($logCandidates | Sort-Object FullName -Unique)

# Finding-6 fix (evidence hygiene): copy ONLY logs from THIS run window. Earlier
# this globbed the most-recent 6 by LastWriteTime with no time filter, so a single
# invocation could pull in prior runs' logs (even days-stale ones), making it
# ambiguous which log backs this evidence JSON. Filter to files written at/after
# $runStart (captured just before launch, with grace). If a run produced no
# matching files (e.g. the harness blocked before opening any log), fall back to
# the single freshest smoke + runtime log so the artifact is never left empty.
$windowLogs = @($logCandidates | Where-Object { $_.LastWriteTime -ge $runStart })
if ($windowLogs.Count -eq 0 -and $logCandidates.Count -gt 0) {
    Write-Host 'session logs: no files in the run window; falling back to freshest smoke+runtime log.'
    $freshSmoke = $logCandidates | Where-Object { $_.Name -like 'vrclient-smoke-*' } |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $freshRuntime = $logCandidates | Where-Object { $_.Name -like 'vrclient-runtime-*' } |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $windowLogs = @($freshSmoke, $freshRuntime | Where-Object { $_ })
}
foreach ($log in $windowLogs) {
    Copy-Item -LiteralPath $log.FullName -Destination $artifactDir -Force -ErrorAction SilentlyContinue
}
$copiedLogs = @(Get-ChildItem -LiteralPath $artifactDir -Filter 'vrclient-*' -ErrorAction SilentlyContinue)
Write-Host "session logs copied: $($copiedLogs.Count) (run-window filtered, since $($runStart.ToString('s')))"

# ---------------------------------------------------------------------------
# Step 6: summary + verdict
# ---------------------------------------------------------------------------
Write-Step 'Step 6/6: RESULT'
if ($timedOut) {
    $verdict = 'TIMEOUT'
    $verdictDetail = "harness did not exit within ${ProcessTimeoutSeconds}s and was killed (UNEXPECTED - investigate; the loop is supposed to be bounded)."
}
elseif ($harnessExit -eq 0) {
    $verdict = 'RAN'
    $verdictDetail = 'session ran + rendered the test scene to both eyes. NEEDS USER EYE CONFIRMATION on the Quest (check both eyes show the test scene, stable poses, no judder).'
}
elseif ($harnessExit -eq 10) {
    $verdict = 'NO-RUNTIME'
    $verdictDetail = 'no OpenXR runtime / no headset - graceful blocker recorded. This is the EXPECTED agent/CI result. To produce real headset evidence, run this on a machine with a Quest + active OpenXR runtime.'
}
elseif ($harnessExit -eq 13) {
    $verdict = 'STARTED-NO-FRAMES'
    $verdictDetail = 'session STARTED (a runtime + system are present) but never began / rendered 0 frames within the window - NOT a render to both eyes. Common causes: headset asleep or not donned, no XR_SESSION_STATE_READY event, or the compositor never drove the session focused. Don the headset and re-run; see evidence + log.'
}
elseif ($harnessExit -eq 11) {
    $verdict = 'GRAPHICS-BLOCKER'
    $verdictDetail = 'Vulkan device / renderer init failed - see evidence + log (driver/SDK issue).'
}
elseif ($harnessExit -eq 12) {
    $verdict = 'PROFILE-BLOCKER'
    $verdictDetail = "runtime-profile JSON missing/invalid ($profileResolved) - see evidence + log."
}
else {
    $verdict = 'FAILURE'
    $verdictDetail = "harness exited $harnessExit - see evidence + log."
}

Write-Host "verdict     : $verdict"
Write-Host "  detail    : $verdictDetail"
Write-Host "exit code   : $harnessExit"
Write-Host "evidence    : $evidencePath"
Write-Host "stdout      : $stdoutPath"
Write-Host "artifacts   : $artifactDir"
Write-Host ''
Write-Host 'HONESTY: building + a graceful no-runtime run is NOT headset verification. No CORE/DIAG box is ticked by this run.'
Write-Host 'Real headset evidence (CORE-01..05, CORE-07, DIAG-03) is produced ONLY on a Quest with an active OpenXR runtime, and the first run is expected to need iteration.'

# Exit nonzero on harness nonzero (so the user/CI sees the verdict) AFTER writing
# all artifacts. A graceful no-runtime (10) is the agent-side success criterion
# but is still a nonzero process exit by design.
if ($timedOut) { exit 124 }
exit $harnessExit

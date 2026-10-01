#Requires -Version 5.1
<#
.SYNOPSIS
    ONE command: environment -> CMake configure (Ninja) -> build all targets -> ctest ->
    every tests/native/**/*.py validator -> machine-readable JSON evidence + ledger entry.

.DESCRIPTION
    Bolt-on Phase B1, requirements BUILD-02 (one-command build+test+validate), BUILD-03
    (evidence ledger), BUILD-04 (regression gates: hot-path audit, schema validators, and
    ABI/static contract checks all run via ctest AND directly as Python scripts).

    Behavior:
      1. Imports tools/setup-build-env.ps1 (locates CMake/Ninja/MSVC via vswhere + pins).
      2. Resolves -OpenXR auto|on|off (default auto: ON only if find_package(OpenXR) can
         succeed, probed with a throwaway CMake project; the choice is recorded).
      3. Configures with -G Ninja into -BuildDir (default build/ci),
         -DVRCLIENT_BUILD_TESTS=ON, -DVRCLIENT_BUILD_OPENXR_RUNTIME=<resolved>.
      4. Builds all targets, runs ctest --output-on-failure.
      5. Runs EVERY tests/native/**/*.py from the repo root (plus the extra pinned
         invocation `validate_game_fingerprint.py config/games/repo.json` that mirrors the
         CTest registration `repo_fingerprint_schema`).
      6. Writes .planning/evidence/runs/<timestamp>-<git-short-hash>.json and appends one
         summary row to .planning/evidence/LEDGER.md.
      7. Exits nonzero on ANY failure (evidence files are still written).

.PARAMETER BuildDir
    CMake binary dir, relative to the repo root or absolute. Default: build/ci

.PARAMETER OpenXR
    auto (default) | on | off. auto probes find_package(OpenXR) and picks ON/OFF.
    NOTE: the bootstrapped OpenXR loader (tools/build-openxr.ps1) is Release-only, so an
    OpenXR-ON build must be Release. To remove that foot-gun, when OpenXR resolves ON this
    script auto-promotes a non-explicit -BuildType Debug to Release (and hard-errors if you
    explicitly pass -BuildType Debug with OpenXR ON). So `build-and-test.ps1 -OpenXR on` is
    green by itself — no second flag needed. The default (-OpenXR auto -> OFF) is untouched.

.PARAMETER BuildType
    CMAKE_BUILD_TYPE. Default: Debug (matches the proven phase4..6 validation builds).
    EXCEPTION: an OpenXR-ON build is forced/auto-promoted to Release (the project-local loader
    is Release-only — linking it into Debug trips lld-link _ITERATOR_DEBUG_LEVEL /failifmismatch).
    See -OpenXR. Has no effect on the default OpenXR-OFF build.

.PARAMETER Compiler
    msvc | clang-cl | auto. Selects the C/C++ toolchain.
      auto (DEFAULT) — open-first: resolve clang-cl when a portable LLVM install is present
                (setup-build-env exposes VRCLIENT_CLANG_CL_EXE), else fall back to msvc. A bare
                invocation prefers the open compiler when available and stays non-breaking on
                machines without LLVM.
      msvc — force the legacy MSVC path: no compiler flags passed, CMake auto-detects cl.exe.
                Byte-identical to before; use it to pin the established toolchain.
      clang-cl — force clang-cl (the "open" compiler); threads
                -DCMAKE_C_COMPILER / -DCMAKE_CXX_COMPILER=clang-cl (absolute, forward-slashed) +
                -DCMAKE_LINKER_TYPE=LLD into the configure. Still uses the Windows SDK/CRT via
                vcvars (clang-cl runs in MSVC-compat mode).
    The chosen compiler and its version are recorded in the run JSON and the ledger Notes.
    NOTE: clang-cl and msvc must use DIFFERENT -BuildDir values (CMake forbids changing the
    compiler of an existing cache). If you leave -BuildDir at its default, it auto-selects
    'build/ci' for msvc and 'build/ci-clang' for clang-cl.

.PARAMETER Note
    Free-text note recorded in the JSON and the ledger row (e.g. what evidence this run
    retires).

.PARAMETER DetoursSmoke
    Enables the optional user-supplied Detours launch-time smoke proof.

.PARAMETER EasyHookAttachSmoke
    Enables the optional user-supplied EasyHook-compatible attach-to-running smoke proof.

.NOTES
    [PowerShell] powershell -NoProfile -ExecutionPolicy Bypass -File scripts\ci\build-and-test.ps1
    [PowerShell] scripts\ci\build-and-test.ps1 -BuildDir build/ci -OpenXR auto -Note "..."
    [PowerShell] scripts\ci\build-and-test.ps1 -Compiler clang-cl -BuildDir build/ci-clang
    [PowerShell] scripts\ci\build-and-test.ps1 -EasyHookAttachSmoke -EasyHookAttachLoader C:\Tools\AttachLoader.exe
#>
[CmdletBinding()]
param(
    [string]$BuildDir = '',
    [ValidateSet('auto', 'on', 'off')][string]$OpenXR = 'auto',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')][string]$BuildType = 'Debug',
    [ValidateSet('msvc', 'clang-cl', 'auto')][string]$Compiler = 'auto',
    [string]$Note = '',
    [switch]$DetoursSmoke,
    [string]$DetoursWithDll = '',
    [switch]$EasyHookAttachSmoke,
    [string]$EasyHookAttachLoader = ''
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$failures = New-Object System.Collections.Generic.List[string]

# Whether the caller explicitly passed -BuildDir. If not, we pick a per-compiler default
# AFTER the env import resolves the compiler (clang-cl and msvc must NOT share a binary dir /
# CMakeCache — CMake forbids changing the compiler of an existing cache). $BuildDir/$BuildDirFull
# are finalized in Step 1.5 below.
$BuildDirExplicit = -not [string]::IsNullOrWhiteSpace($BuildDir)
$BuildDirFull = $null

# Whether the caller explicitly passed -BuildType. The default is Debug, but the project-local
# OpenXR loader (tools/build-openxr.ps1) is built Release-only, so a Debug app linking it trips
# lld-link /failifmismatch on _ITERATOR_DEBUG_LEVEL (loader=0, app=2). When OpenXR resolves ON
# we therefore (Step 3.5): auto-promote a NON-explicit Debug -> Release so the documented
# `build-and-test.ps1 -OpenXR on` is green out of the box, and hard-error on an EXPLICIT
# `-BuildType Debug` + OpenXR ON (honest about the Release-only loader rather than emitting a RED
# link failure 90 seconds later). The default auto/off path never enters this branch.
$BuildTypeExplicit = $PSBoundParameters.ContainsKey('BuildType')

# Set true by Step 3.5 when OpenXR ON collides with an explicit -BuildType Debug (Release-only
# loader). When true, configure/build/ctest/validators are skipped and a RED evidence row is
# still written.
$abortBeforeBuild = $false

function Write-Step { param([string]$Message) Write-Host "`n=== $Message ===" }

# Runs a native executable with stderr merged, never throwing on stderr writes;
# returns @{ ExitCode; Output(string[]) }.
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

function Save-Log {
    param([string]$Name, [string[]]$Lines)
    $logDir = Join-Path $BuildDirFull 'ci-logs'
    New-Item -ItemType Directory -Force -Path $logDir | Out-Null
    $path = Join-Path $logDir "$Name.log"
    [System.IO.File]::WriteAllLines($path, [string[]]$Lines)
    return $path
}

function Get-Tail {
    param([string[]]$Lines, [int]$Count = 40)
    if (-not $Lines) { return @() }
    if ($Lines.Count -le $Count) { return @($Lines) }
    return @($Lines[($Lines.Count - $Count)..($Lines.Count - 1)])
}

# ---------------------------------------------------------------------------
# Step 1: build environment
# ---------------------------------------------------------------------------
Write-Step "Step 1/7: import build environment (tools/setup-build-env.ps1) [compiler: $Compiler]"
$envOk = $true
$envError = $null
try {
    . (Join-Path $RepoRoot 'tools\setup-build-env.ps1') -Compiler $Compiler
}
catch {
    $envOk = $false
    $envError = $_.Exception.Message
    $failures.Add("environment: $envError")
    Write-Host "ENVIRONMENT FAILURE: $envError"
}
$CMakeExe = $env:VRCLIENT_CMAKE_EXE
$CTestExe = $env:VRCLIENT_CTEST_EXE
$NinjaExe = $env:VRCLIENT_NINJA_EXE
$PythonExe = $env:VRCLIENT_PYTHON_EXE
if (-not $PythonExe) {
    $pathPython = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($pathPython) { $PythonExe = $pathPython.Source }
}

# Resolve which compiler the env script actually selected (auto may fall back to msvc when
# no LLVM is present). VRCLIENT_COMPILER is the authoritative resolved value; VRCLIENT_CLANG_CL_EXE
# is the absolute clang-cl path when clang-cl resolved.
$ClangClExe = $env:VRCLIENT_CLANG_CL_EXE
$LldLinkExe = $env:VRCLIENT_LLD_LINK_EXE
$compilerResolved = $env:VRCLIENT_COMPILER
if (-not $compilerResolved) { $compilerResolved = 'msvc' }
if ($Compiler -eq 'msvc') {
    $compilerDecision = 'forced msvc by -Compiler msvc (no compiler flags; CMake auto-detects cl.exe)'
}
elseif ($Compiler -eq 'clang-cl') {
    $compilerDecision = "forced clang-cl by -Compiler clang-cl (resolved: $compilerResolved)"
}
else {
    if ($compilerResolved -eq 'clang-cl') {
        $compilerDecision = 'auto (opt-in): portable LLVM/clang-cl found -> clang-cl (the open compiler)'
    }
    else {
        $compilerDecision = 'auto (opt-in): no portable LLVM/clang-cl found -> msvc fallback'
    }
}
Write-Host "Compiler: $compilerResolved ($compilerDecision)"

# ---------------------------------------------------------------------------
# Step 1.5: finalize the binary dir. Default per-compiler when -BuildDir was not
# given (clang-cl and msvc must not share a CMakeCache).
# ---------------------------------------------------------------------------
if (-not $BuildDirExplicit) {
    if ($compilerResolved -eq 'clang-cl') { $BuildDir = 'build/ci-clang' } else { $BuildDir = 'build/ci' }
    Write-Host "BuildDir (auto for ${compilerResolved}): $BuildDir"
}
if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDirFull = $BuildDir }
else { $BuildDirFull = Join-Path $RepoRoot $BuildDir }

# Probe the running CMake's version so the clang-cl path can pick a version-robust LLD
# selection. CMAKE_LINKER_TYPE was only introduced in CMake 3.29; on the project's
# documented floor (3.24) it is silently ignored. We gate the native flag on >= 3.29 and
# always fall back to the portable -fuse-ld=lld-link (see Step 4).
$cmakeVersionString = 'unknown'
$cmakeSupportsLinkerType = $false
if ($envOk -and $CMakeExe) {
    $cv = Invoke-Native -Exe $CMakeExe -Arguments @('--version')
    # Match the version directly off the joined output. NOTE: do NOT pre-filter with
    # `Where-Object { $_ -match ... }` then re-match $cvLine — the Where-Object scriptblock
    # rebinds the automatic $matches on every pipeline element, so the version captures get
    # clobbered (the probe then sees 'unknown' and wrongly skips the >=3.29 native flag).
    $cvText = ($cv.Output -join "`n")
    if ($cvText -match 'cmake version (\d+)\.(\d+)\.(\d+)') {
        $cmakeVersionString = "$($matches[1]).$($matches[2]).$($matches[3])"
        $cmv = New-Object System.Version([int]$matches[1], [int]$matches[2], [int]$matches[3])
        $cmakeSupportsLinkerType = ($cmv -ge (New-Object System.Version(3, 29, 0)))
    }
}

# ---------------------------------------------------------------------------
# Step 2: git identity of this run
# ---------------------------------------------------------------------------
Write-Step 'Step 2/7: git identity'
$gitExe = $null
$gitCmd = Get-Command git.exe -ErrorAction SilentlyContinue
if ($gitCmd) { $gitExe = $gitCmd.Source }

function Invoke-Git {
    param([string[]]$GitArguments)
    if (-not $gitExe) { return @{ ExitCode = 127; Output = @('git not found on PATH') } }
    $r = Invoke-Native -Exe $gitExe -Arguments $GitArguments
    if ($r.ExitCode -ne 0 -and (($r.Output -join ' ') -match 'dubious ownership')) {
        $r = Invoke-Native -Exe $gitExe -Arguments (@('-c', "safe.directory=$($RepoRoot.Replace('\','/'))") + $GitArguments)
    }
    return $r
}

$gitCommit = 'unknown'
$gitShort = 'nogit'
$gitBranch = 'unknown'
$gitDirty = $null
$r = Invoke-Git -GitArguments @('rev-parse', 'HEAD')
if ($r.ExitCode -eq 0) { $gitCommit = ($r.Output | Select-Object -First 1).Trim() }
$r = Invoke-Git -GitArguments @('rev-parse', '--short=8', 'HEAD')
if ($r.ExitCode -eq 0) { $gitShort = ($r.Output | Select-Object -First 1).Trim() }
$r = Invoke-Git -GitArguments @('rev-parse', '--abbrev-ref', 'HEAD')
if ($r.ExitCode -eq 0) { $gitBranch = ($r.Output | Select-Object -First 1).Trim() }
$r = Invoke-Git -GitArguments @('status', '--porcelain')
if ($r.ExitCode -eq 0) { $gitDirty = [bool](@($r.Output | Where-Object { $_ }).Count -gt 0) }
Write-Host "commit=$gitCommit short=$gitShort branch=$gitBranch dirty=$gitDirty"

$startUtc = (Get-Date).ToUniversalTime()
$stamp = $startUtc.ToString('yyyyMMdd-HHmmss')

# ---------------------------------------------------------------------------
# Step 3: resolve OpenXR mode
# ---------------------------------------------------------------------------
Write-Step "Step 3/7: resolve OpenXR mode (requested: $OpenXR)"
$openxrResolved = 'OFF'
$openxrDecision = ''
# Project-local OpenXR loader prefix produced by tools/build-openxr.ps1 (gitignored external/).
# This is a NON-STANDARD location CMake does NOT search unless told. It is wired into
# CMAKE_PREFIX_PATH ONLY for an explicit '-OpenXR on' (Step 4); the 'auto' probe below
# deliberately does NOT add it, so a bare default build keeps find_package(OpenXR) NOTFOUND
# and stays OpenXR OFF even when this prefix exists.
$openxrLocalPrefix = Join-Path $RepoRoot 'external\openxr'
$openxrLocalPrefixExists = [bool](Get-ChildItem -LiteralPath $openxrLocalPrefix -Recurse -Filter 'OpenXRConfig.cmake' -ErrorAction SilentlyContinue | Select-Object -First 1)
if ($OpenXR -eq 'on') {
    $openxrResolved = 'ON'
    if ($openxrLocalPrefixExists) {
        $openxrDecision = "forced ON by -OpenXR on (CMAKE_PREFIX_PATH += $openxrLocalPrefix)"
    }
    else {
        $openxrDecision = "forced ON by -OpenXR on (WARNING: no project-local loader at $openxrLocalPrefix; run tools/build-openxr.ps1 first or find_package(OpenXR REQUIRED) will fail)"
    }
}
elseif ($OpenXR -eq 'off') {
    $openxrResolved = 'OFF'
    $openxrDecision = 'forced OFF by -OpenXR off'
}
elseif (-not $envOk) {
    $openxrResolved = 'OFF'
    $openxrDecision = 'auto: environment setup failed, probe skipped, defaulting OFF'
}
else {
    $probeRoot = Join-Path $BuildDirFull '_openxr-probe'
    $probeSrc = Join-Path $probeRoot 'src'
    $probeBin = Join-Path $probeRoot 'bin'
    New-Item -ItemType Directory -Force -Path $probeSrc, $probeBin | Out-Null
    $probeCMake = @(
        'cmake_minimum_required(VERSION 3.24)',
        'project(vrclient_openxr_probe LANGUAGES NONE)',
        'find_package(OpenXR QUIET)',
        'if(OpenXR_FOUND)',
        '  message(STATUS "VRCLIENT_OPENXR_PROBE_RESULT=FOUND")',
        'else()',
        '  message(STATUS "VRCLIENT_OPENXR_PROBE_RESULT=NOTFOUND")',
        'endif()'
    )
    Set-Content -LiteralPath (Join-Path $probeSrc 'CMakeLists.txt') -Value $probeCMake -Encoding ASCII
    $probe = Invoke-Native -Exe $CMakeExe -Arguments @(
        '-S', $probeSrc, '-B', $probeBin, '-G', 'Ninja',
        "-DCMAKE_MAKE_PROGRAM=$($NinjaExe.Replace('\','/'))"
    )
    $probeText = $probe.Output -join "`n"
    if ($probe.ExitCode -eq 0 -and $probeText -match 'VRCLIENT_OPENXR_PROBE_RESULT=FOUND') {
        $openxrResolved = 'ON'
        $openxrDecision = 'auto: find_package(OpenXR) probe FOUND the SDK -> ON'
    }
    else {
        $openxrResolved = 'OFF'
        $openxrDecision = "auto: find_package(OpenXR) probe did not find the SDK (probe exit $($probe.ExitCode)) -> OFF"
    }
}
Write-Host "OpenXR: $openxrResolved ($openxrDecision)"

# ---------------------------------------------------------------------------
# Step 3.5: reconcile BuildType with an OpenXR-ON build.
#
# The bootstrapped loader (tools/build-openxr.ps1) is Release-only. Linking it into a Debug app
# trips lld-link /failifmismatch on _ITERATOR_DEBUG_LEVEL (loader=0 / app=2) -> the OpenXR-ON
# build FAILS to link. The fix removes the silent two-flag foot-gun (`-OpenXR on` alone defaults
# to Debug and goes RED) without touching the default build:
#   - OpenXR OFF (the default auto/off path): no-op, BuildType is whatever the caller chose.
#   - OpenXR ON + Debug NOT explicitly requested: auto-promote to Release so the documented
#     `build-and-test.ps1 -OpenXR on` links green by itself. Recorded in $buildTypeDecision and
#     the ledger note.
#   - OpenXR ON + Debug EXPLICITLY requested: hard error NOW (fail fast, honest) — a Debug app
#     cannot link the Release-only loader. The caller must pass -BuildType Release (or rebuild
#     the loader Debug). Exits RED before configure rather than after a confusing link failure.
$buildTypeDecision = "as requested ($BuildType)"
if ($openxrResolved -eq 'ON' -and $BuildType -eq 'Debug') {
    if ($BuildTypeExplicit) {
        $msg = @(
            'OpenXR ON requires a non-Debug build: the project-local loader (tools/build-openxr.ps1)',
            'is built Release-only, so a Debug app fails to link it (lld-link /failifmismatch on',
            '_ITERATOR_DEBUG_LEVEL: loader=0, app=2). You explicitly passed -BuildType Debug.',
            'Re-run with -BuildType Release (or rebuild the loader Debug). Aborting before configure.'
        ) -join ' '
        Write-Step 'Step 3.5: BuildType/OpenXR reconciliation'
        Write-Host "ERROR: $msg"
        # Write a RED evidence row so this abort is captured like any other failure, then exit.
        $failures.Add("buildtype: $msg")
        $configureFlags = @()
        $configureExit = $null
        $configureOk = $false
        $buildExit = $null; $buildOk = $false
        $ctestExit = $null; $ctestOk = $false; $ctestTotal = 0; $ctestPassed = 0; $ctestFailedNames = @()
        $validatorResults = @(); $validatorsPassed = 0; $validatorsTotal = 0; $validatorsOk = $false
        $jsonschemaInfo = 'skipped (BuildType/OpenXR abort)'
        $buildTypeDecision = 'ABORT: OpenXR ON + explicit -BuildType Debug (loader is Release-only)'
        $abortBeforeBuild = $true
    }
    else {
        $BuildType = 'Release'
        $buildTypeDecision = 'auto-promoted Debug -> Release (OpenXR ON; loader is Release-only)'
        Write-Step 'Step 3.5: BuildType/OpenXR reconciliation'
        Write-Host "BuildType: $BuildType ($buildTypeDecision)"
    }
}

# ---------------------------------------------------------------------------
# Step 4: configure
# ---------------------------------------------------------------------------
Write-Step "Step 4/7: CMake configure -> $BuildDirFull"
if (-not $abortBeforeBuild) {
    $configureFlags = @()
    $configureOk = $false
    $configureExit = $null
}
if ($envOk -and -not $abortBeforeBuild) {
    $configureFlags = @(
        '-S', $RepoRoot,
        '-B', $BuildDirFull,
        '-G', 'Ninja',
        "-DCMAKE_BUILD_TYPE=$BuildType",
        "-DCMAKE_MAKE_PROGRAM=$($NinjaExe.Replace('\','/'))",
        "-DVRCLIENT_BUILD_OPENXR_RUNTIME=$openxrResolved",
        '-DVRCLIENT_BUILD_TESTS=ON'
    )
    if ($DetoursSmoke) {
        $configureFlags += '-DVRCLIENT_ENABLE_DETOURS_SMOKE=ON'
        if (-not [string]::IsNullOrWhiteSpace($DetoursWithDll)) {
            $configureFlags += "-DVRCLIENT_DETOURS_WITHDLL=$($DetoursWithDll.Replace('\','/'))"
        }
    }
    if ($EasyHookAttachSmoke) {
        $configureFlags += '-DVRCLIENT_ENABLE_EASYHOOK_ATTACH_SMOKE=ON'
        if (-not [string]::IsNullOrWhiteSpace($EasyHookAttachLoader)) {
            $configureFlags += "-DVRCLIENT_EASYHOOK_ATTACH_LOADER=$($EasyHookAttachLoader.Replace('\','/'))"
        }
    }
    # OpenXR ON: point find_package(OpenXR REQUIRED) at the project-local loader prefix built by
    # tools/build-openxr.ps1. Added ONLY when OpenXR resolved ON AND that prefix exists — the
    # default 'auto'/'off' paths never see it, so a bare build stays OpenXR OFF (HARD CONSTRAINT:
    # installing the SDK to external/openxr must not flip the default build ON).
    # NOTE: the bootstrapped loader is built Release (CMAKE_BUILD_TYPE=Release). Linking it into a
    # Debug app trips lld-link /failifmismatch on _ITERATOR_DEBUG_LEVEL (loader=0, app=2), so an
    # OpenXR-ON build MUST be Release. Step 3.5 already guarantees that here: when OpenXR resolved
    # ON, $BuildType is Release (auto-promoted from the default Debug, or the caller passed
    # Release; an explicit -BuildType Debug + OpenXR ON aborts before reaching this point). So
    # CMAKE_BUILD_TYPE above is Release on this path. vr_runtime + vr_runtime_harness build and
    # link clean in Release against this loader.
    if ($openxrResolved -eq 'ON' -and $openxrLocalPrefixExists) {
        $configureFlags += "-DCMAKE_PREFIX_PATH=$($openxrLocalPrefix.Replace('\','/'))"
    }
    # clang-cl path: set the compiler explicitly (absolute, forward-slashed) and force the LLD
    # linker (lld-link). Everything else — Ninja generator, ninja program, OpenXR OFF — is
    # identical to the MSVC path, which stays byte-for-byte unchanged (no compiler flags).
    #
    # LLD selection is version-robust across the WHOLE documented CMake floor [3.24, ...):
    #   - -DCMAKE_LINKER_TYPE=LLD is the clean native mechanism but was only INTRODUCED in
    #     CMake 3.29. On a conforming CMake in [3.24, 3.29) it is an unknown cache variable
    #     that CMake stores but NEVER reads — silently ignored, so the linker would fall back
    #     to whatever clang-cl/CMake default to (may NOT be lld-link). The project floor is
    #     3.24 (CMakeLists.txt cmake_minimum_required + tools/toolchain.md), so relying on it
    #     alone is a real gap on the supported floor.
    #   - -fuse-ld=lld-link is a clang driver LINK flag honored on ALL CMake versions. It MUST
    #     go through the *linker*-flags variables (CMAKE_EXE/SHARED/MODULE_LINKER_FLAGS), NOT
    #     CMAKE_C/CXX_FLAGS: setting CMAKE_CXX_FLAGS on the command line REPLACES the default
    #     init flags CMake seeds for clang-cl's MSVC-compat mode (notably /EHsc), which
    #     disables exceptions and breaks the build (`cannot use 'try' with exceptions
    #     disabled`). The linker-flags variables only affect the link step (driven by the
    #     clang-cl driver), so they force lld-link without touching compilation.
    # We therefore ALWAYS append the portable -fuse-ld=lld-link to the linker-flags vars, and
    # ADDITIONALLY pass the native -DCMAKE_LINKER_TYPE=LLD only on CMake >= 3.29
    # (belt-and-suspenders; on >= 3.29 both agree on lld-link, on < 3.29 the portable linker
    # flag carries it alone). The gate $cmakeSupportsLinkerType is computed from the
    # running-CMake version probed in Step 1.5.
    if ($compilerResolved -eq 'clang-cl' -and $ClangClExe) {
        $cc = $ClangClExe.Replace('\', '/')
        $configureFlags += @(
            "-DCMAKE_C_COMPILER=$cc",
            "-DCMAKE_CXX_COMPILER=$cc",
            # Portable across the 3.24 floor — clang-cl honors this regardless of CMake
            # version. LINKER-flags only (never C/CXX_FLAGS — that would clobber /EHsc).
            '-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld-link',
            '-DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld-link',
            '-DCMAKE_MODULE_LINKER_FLAGS=-fuse-ld=lld-link'
        )
        # Native LLD selection only where CMake actually reads it (>= 3.29). On older CMake
        # this var is silently ignored, so we DON'T pass it (avoids a misleading unread cache
        # entry) and lean on -fuse-ld=lld-link above.
        if ($cmakeSupportsLinkerType) {
            $configureFlags += '-DCMAKE_LINKER_TYPE=LLD'
        }
        else {
            Write-Host "NOTE: running CMake $cmakeVersionString < 3.29 -> CMAKE_LINKER_TYPE unsupported; lld-link forced via -fuse-ld=lld-link only."
        }
    }
    $r = Invoke-Native -Exe $CMakeExe -Arguments $configureFlags
    $configureExit = $r.ExitCode
    $configureOk = ($r.ExitCode -eq 0)
    $log = Save-Log -Name 'configure' -Lines $r.Output
    Write-Host "configure exit=$configureExit (full log: $log)"
    if (-not $configureOk) {
        $failures.Add("configure: cmake exited $configureExit")
        Get-Tail -Lines $r.Output -Count 30 | ForEach-Object { Write-Host "  $_" }
    }
}
elseif ($abortBeforeBuild) {
    Write-Host 'configure: skipped (BuildType/OpenXR abort — see Step 3.5)'
}
else {
    $failures.Add('configure: skipped (environment setup failed)')
}

# ---------------------------------------------------------------------------
# Step 5: build
# ---------------------------------------------------------------------------
Write-Step 'Step 5/7: build all targets'
if (-not $abortBeforeBuild) {
    $buildOk = $false
    $buildExit = $null
}
if ($configureOk) {
    $r = Invoke-Native -Exe $CMakeExe -Arguments @('--build', $BuildDirFull)
    $buildExit = $r.ExitCode
    $buildOk = ($r.ExitCode -eq 0)
    $log = Save-Log -Name 'build' -Lines $r.Output
    Write-Host "build exit=$buildExit (full log: $log)"
    if (-not $buildOk) {
        $failures.Add("build: cmake --build exited $buildExit")
        Get-Tail -Lines $r.Output -Count 50 | ForEach-Object { Write-Host "  $_" }
    }
}
else {
    $failures.Add('build: skipped (configure failed or skipped)')
}

# ---------------------------------------------------------------------------
# Step 6: ctest
# ---------------------------------------------------------------------------
Write-Step 'Step 6/7: ctest --output-on-failure'
$ctestOk = $false
$ctestExit = $null
$ctestTotal = 0
$ctestPassed = 0
$ctestFailedNames = @()
if ($buildOk) {
    $r = Invoke-Native -Exe $CTestExe -Arguments @('--test-dir', $BuildDirFull, '--output-on-failure') -WorkDir $BuildDirFull
    $ctestExit = $r.ExitCode
    $log = Save-Log -Name 'ctest' -Lines $r.Output
    $ctestText = $r.Output -join "`n"
    if ($ctestText -match '(\d+)% tests passed, (\d+) tests failed out of (\d+)') {
        $failedCount = [int]$matches[2]
        $ctestTotal = [int]$matches[3]
        $ctestPassed = $ctestTotal - $failedCount
    }
    $inFailedBlock = $false
    foreach ($line in $r.Output) {
        if ($line -match 'The following tests FAILED:') { $inFailedBlock = $true; continue }
        if ($inFailedBlock -and $line -match '^\s*\d+\s*-\s*(\S+)\s*\(') { $ctestFailedNames += $matches[1] }
    }
    $ctestOk = ($ctestExit -eq 0 -and $ctestTotal -gt 0 -and $ctestPassed -eq $ctestTotal)
    Write-Host "ctest exit=$ctestExit passed=$ctestPassed/$ctestTotal (full log: $log)"
    if (-not $ctestOk) {
        if ($ctestTotal -eq 0) { $failures.Add("ctest: no tests were found (exit $ctestExit)") }
        else { $failures.Add("ctest: $($ctestTotal - $ctestPassed)/$ctestTotal failed: $($ctestFailedNames -join ', ')") }
        Get-Tail -Lines $r.Output -Count 50 | ForEach-Object { Write-Host "  $_" }
    }
}
else {
    $failures.Add('ctest: skipped (build failed or skipped)')
}

# ---------------------------------------------------------------------------
# Step 7: every tests/native/**/*.py validator, run directly from the repo root
# (independent of CTest registration, per BUILD-02/BUILD-04)
# ---------------------------------------------------------------------------
Write-Step 'Step 7/7: python validators (tests/native/**/*.py)'
$jsonschemaOk = $false
if ($abortBeforeBuild) {
    Write-Host 'validators: skipped (BuildType/OpenXR abort — see Step 3.5)'
}
elseif ($true) {
$validatorResults = @()
$validatorsOk = $true
$jsonschemaInfo = ''
if ($PythonExe) {
    # NOTE: no double quotes inside the -c snippet — PS 5.1 native arg passing strips them.
    $r = Invoke-Native -Exe $PythonExe -Arguments @('-c', "import importlib.metadata as m; print(m.version('jsonschema'))")
    if ($r.ExitCode -eq 0) {
        $jsonschemaOk = $true
        $jsonschemaInfo = "jsonschema $((($r.Output | Select-Object -First 1)).Trim())"
    }
    else {
        $jsonschemaInfo = 'jsonschema NOT importable'
        $failures.Add("python: jsonschema not importable by $PythonExe (fix: `"$PythonExe`" -m pip install jsonschema)")
    }

    $pyFiles = @(Get-ChildItem -LiteralPath (Join-Path $RepoRoot 'tests\native') -Recurse -Filter '*.py' | Sort-Object FullName)
    # One entry per script (no args) + the extra pinned invocation mirroring the CTest
    # registration repo_fingerprint_schema (validate_game_fingerprint.py config/games/repo.json).
    $invocations = @()
    foreach ($f in $pyFiles) {
        $rel = $f.FullName.Substring($RepoRoot.Length + 1).Replace('\', '/')
        $invocations += , @{ Script = $rel; Args = @() }
        if ($rel -eq 'tests/native/versioning/validate_game_fingerprint.py') {
            $invocations += , @{ Script = $rel; Args = @('config/games/repo.json') }
        }
    }
    foreach ($inv in $invocations) {
        $argv = @($inv.Script) + $inv.Args
        $r = Invoke-Native -Exe $PythonExe -Arguments $argv -WorkDir $RepoRoot
        $ok = ($r.ExitCode -eq 0)
        $summaryLines = @($r.Output | Where-Object { $_ -and $_.Trim() })
        if ($summaryLines.Count -gt 0) { $summary = $summaryLines[-1] } else { $summary = '' }
        if ($summary.Length -gt 240) { $summary = $summary.Substring(0, 240) }
        $label = $inv.Script
        if ($inv.Args.Count -gt 0) { $label = "$($inv.Script) $($inv.Args -join ' ')" }
        $validatorResults += [ordered]@{
            script    = $inv.Script
            args      = @($inv.Args)
            exit_code = $r.ExitCode
            ok        = $ok
            summary   = $summary
        }
        if ($ok) { Write-Host ('  PASS ' + $label) }
        else {
            $validatorsOk = $false
            $failures.Add("validator: $label exited $($r.ExitCode): $summary")
            Write-Host ('  FAIL ' + $label + " (exit $($r.ExitCode))")
            Get-Tail -Lines $r.Output -Count 15 | ForEach-Object { Write-Host "    $_" }
        }
    }
    if ($pyFiles.Count -eq 0) {
        $validatorsOk = $false
        $failures.Add('validator: no tests/native/**/*.py files found (expected 15)')
    }
}
else {
    $validatorsOk = $false
    $failures.Add('python: no python interpreter available; validators skipped')
}
$validatorsPassed = @($validatorResults | Where-Object { $_.ok }).Count
$validatorsTotal = @($validatorResults).Count
$validatorsOk = $validatorsOk -and $jsonschemaOk
}
Write-Host "validators passed=$validatorsPassed/$validatorsTotal ($jsonschemaInfo)"

# ---------------------------------------------------------------------------
# Toolchain versions (for the evidence record)
# ---------------------------------------------------------------------------
function Get-VersionLine {
    param([string]$Exe, [string[]]$VersionArguments = @('--version'))
    if (-not $Exe) { return 'unavailable' }
    $r = Invoke-Native -Exe $Exe -Arguments $VersionArguments
    $line = @($r.Output | Where-Object { $_ -and $_.Trim() } | Select-Object -First 1)
    if ($line) { return "$line".Trim() }
    return 'unavailable'
}
$clBanner = 'unavailable'
if ($envOk) {
    $clCmd = Get-Command cl.exe -ErrorAction SilentlyContinue
    if ($clCmd) {
        $r = Invoke-Native -Exe $clCmd.Source -Arguments @()
        $banner = @($r.Output | Where-Object { $_ -match 'Compiler Version' } | Select-Object -First 1)
        if ($banner) { $clBanner = "$banner".Trim() + " [$($clCmd.Source)]" }
    }
}
# clang-cl / lld-link banners (only meaningful when clang-cl resolved). clang-cl --version
# prints the clang banner; the 'clang version' line is the honest evidence of which LLVM built.
$clangBanner = 'unavailable'
$lldBanner = 'unavailable'
if ($compilerResolved -eq 'clang-cl' -and $ClangClExe) {
    $r = Invoke-Native -Exe $ClangClExe -Arguments @('--version')
    $banner = @($r.Output | Where-Object { $_ -match 'clang version' } | Select-Object -First 1)
    if (-not $banner) { $banner = @($r.Output | Where-Object { $_ -and $_.Trim() } | Select-Object -First 1) }
    if ($banner) { $clangBanner = "$banner".Trim() + " [$ClangClExe]" }
    if ($LldLinkExe) {
        $lldBanner = "$(Get-VersionLine -Exe $LldLinkExe) [$LldLinkExe]"
    }
}
$toolchain = [ordered]@{
    cmake      = "$(Get-VersionLine -Exe $CMakeExe) [$CMakeExe]"
    ninja      = "ninja $(Get-VersionLine -Exe $NinjaExe) [$NinjaExe]"
    ctest      = "$(Get-VersionLine -Exe $CTestExe) [$CTestExe]"
    msvc_cl    = $clBanner
    clang_cl   = $clangBanner
    lld_link   = $lldBanner
    python     = "$(Get-VersionLine -Exe $PythonExe) [$PythonExe]"
    jsonschema = $jsonschemaInfo
    git        = (Get-VersionLine -Exe $gitExe)
    vcvars_bat = "$env:VRCLIENT_VCVARS_BAT"
}

# ---------------------------------------------------------------------------
# Verdict + evidence JSON + ledger row
# ---------------------------------------------------------------------------
$green = $envOk -and $configureOk -and $buildOk -and $ctestOk -and $validatorsOk

$result = [ordered]@{
    schema        = 'vrclient-ci-run/1'
    timestamp_utc = $startUtc.ToString("yyyy-MM-dd'T'HH:mm:ss'Z'")
    repo_root     = $RepoRoot
    git           = [ordered]@{
        commit = $gitCommit
        short  = $gitShort
        branch = $gitBranch
        dirty  = $gitDirty
    }
    parameters    = [ordered]@{
        build_dir           = $BuildDir
        build_type          = $BuildType
        build_type_explicit = $BuildTypeExplicit
        build_type_decision = $buildTypeDecision
        compiler            = $Compiler
        note                = $Note
        detours_smoke       = [bool]$DetoursSmoke
        detours_withdll     = $DetoursWithDll
        easyhook_attach_smoke = [bool]$EasyHookAttachSmoke
        easyhook_attach_loader = $EasyHookAttachLoader
    }
    openxr        = [ordered]@{
        requested = $OpenXR
        resolved  = $openxrResolved
        decision  = $openxrDecision
    }
    compiler      = [ordered]@{
        requested = $Compiler
        resolved  = $compilerResolved
        decision  = $compilerDecision
        exe       = $ClangClExe
        linker    = $LldLinkExe
    }
    environment   = [ordered]@{
        ok    = $envOk
        error = $envError
    }
    configure     = [ordered]@{
        flags     = @($configureFlags)
        exit_code = $configureExit
        ok        = $configureOk
    }
    build         = [ordered]@{
        exit_code = $buildExit
        ok        = $buildOk
    }
    ctest         = [ordered]@{
        exit_code    = $ctestExit
        total        = $ctestTotal
        passed       = $ctestPassed
        failed       = ($ctestTotal - $ctestPassed)
        failed_names = @($ctestFailedNames)
        ok           = $ctestOk
    }
    python_validators = [ordered]@{
        total   = $validatorsTotal
        passed  = $validatorsPassed
        ok      = $validatorsOk
        runs    = @($validatorResults)
    }
    toolchain     = $toolchain
    result        = [ordered]@{
        green    = $green
        failures = @($failures)
    }
}

$runsDir = Join-Path $RepoRoot '.planning\evidence\runs'
New-Item -ItemType Directory -Force -Path $runsDir | Out-Null
$jsonName = "$stamp-$gitShort.json"
$jsonPath = Join-Path $runsDir $jsonName
$jsonText = ($result | ConvertTo-Json -Depth 8)
[System.IO.File]::WriteAllText($jsonPath, $jsonText, (New-Object System.Text.UTF8Encoding($false)))
Write-Host "`nEvidence JSON: $jsonPath"

$ledgerPath = Join-Path $RepoRoot '.planning\evidence\LEDGER.md'
if (-not (Test-Path -LiteralPath $ledgerPath)) {
    $header = @(
        '# VRClient Evidence Ledger',
        '',
        '**Ledger rule:** a phase''s provisional status is retired ONLY by a ledger entry recording a',
        'real run. One run = one JSON file under `.planning/evidence/runs/` + one row below.',
        'Rows are append-only. Written by `scripts/ci/build-and-test.ps1`.',
        '',
        '| Timestamp (UTC) | Commit | Dirty | OpenXR | Build | CTest | Validators | Result | Run JSON | Notes |',
        '|---|---|---|---|---|---|---|---|---|---|'
    )
    [System.IO.File]::WriteAllText($ledgerPath, (($header -join "`r`n") + "`r`n"), (New-Object System.Text.UTF8Encoding($false)))
}
if ($green) { $resultWord = 'GREEN' } else { $resultWord = 'RED' }
if ($buildOk) { $buildWord = 'OK' } elseif ($configureOk) { $buildWord = 'FAIL' } else { $buildWord = 'SKIP/FAIL' }
if ($gitDirty -eq $true) { $dirtyWord = 'yes' } elseif ($gitDirty -eq $false) { $dirtyWord = 'no' } else { $dirtyWord = 'unknown' }
# Surface the resolved compiler in the existing free-text Notes cell (no ledger column-schema
# change — older rows stay valid; the run JSON's compiler block is the authoritative record).
$compilerTag = "[$compilerResolved ($Compiler)]"
if ([string]::IsNullOrWhiteSpace($Note)) { $noteWithCompiler = $compilerTag }
else { $noteWithCompiler = "$compilerTag $Note" }
$noteCell = $noteWithCompiler.Replace('|', '\|')
$row = "| $($result.timestamp_utc) | $gitShort | $dirtyWord | $openxrResolved ($OpenXR) | $buildWord | $ctestPassed/$ctestTotal | $validatorsPassed/$validatorsTotal | $resultWord | runs/$jsonName | $noteCell |"
[System.IO.File]::AppendAllText($ledgerPath, $row + "`r`n", (New-Object System.Text.UTF8Encoding($false)))
Write-Host "Ledger row appended: $ledgerPath"

Write-Step "RESULT: $resultWord"
if ($failures.Count -gt 0) {
    Write-Host 'Failures:'
    foreach ($f in $failures) { Write-Host "  - $f" }
}
if ($green) { exit 0 } else { exit 1 }

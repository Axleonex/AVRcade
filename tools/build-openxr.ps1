#Requires -Version 5.1
<#
.SYNOPSIS
    Reproducible bootstrap: build + install the Khronos OpenXR loader from source into a
    project-local, gitignored prefix so find_package(OpenXR) can resolve for an explicit
    '-OpenXR on' build.

.DESCRIPTION
    Bolt-on Phase 3 prerequisite (real OpenXR runtime compile). This is the OPEN-SOURCE
    (Apache-2.0) Khronos OpenXR-SDK loader, built FROM SOURCE at a PINNED tag — no prebuilt
    binaries are downloaded and nothing is committed to the repo. The SDK source, build tree,
    and install prefix all live under the gitignored external/ directory.

    The ONLY step that needs network is the shallow git clone of the pinned tag. Everything
    else (configure / build / install) is fully offline. Re-running is idempotent: if the
    loader's CMake package config already exists at the prefix it is skipped unless -Force.

    Pipeline:
      1. Dot-source tools/setup-build-env.ps1 -Compiler <Compiler> to get the SAME pinned
         toolchain the CI build uses (VRCLIENT_CMAKE_EXE / VRCLIENT_NINJA_EXE, vcvars imported,
         and on clang-cl: VRCLIENT_CLANG_CL_EXE / VRCLIENT_LLD_LINK_EXE).
      2. Idempotency check: skip if <Prefix>/lib/cmake/openxr/OpenXRConfig.cmake exists (or the
         config is found anywhere under <Prefix>) and -Force was not given.
      3. Shallow-clone github.com/KhronosGroup/OpenXR-SDK at the pinned -Ref into
         external/_openxr-src (gitignored), record the resolved commit.
      4. Configure (-G Ninja, Release, CMAKE_INSTALL_PREFIX=<Prefix>), build the openxr_loader
         target, install. Parallelism is capped at 2 (CMAKE_BUILD_PARALLEL_LEVEL=2) because the
         host is RAM-constrained.
      5. Print install prefix, resolved OpenXR commit, and loader version; fail loudly on any
         nonzero step.
      6. On success, remove the disposable build tree (external/_openxr-build) and shallow
         source clone (external/_openxr-src); the install prefix external/openxr is kept.
         -KeepIntermediate preserves both. (The Step 2 idempotent early-exit never reaches here.)

    DOES NOT touch CMAKE_PREFIX_PATH or the default build. Wiring the prefix into a build is the
    job of scripts/ci/build-and-test.ps1 -OpenXR on (which adds CMAKE_PREFIX_PATH only then);
    the default '-OpenXR auto' must NOT discover this non-standard project-local prefix.

.PARAMETER Prefix
    Install prefix for the loader (CMAKE_INSTALL_PREFIX). Default: <repo>/external/openxr.

.PARAMETER Ref
    Pinned Khronos OpenXR-SDK git tag/ref to clone (--branch <Ref> --depth 1).
    Default: release-1.1.43 (Apache-2.0 stable loader release).

.PARAMETER Compiler
    msvc | clang-cl | auto (default auto). Passed straight to setup-build-env.ps1. On clang-cl
    the OpenXR configure mirrors build-and-test.ps1: -DCMAKE_C_COMPILER / -DCMAKE_CXX_COMPILER
    = clang-cl (absolute, forward-slashed) plus LLD linker selection.

.PARAMETER Force
    Rebuild + reinstall even if the loader's CMake package config already exists at <Prefix>.

.PARAMETER KeepIntermediate
    Keep the disposable build tree (external/_openxr-build) and the shallow source clone
    (external/_openxr-src) after a successful install. By DEFAULT both are removed once Step 5
    verification confirms a working prefix (the install prefix external/openxr is always kept).
    Use this when debugging the OpenXR build itself. Cleanup only runs on success; a failed
    build throws earlier and leaves the trees in place for inspection.

.NOTES
    [PowerShell] powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-openxr.ps1
    [PowerShell] tools\build-openxr.ps1 -Ref release-1.1.43 -Compiler clang-cl -Force

    Source / license: https://github.com/KhronosGroup/OpenXR-SDK (Apache-2.0). Pinned, built
    from source, project-local. Clone is the only network step; normal builds stay offline.
#>
[CmdletBinding()]
param(
    [string]$Prefix = '',
    [string]$Ref = 'release-1.1.43',
    [ValidateSet('msvc', 'clang-cl', 'auto')][string]$Compiler = 'auto',
    [switch]$Force,
    [switch]$KeepIntermediate
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

if ([string]::IsNullOrWhiteSpace($Prefix)) {
    $Prefix = Join-Path $RepoRoot 'external\openxr'
}
elseif (-not [System.IO.Path]::IsPathRooted($Prefix)) {
    $Prefix = Join-Path $RepoRoot $Prefix
}
$SrcDir = Join-Path $RepoRoot 'external\_openxr-src'
$BuildDir = Join-Path $RepoRoot 'external\_openxr-build'

function Write-Step { param([string]$Message) Write-Host "`n=== $Message ===" }

# Runs a native executable with stderr merged, never throwing on stderr writes;
# returns @{ ExitCode; Output(string[]) }. Mirrors build-and-test.ps1's Invoke-Native.
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

# Locate the installed OpenXRConfig.cmake anywhere under the prefix (the loader lands it at
# lib/cmake/openxr/ but we search robustly in case a future SDK relocates it).
function Find-OpenXRConfig {
    param([string]$Root)
    if (-not (Test-Path -LiteralPath $Root)) { return $null }
    $hit = Get-ChildItem -LiteralPath $Root -Recurse -Filter 'OpenXRConfig.cmake' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($hit) { return $hit.FullName }
    return $null
}

# ---------------------------------------------------------------------------
# Step 1: toolchain
# ---------------------------------------------------------------------------
Write-Step "Step 1/5: import build environment (tools/setup-build-env.ps1) [compiler: $Compiler]"
. (Join-Path $RepoRoot 'tools\setup-build-env.ps1') -Compiler $Compiler

$CMakeExe = $env:VRCLIENT_CMAKE_EXE
$NinjaExe = $env:VRCLIENT_NINJA_EXE
$compilerResolved = $env:VRCLIENT_COMPILER
if (-not $compilerResolved) { $compilerResolved = 'msvc' }
$ClangClExe = $env:VRCLIENT_CLANG_CL_EXE
if (-not $CMakeExe) { throw 'build-openxr: VRCLIENT_CMAKE_EXE not set after setup-build-env.' }
if (-not $NinjaExe) { throw 'build-openxr: VRCLIENT_NINJA_EXE not set after setup-build-env.' }
Write-Host "cmake   : $CMakeExe"
Write-Host "ninja   : $NinjaExe"
Write-Host "compiler: $compilerResolved (requested: $Compiler)"

# Probe CMake version for the >= 3.29 CMAKE_LINKER_TYPE native LLD selection (mirrors
# build-and-test.ps1: always force -fuse-ld=lld-link, additionally CMAKE_LINKER_TYPE on >=3.29).
$cmakeVersionString = 'unknown'
$cmakeSupportsLinkerType = $false
$cv = Invoke-Native -Exe $CMakeExe -Arguments @('--version')
$cvText = ($cv.Output -join "`n")
if ($cvText -match 'cmake version (\d+)\.(\d+)\.(\d+)') {
    $cmakeVersionString = "$($matches[1]).$($matches[2]).$($matches[3])"
    $cmv = New-Object System.Version([int]$matches[1], [int]$matches[2], [int]$matches[3])
    $cmakeSupportsLinkerType = ($cmv -ge (New-Object System.Version(3, 29, 0)))
}

# ---------------------------------------------------------------------------
# Step 2: idempotency
# ---------------------------------------------------------------------------
Write-Step "Step 2/5: idempotency check (prefix: $Prefix)"
$existingConfig = Find-OpenXRConfig -Root $Prefix
if ($existingConfig -and -not $Force) {
    Write-Host "OpenXR loader already installed; skipping (pass -Force to rebuild)."
    Write-Host "OpenXRConfig.cmake : $existingConfig"
    $libDir = Join-Path $Prefix 'lib'
    $loaderLib = Get-ChildItem -LiteralPath $libDir -Recurse -Filter 'openxr_loader*.lib' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($loaderLib) { Write-Host "loader import lib  : $($loaderLib.FullName)" }
    Write-Host "Install prefix     : $Prefix"
    exit 0
}
if ($existingConfig -and $Force) {
    Write-Host "-Force given; rebuilding even though a config already exists at: $existingConfig"
}

# ---------------------------------------------------------------------------
# Step 3: shallow clone the pinned tag (ONLY network step)
# ---------------------------------------------------------------------------
Write-Step "Step 3/5: clone KhronosGroup/OpenXR-SDK @ $Ref (shallow; the only network step)"
$gitCmd = Get-Command git.exe -ErrorAction SilentlyContinue
if (-not $gitCmd) { throw 'build-openxr: git.exe not found on PATH (required for the source clone).' }
$gitExe = $gitCmd.Source

# Fresh clone each run for reproducibility (cheap shallow clone). Remove a stale src tree first.
if (Test-Path -LiteralPath $SrcDir) {
    Write-Host "Removing stale source tree: $SrcDir"
    Remove-Item -LiteralPath $SrcDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $SrcDir) | Out-Null

$cloneArgs = @(
    'clone', '--depth', '1', '--branch', $Ref,
    'https://github.com/KhronosGroup/OpenXR-SDK.git', $SrcDir
)
$r = Invoke-Native -Exe $gitExe -Arguments $cloneArgs
$r.Output | ForEach-Object { Write-Host "  $_" }
if ($r.ExitCode -ne 0) {
    throw "build-openxr: git clone of OpenXR-SDK @ '$Ref' failed (exit $($r.ExitCode)). Check the pinned -Ref tag and network access (this is the only networked step)."
}

# Record the resolved commit (an annotated tag like release-1.1.43 points at a tree, so HEAD
# after checkout is the underlying commit). The src tree lives on a filesystem that does not
# record ownership, so a bare `git rev-parse` trips git's "dubious ownership" guard — retry
# with a safe.directory exception, mirroring build-and-test.ps1's Invoke-Git.
$openxrCommit = 'unknown'
$revArgs = @('-C', $SrcDir, 'rev-parse', 'HEAD')
$r = Invoke-Native -Exe $gitExe -Arguments $revArgs
if ($r.ExitCode -ne 0 -and (($r.Output -join ' ') -match 'dubious ownership')) {
    $r = Invoke-Native -Exe $gitExe -Arguments (@('-c', "safe.directory=$($SrcDir.Replace('\','/'))") + $revArgs)
}
if ($r.ExitCode -eq 0) { $openxrCommit = ($r.Output | Select-Object -First 1).Trim() }
Write-Host "Resolved OpenXR commit: $openxrCommit ($Ref)"

# ---------------------------------------------------------------------------
# Step 4: configure + build openxr_loader + install
# ---------------------------------------------------------------------------
Write-Step "Step 4/5: configure (Ninja, Release) -> build openxr_loader -> install ($Prefix)"
if (Test-Path -LiteralPath $BuildDir) {
    Write-Host "Removing stale build tree: $BuildDir"
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

# Cap parallelism: the host is RAM-constrained, the loader OOMs at full -j. Honored by
# `cmake --build ... --parallel 2` AND as a belt for any nested invocation.
$env:CMAKE_BUILD_PARALLEL_LEVEL = '2'

$configureFlags = @(
    '-S', $SrcDir,
    '-B', $BuildDir,
    '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_MAKE_PROGRAM=$($NinjaExe.Replace('\','/'))",
    "-DCMAKE_INSTALL_PREFIX=$($Prefix.Replace('\','/'))",
    # Build only the loader (no API layers / tests / conformance) to keep the build small + fast.
    '-DDYNAMIC_LOADER=OFF',
    '-DBUILD_TESTS=OFF',
    '-DBUILD_API_LAYERS=OFF',
    '-DBUILD_CONFORMANCE_TESTS=OFF'
)
# clang-cl path mirrors build-and-test.ps1 exactly: explicit clang-cl compiler + portable
# -fuse-ld=lld-link on the linker-flags vars (NEVER C/CXX_FLAGS — that clobbers /EHsc), plus the
# native CMAKE_LINKER_TYPE=LLD only on CMake >= 3.29.
if ($compilerResolved -eq 'clang-cl' -and $ClangClExe) {
    $cc = $ClangClExe.Replace('\', '/')
    $configureFlags += @(
        "-DCMAKE_C_COMPILER=$cc",
        "-DCMAKE_CXX_COMPILER=$cc",
        '-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld-link',
        '-DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld-link',
        '-DCMAKE_MODULE_LINKER_FLAGS=-fuse-ld=lld-link'
    )
    if ($cmakeSupportsLinkerType) {
        $configureFlags += '-DCMAKE_LINKER_TYPE=LLD'
    }
    else {
        Write-Host "NOTE: running CMake $cmakeVersionString < 3.29 -> CMAKE_LINKER_TYPE unsupported; lld-link forced via -fuse-ld=lld-link only."
    }
}

$r = Invoke-Native -Exe $CMakeExe -Arguments $configureFlags
$r.Output | ForEach-Object { Write-Host "  $_" }
if ($r.ExitCode -ne 0) { throw "build-openxr: CMake configure failed (exit $($r.ExitCode))." }

$r = Invoke-Native -Exe $CMakeExe -Arguments @(
    '--build', $BuildDir, '--target', 'openxr_loader', '--config', 'Release', '--parallel', '2'
)
$r.Output | ForEach-Object { Write-Host "  $_" }
if ($r.ExitCode -ne 0) { throw "build-openxr: building target openxr_loader failed (exit $($r.ExitCode))." }

$r = Invoke-Native -Exe $CMakeExe -Arguments @(
    '--install', $BuildDir, '--config', 'Release'
)
$r.Output | ForEach-Object { Write-Host "  $_" }
if ($r.ExitCode -ne 0) { throw "build-openxr: install failed (exit $($r.ExitCode))." }

# ---------------------------------------------------------------------------
# Step 5: verify + report
# ---------------------------------------------------------------------------
Write-Step 'Step 5/5: verify install'
$installedConfig = Find-OpenXRConfig -Root $Prefix
if (-not $installedConfig) {
    throw "build-openxr: install completed but OpenXRConfig.cmake was not found anywhere under $Prefix."
}
$loaderLib = Get-ChildItem -LiteralPath (Join-Path $Prefix 'lib') -Recurse -Filter 'openxr_loader*.lib' -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $loaderLib) {
    # static loader may also land as openxr_loader.lib directly; do a prefix-wide sweep as a fallback.
    $loaderLib = Get-ChildItem -LiteralPath $Prefix -Recurse -Filter 'openxr_loader*.lib' -ErrorAction SilentlyContinue |
        Select-Object -First 1
}
if (-not $loaderLib) {
    throw "build-openxr: install completed but no openxr_loader*.lib import library was found under $Prefix."
}

# Loader version: prefer the OpenXRConfigVersion.cmake (PACKAGE_VERSION), else fall back to the tag.
$loaderVersion = $Ref
$versionConfig = Join-Path (Split-Path -Parent $installedConfig) 'OpenXRConfigVersion.cmake'
if (Test-Path -LiteralPath $versionConfig) {
    $vc = Get-Content -LiteralPath $versionConfig -Raw
    if ($vc -match 'PACKAGE_VERSION\s+"?([\d.]+)"?') { $loaderVersion = $matches[1] }
}

Write-Host ''
Write-Host '=== OpenXR loader installed (open-source, Apache-2.0, built from source) ==='
Write-Host "Install prefix     : $Prefix"
Write-Host "OpenXR commit      : $openxrCommit ($Ref)"
Write-Host "Loader version     : $loaderVersion"
Write-Host "OpenXRConfig.cmake : $installedConfig"
Write-Host "Loader import lib  : $($loaderLib.FullName)"

# ---------------------------------------------------------------------------
# Step 5b: clean up disposable intermediates (build tree + shallow src clone).
# Runs ONLY after Step 5 verification above has succeeded (any earlier failure
# throws under ErrorActionPreference=Stop and leaves the trees for debugging).
# The install prefix ($Prefix = external/openxr) is ALWAYS kept; only the
# regenerable build/source trees are removed. -KeepIntermediate preserves both.
# The Step 2 idempotent early-exit returns before this block, so re-running with
# an existing prefix is unaffected.
# ---------------------------------------------------------------------------
if ($KeepIntermediate) {
    Write-Host ''
    Write-Host "Intermediates kept (-KeepIntermediate): $BuildDir ; $SrcDir"
}
else {
    foreach ($tree in @($BuildDir, $SrcDir)) {
        if (Test-Path -LiteralPath $tree) {
            Remove-Item -LiteralPath $tree -Recurse -Force -ErrorAction SilentlyContinue
            if (Test-Path -LiteralPath $tree) {
                Write-Host "NOTE: could not fully remove intermediate (non-fatal): $tree"
            }
            else {
                Write-Host "Removed disposable intermediate: $tree"
            }
        }
    }
    Write-Host '(intermediates cleaned; pass -KeepIntermediate to keep external/_openxr-build + external/_openxr-src for debugging)'
}

exit 0

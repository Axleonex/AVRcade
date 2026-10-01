#Requires -Version 5.1
<#
.SYNOPSIS
    Reproducible bootstrap: build + install a self-contained, from-source Vulkan SDK into a
    project-local, gitignored prefix so a later Vulkan renderer port can find_package(Vulkan),
    link the loader (vulkan-1), compile GLSL/HLSL -> SPIR-V (glslang), and dev-run with the
    VK_LAYER_KHRONOS_validation layer.

.DESCRIPTION
    Vulkan renderer-port PREREQUISITE. This builds the OPEN-SOURCE (Apache-2.0 / MIT) Khronos
    Vulkan stack FROM SOURCE at a single PINNED vulkan-sdk-* tag (lockstep across every repo) -
    no prebuilt LunarG SDK is downloaded and nothing under external/ is committed. All source,
    build trees, and the install prefix live under the gitignored external/ directory.

    Mirrors tools/build-openxr.ps1's proven structure exactly: pinned ref, idempotent early-exit,
    modest parallelism (RAM-constrained host), clone-is-the-only-network-step, clean disposable
    intermediates by default (-KeepIntermediate to keep them).

    Components (ALL pinned to the SAME -Ref tag for header/loader/layer/SPIRV lockstep):
      Vulkan-Headers          (Khronos)  -> vulkan/*.h + VulkanHeadersConfig.cmake          [ESSENTIAL]
      Vulkan-Loader           (Khronos)  -> vulkan-1.dll + vulkan-1.lib + VulkanLoaderConfig [ESSENTIAL]
      glslang                 (Khronos)  -> glslang(.exe) (the former glslangValidator) + libs[ESSENTIAL]
      SPIRV-Headers           (Khronos)  -> SPIR-V grammar headers (VVL dep)                 [for layers]
      SPIRV-Tools             (Khronos)  -> SPIRV-Tools libs (VVL dep)                        [for layers]
      Vulkan-ValidationLayers (Khronos)  -> VK_LAYER_KHRONOS_validation .dll + .json manifest [DEV-ONLY]

    GRACEFUL DEGRADATION (critical): the three ESSENTIALS (Headers + Loader + glslang) are built
    and verified FIRST. The validation-layer stack (SPIRV-Headers, SPIRV-Tools, then the heavy
    Vulkan-ValidationLayers) is attempted AFTER, at parallelism 1. If ANY layer-stack step fails
    (OOM / heap exhaustion on this RAM-tight host) the run DOES NOT fail: essentials stay intact,
    the layers are reported as DEFERRED, and exit is still 0. The renderer port can proceed on
    essentials; validation can be added later by re-running (idempotency rebuilds only what's
    missing) or with -Force.

    The validation layer is DEV-ONLY: it is runtime-loaded via VK_LAYER_PATH / VK_INSTANCE_LAYERS,
    never linked into or shipped with the product. This script installs it into the prefix but
    does NOT wire it into any product target, CMAKE_PREFIX_PATH, or the default build.

    The ONLY network step is the shallow git clone of each pinned tag. Everything else (configure /
    build / install) is fully offline. Deps are pinned by us cloning SPIRV-Headers/SPIRV-Tools at
    the same tag and feeding VVL their install dirs (UPDATE_DEPS=OFF) - so VVL never re-downloads.

    Pipeline:
      1. Dot-source tools/setup-build-env.ps1 -Compiler <Compiler> for the SAME pinned toolchain
         the CI build uses (VRCLIENT_CMAKE_EXE / VRCLIENT_NINJA_EXE, vcvars imported; on clang-cl
         VRCLIENT_CLANG_CL_EXE / VRCLIENT_LLD_LINK_EXE).
      2. Idempotency: skip if the loader (vulkan-1.lib) AND - unless -SkipValidationLayers - the
         validation layer manifest already exist at <Prefix>, and -Force was not given.
      3. Clone each needed Khronos repo at the pinned -Ref into external/_vulkan-src/<repo>
         (gitignored, shallow), record resolved commits. (Layer-stack repos are cloned lazily,
         only when the layer build is attempted.)
      4. ESSENTIALS: (a) Vulkan-Headers install, (b) Vulkan-Loader build+install (vulkan-1),
         (c) glslang build+install (glslang binary + libs). Verify all three -> essentialsInstalled.
      5. LAYERS (graceful): SPIRV-Headers install, SPIRV-Tools build+install, then
         Vulkan-ValidationLayers build+install at parallelism 1. Any failure -> DEFERRED, not fatal.
      6. Report: install prefix, resolved tag + per-repo commits, which components installed,
         and whether validation layers built or were deferred (with the reason).
      7. On success, remove disposable build trees + the shallow source clones; the install prefix
         external/vulkan is kept. -KeepIntermediate preserves them.

    DOES NOT touch CMAKE_PREFIX_PATH or the default build. Wiring this prefix into a build is the
    job of a later renderer-port increment.

.PARAMETER Prefix
    Install prefix for the whole stack (CMAKE_INSTALL_PREFIX). Default: <repo>/external/vulkan.

.PARAMETER Ref
    Pinned Khronos vulkan-sdk-* git tag cloned for EVERY repo (--branch <Ref> --depth 1).
    Default: vulkan-sdk-1.4.350.0 (verified present across Vulkan-Headers / Vulkan-Loader /
    Vulkan-ValidationLayers / glslang / SPIRV-Tools / SPIRV-Headers).

.PARAMETER Compiler
    msvc | clang-cl | auto (default auto). Passed straight to setup-build-env.ps1.

.PARAMETER Force
    Rebuild + reinstall even if the loader / layer already exist at <Prefix>.

.PARAMETER SkipValidationLayers
    Build ONLY the essentials (Headers + Loader + glslang); do not attempt the SPIRV / VVL stack.
    Idempotency then only requires the loader to consider the prefix complete.

.PARAMETER KeepIntermediate
    Keep the disposable build trees (external/_vulkan-build/*) and shallow source clones
    (external/_vulkan-src/*) after a successful run. By DEFAULT they are removed once verification
    confirms the prefix (the install prefix external/vulkan is always kept). Cleanup only runs at
    the end of a successful run; a hard failure throws earlier and leaves trees for inspection.

.NOTES
    [PowerShell] powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-vulkan.ps1
    [PowerShell] tools\build-vulkan.ps1 -Ref vulkan-sdk-1.4.350.0 -Compiler clang-cl -Force
    [PowerShell] tools\build-vulkan.ps1 -SkipValidationLayers   # essentials only

    Sources / licenses: github.com/KhronosGroup/{Vulkan-Headers,Vulkan-Loader,glslang,
    SPIRV-Headers,SPIRV-Tools,Vulkan-ValidationLayers} (Apache-2.0 / MIT). Pinned, built from
    source, project-local. Clone is the only network step; normal builds stay offline.
#>
[CmdletBinding()]
param(
    [string]$Prefix = '',
    [string]$Ref = 'vulkan-sdk-1.4.350.0',
    [ValidateSet('msvc', 'clang-cl', 'auto')][string]$Compiler = 'auto',
    [switch]$Force,
    [switch]$SkipValidationLayers,
    [switch]$KeepIntermediate
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

if ([string]::IsNullOrWhiteSpace($Prefix)) {
    $Prefix = Join-Path $RepoRoot 'external\vulkan'
}
elseif (-not [System.IO.Path]::IsPathRooted($Prefix)) {
    $Prefix = Join-Path $RepoRoot $Prefix
}
$SrcRoot = Join-Path $RepoRoot 'external\_vulkan-src'
$BuildRoot = Join-Path $RepoRoot 'external\_vulkan-build'

function Write-Step { param([string]$Message) Write-Host "`n=== $Message ===" }

# Runs a native executable with stderr merged, never throwing on stderr writes;
# returns @{ ExitCode; Output(string[]) }. Mirrors build-openxr.ps1's Invoke-Native.
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

# Heuristic: does this build output look like an out-of-memory / heap-exhaustion failure?
# Used ONLY for the graceful-degradation classification of the validation-layer stack - an
# OOM there is DEFERRED (non-fatal); any other failure of the layer stack is also deferred
# (essentials already shipped), but this lets the report state the reason honestly.
function Test-LooksLikeOOM {
    param([string[]]$Output)
    $text = ($Output -join "`n")
    return ($text -match '(?i)out of memory|heap (space|exhaust)|cannot allocate|bad_alloc|virtual memory|LNK1102|C1060|C1076|compiler is out of heap|Killed|fatal error C1001.*memory')
}

# ---------------------------------------------------------------------------
# Shallow-clone one Khronos repo at the pinned tag into external/_vulkan-src/<name>.
# Returns the resolved commit. THIS is the only networked operation.
# ---------------------------------------------------------------------------
function Invoke-PinnedClone {
    param(
        [Parameter(Mandatory = $true)][string]$GitExe,
        [Parameter(Mandatory = $true)][string]$Repo,   # e.g. Vulkan-Headers
        [Parameter(Mandatory = $true)][string]$Dest
    )
    if (Test-Path -LiteralPath $Dest) {
        Write-Host "Removing stale source tree: $Dest"
        Remove-Item -LiteralPath $Dest -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Dest) | Out-Null

    $cloneArgs = @(
        'clone', '--depth', '1', '--branch', $Ref,
        "https://github.com/KhronosGroup/$Repo.git", $Dest
    )
    $r = Invoke-Native -Exe $GitExe -Arguments $cloneArgs
    $r.Output | ForEach-Object { Write-Host "  $_" }
    if ($r.ExitCode -ne 0) {
        throw "build-vulkan: git clone of $Repo @ '$Ref' failed (exit $($r.ExitCode)). Check the pinned -Ref tag and network access (this is the only networked step)."
    }

    # Resolve commit; retry past git's "dubious ownership" guard on non-ownership filesystems.
    $commit = 'unknown'
    $revArgs = @('-C', $Dest, 'rev-parse', 'HEAD')
    $r = Invoke-Native -Exe $GitExe -Arguments $revArgs
    if ($r.ExitCode -ne 0 -and (($r.Output -join ' ') -match 'dubious ownership')) {
        $r = Invoke-Native -Exe $GitExe -Arguments (@('-c', "safe.directory=$($Dest.Replace('\','/'))") + $revArgs)
    }
    if ($r.ExitCode -eq 0) { $commit = ($r.Output | Select-Object -First 1).Trim() }
    Write-Host "Resolved ${Repo} commit: $commit ($Ref)"
    return $commit
}

# ---------------------------------------------------------------------------
# Step 1: toolchain
# ---------------------------------------------------------------------------
Write-Step "Step 1/6: import build environment (tools/setup-build-env.ps1) [compiler: $Compiler]"
. (Join-Path $RepoRoot 'tools\setup-build-env.ps1') -Compiler $Compiler

$CMakeExe = $env:VRCLIENT_CMAKE_EXE
$NinjaExe = $env:VRCLIENT_NINJA_EXE
$compilerResolved = $env:VRCLIENT_COMPILER
if (-not $compilerResolved) { $compilerResolved = 'msvc' }
$ClangClExe = $env:VRCLIENT_CLANG_CL_EXE
if (-not $CMakeExe) { throw 'build-vulkan: VRCLIENT_CMAKE_EXE not set after setup-build-env.' }
if (-not $NinjaExe) { throw 'build-vulkan: VRCLIENT_NINJA_EXE not set after setup-build-env.' }
Write-Host "cmake   : $CMakeExe"
Write-Host "ninja   : $NinjaExe"
Write-Host "compiler: $compilerResolved (requested: $Compiler)"

# Probe CMake version for the >= 3.29 CMAKE_LINKER_TYPE native LLD selection.
$cmakeVersionString = 'unknown'
$cmakeSupportsLinkerType = $false
$cv = Invoke-Native -Exe $CMakeExe -Arguments @('--version')
$cvText = ($cv.Output -join "`n")
if ($cvText -match 'cmake version (\d+)\.(\d+)\.(\d+)') {
    $cmakeVersionString = "$($matches[1]).$($matches[2]).$($matches[3])"
    $cmv = New-Object System.Version([int]$matches[1], [int]$matches[2], [int]$matches[3])
    $cmakeSupportsLinkerType = ($cmv -ge (New-Object System.Version(3, 29, 0)))
}

# Forward-slashed prefix - CMake-friendly, reused throughout.
$prefixFwd = $Prefix.Replace('\', '/')

# Shared configure flags every component gets: generator, Release, ninja program, install prefix,
# and (on clang-cl) the exact compiler/linker selection build-and-test.ps1 / build-openxr.ps1 use.
function Get-BaseConfigureFlags {
    param([string]$SrcDir, [string]$BuildDir)
    $flags = @(
        '-S', $SrcDir,
        '-B', $BuildDir,
        '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_MAKE_PROGRAM=$($NinjaExe.Replace('\','/'))",
        "-DCMAKE_INSTALL_PREFIX=$prefixFwd"
    )
    if ($compilerResolved -eq 'clang-cl' -and $ClangClExe) {
        $cc = $ClangClExe.Replace('\', '/')
        $flags += @(
            "-DCMAKE_C_COMPILER=$cc",
            "-DCMAKE_CXX_COMPILER=$cc",
            '-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld-link',
            '-DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld-link',
            '-DCMAKE_MODULE_LINKER_FLAGS=-fuse-ld=lld-link'
        )
        if ($cmakeSupportsLinkerType) { $flags += '-DCMAKE_LINKER_TYPE=LLD' }
    }
    return $flags
}

# configure -> build (optional --target) -> install for one component.
# $Parallel caps build parallelism (2 for essentials, 1 for the heavy VVL stack).
# Returns @{ Ok=$bool; Phase='configure'|'build'|'install'|'ok'; Output=string[] }.
function Build-Component {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$SrcDir,
        [Parameter(Mandatory = $true)][string]$BuildDir,
        [string[]]$ExtraConfigure = @(),
        [string]$Target = '',
        [int]$Parallel = 2
    )
    if (Test-Path -LiteralPath $BuildDir) {
        Write-Host "Removing stale build tree: $BuildDir"
        Remove-Item -LiteralPath $BuildDir -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

    $env:CMAKE_BUILD_PARALLEL_LEVEL = "$Parallel"

    $configureFlags = (Get-BaseConfigureFlags -SrcDir $SrcDir -BuildDir $BuildDir) + $ExtraConfigure
    Write-Host "--- $Name : configure ---"
    $r = Invoke-Native -Exe $CMakeExe -Arguments $configureFlags
    $r.Output | ForEach-Object { Write-Host "  $_" }
    if ($r.ExitCode -ne 0) { return @{ Ok = $false; Phase = 'configure'; Output = $r.Output } }

    $buildArgs = @('--build', $BuildDir, '--config', 'Release', '--parallel', "$Parallel")
    if ($Target) { $buildArgs += @('--target', $Target) }
    Write-Host "--- $Name : build (parallel $Parallel$(if($Target){" target $Target"})) ---"
    $r = Invoke-Native -Exe $CMakeExe -Arguments $buildArgs
    $r.Output | ForEach-Object { Write-Host "  $_" }
    if ($r.ExitCode -ne 0) { return @{ Ok = $false; Phase = 'build'; Output = $r.Output } }

    Write-Host "--- $Name : install ---"
    $r = Invoke-Native -Exe $CMakeExe -Arguments @('--install', $BuildDir, '--config', 'Release')
    $r.Output | ForEach-Object { Write-Host "  $_" }
    if ($r.ExitCode -ne 0) { return @{ Ok = $false; Phase = 'install'; Output = $r.Output } }

    return @{ Ok = $true; Phase = 'ok'; Output = $r.Output }
}

# Locate the validation-layer JSON manifest anywhere under the prefix (it lands at
# bin/ or share/vulkan/explicit_layer.d/ depending on platform/version).
function Find-ValidationManifest {
    param([string]$Root)
    if (-not (Test-Path -LiteralPath $Root)) { return $null }
    $hit = Get-ChildItem -LiteralPath $Root -Recurse -Filter 'VkLayer_khronos_validation.json' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($hit) { return $hit.FullName }
    return $null
}

# Locate the loader import lib (vulkan-1.lib) anywhere under the prefix.
function Find-LoaderLib {
    param([string]$Root)
    if (-not (Test-Path -LiteralPath $Root)) { return $null }
    foreach ($name in @('vulkan-1.lib', 'vulkan.lib')) {
        $hit = Get-ChildItem -LiteralPath $Root -Recurse -Filter $name -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    return $null
}

# Locate the glslang front-end binary (named 'glslang(.exe)' at this tag; historically glslangValidator).
function Find-GlslangBinary {
    param([string]$Root)
    if (-not (Test-Path -LiteralPath $Root)) { return $null }
    foreach ($name in @('glslang.exe', 'glslangValidator.exe')) {
        $hit = Get-ChildItem -LiteralPath $Root -Recurse -Filter $name -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    return $null
}

# ---------------------------------------------------------------------------
# Step 2: idempotency
# ---------------------------------------------------------------------------
Write-Step "Step 2/6: idempotency check (prefix: $Prefix)"
$existingLoader = Find-LoaderLib -Root $Prefix
$existingLayer = Find-ValidationManifest -Root $Prefix
$prefixComplete = $false
if ($existingLoader) {
    if ($SkipValidationLayers) { $prefixComplete = $true }
    elseif ($existingLayer) { $prefixComplete = $true }
}
if ($prefixComplete -and -not $Force) {
    Write-Host "Vulkan stack already installed; skipping (pass -Force to rebuild)."
    Write-Host "loader import lib : $existingLoader"
    $glslangBin = Find-GlslangBinary -Root $Prefix
    if ($glslangBin) { Write-Host "glslang binary   : $glslangBin" }
    if ($existingLayer) { Write-Host "validation layer : $existingLayer" }
    elseif ($SkipValidationLayers) { Write-Host "validation layer : (skipped via -SkipValidationLayers)" }
    Write-Host "Install prefix   : $Prefix"
    exit 0
}
if ($existingLoader -and -not $prefixComplete -and -not $Force) {
    Write-Host "Loader present but validation layer missing; continuing to (re)attempt the layer stack."
}
if ($prefixComplete -and $Force) {
    Write-Host "-Force given; rebuilding even though the stack appears installed at: $Prefix"
}

# ---------------------------------------------------------------------------
# Step 3: locate git (clone is the only network step)
# ---------------------------------------------------------------------------
Write-Step "Step 3/6: locate git + plan pinned clones @ $Ref"
$gitCmd = Get-Command git.exe -ErrorAction SilentlyContinue
if (-not $gitCmd) { throw 'build-vulkan: git.exe not found on PATH (required for the source clones).' }
$gitExe = $gitCmd.Source

# Per-repo src/build dirs.
$dirs = @{
    'Vulkan-Headers'           = @{ Src = (Join-Path $SrcRoot 'Vulkan-Headers');           Build = (Join-Path $BuildRoot 'Vulkan-Headers') }
    'Vulkan-Loader'            = @{ Src = (Join-Path $SrcRoot 'Vulkan-Loader');            Build = (Join-Path $BuildRoot 'Vulkan-Loader') }
    'glslang'                  = @{ Src = (Join-Path $SrcRoot 'glslang');                  Build = (Join-Path $BuildRoot 'glslang') }
    'SPIRV-Headers'            = @{ Src = (Join-Path $SrcRoot 'SPIRV-Headers');            Build = (Join-Path $BuildRoot 'SPIRV-Headers') }
    'SPIRV-Tools'              = @{ Src = (Join-Path $SrcRoot 'SPIRV-Tools');              Build = (Join-Path $BuildRoot 'SPIRV-Tools') }
    'Vulkan-Utility-Libraries' = @{ Src = (Join-Path $SrcRoot 'Vulkan-Utility-Libraries'); Build = (Join-Path $BuildRoot 'Vulkan-Utility-Libraries') }
    'Vulkan-ValidationLayers'  = @{ Src = (Join-Path $SrcRoot 'Vulkan-ValidationLayers');  Build = (Join-Path $BuildRoot 'Vulkan-ValidationLayers') }
}
$commits = @{}

# ---------------------------------------------------------------------------
# Step 4: ESSENTIALS (Headers -> Loader -> glslang). Any failure here is FATAL.
# ---------------------------------------------------------------------------
Write-Step "Step 4/6: build essentials (Vulkan-Headers -> Vulkan-Loader -> glslang) [parallel 2]"

# (a) Vulkan-Headers - header-only; install only.
$commits['Vulkan-Headers'] = Invoke-PinnedClone -GitExe $gitExe -Repo 'Vulkan-Headers' -Dest $dirs['Vulkan-Headers'].Src
$res = Build-Component -Name 'Vulkan-Headers' -SrcDir $dirs['Vulkan-Headers'].Src -BuildDir $dirs['Vulkan-Headers'].Build -Parallel 2
if (-not $res.Ok) { throw "build-vulkan: ESSENTIAL Vulkan-Headers failed at $($res.Phase). Cannot continue." }

# (b) Vulkan-Loader - needs the headers; point it at our just-installed prefix. Build the lot + install.
$commits['Vulkan-Loader'] = Invoke-PinnedClone -GitExe $gitExe -Repo 'Vulkan-Loader' -Dest $dirs['Vulkan-Loader'].Src
$loaderExtra = @(
    "-DVULKAN_HEADERS_INSTALL_DIR=$prefixFwd",
    "-DCMAKE_PREFIX_PATH=$prefixFwd",
    '-DBUILD_TESTS=OFF',
    '-DUPDATE_DEPS=OFF',
    '-DUSE_GAS=OFF'
)
$res = Build-Component -Name 'Vulkan-Loader' -SrcDir $dirs['Vulkan-Loader'].Src -BuildDir $dirs['Vulkan-Loader'].Build -ExtraConfigure $loaderExtra -Parallel 2
if (-not $res.Ok) { throw "build-vulkan: ESSENTIAL Vulkan-Loader failed at $($res.Phase). Cannot continue." }

# (c) glslang - shader compiler. ENABLE_OPT=OFF drops the SPIRV-Tools build dependency so the
#     glslang front-end (GLSL/HLSL -> SPIR-V) + libs install without the heavy optimizer; the
#     standalone 'glslang' binary replaces the old glslangValidator. Build everything + install.
$commits['glslang'] = Invoke-PinnedClone -GitExe $gitExe -Repo 'glslang' -Dest $dirs['glslang'].Src
$glslangExtra = @(
    '-DENABLE_OPT=OFF',
    '-DENABLE_GLSLANG_BINARIES=ON',
    '-DGLSLANG_TESTS=OFF',
    '-DGLSLANG_ENABLE_INSTALL=ON',
    '-DBUILD_TESTING=OFF',
    '-DENABLE_CTEST=OFF'
)
$res = Build-Component -Name 'glslang' -SrcDir $dirs['glslang'].Src -BuildDir $dirs['glslang'].Build -ExtraConfigure $glslangExtra -Parallel 2
if (-not $res.Ok) { throw "build-vulkan: ESSENTIAL glslang failed at $($res.Phase). Cannot continue." }

# Verify essentials are physically present.
$loaderLib = Find-LoaderLib -Root $Prefix
$glslangBin = Find-GlslangBinary -Root $Prefix
$headersHdr = Get-ChildItem -LiteralPath $Prefix -Recurse -Filter 'vulkan_core.h' -ErrorAction SilentlyContinue | Select-Object -First 1
$essentialsInstalled = [bool]($loaderLib -and $glslangBin -and $headersHdr)
if (-not $essentialsInstalled) {
    $missing = @()
    if (-not $headersHdr) { $missing += 'Vulkan-Headers (vulkan_core.h)' }
    if (-not $loaderLib) { $missing += 'Vulkan-Loader (vulkan-1.lib)' }
    if (-not $glslangBin) { $missing += 'glslang binary' }
    throw "build-vulkan: essentials install verification failed - missing: $($missing -join ', ')."
}
$loaderDll = Get-ChildItem -LiteralPath $Prefix -Recurse -Filter 'vulkan-1.dll' -ErrorAction SilentlyContinue | Select-Object -First 1
Write-Host ''
Write-Host '--- essentials verified ---'
Write-Host "headers : $($headersHdr.FullName)"
Write-Host "loader  : $($loaderLib)$(if($loaderDll){" + $($loaderDll.FullName)"})"
Write-Host "glslang : $glslangBin"

# ---------------------------------------------------------------------------
# Step 5: VALIDATION LAYERS (graceful). SPIRV-Headers -> SPIRV-Tools -> VVL, parallelism 1.
# A failure here is NON-FATAL: essentials already shipped; layers reported DEFERRED.
# ---------------------------------------------------------------------------
$validationLayersBuilt = $false
$validationManifest = $null
$validationDeferredReason = $null

if ($SkipValidationLayers) {
    Write-Step 'Step 5/6: validation layers SKIPPED (-SkipValidationLayers)'
    $validationDeferredReason = 'skipped via -SkipValidationLayers'
}
else {
    Write-Step 'Step 5/6: attempt validation-layer stack (SPIRV-Headers -> SPIRV-Tools -> Vulkan-Utility-Libraries -> Vulkan-ValidationLayers) [parallel 1]'
    try {
        # (a) SPIRV-Headers - header-only; install. (Light; failure here still degrades gracefully.)
        $commits['SPIRV-Headers'] = Invoke-PinnedClone -GitExe $gitExe -Repo 'SPIRV-Headers' -Dest $dirs['SPIRV-Headers'].Src
        $res = Build-Component -Name 'SPIRV-Headers' -SrcDir $dirs['SPIRV-Headers'].Src -BuildDir $dirs['SPIRV-Headers'].Build -Parallel 1
        if (-not $res.Ok) {
            $validationDeferredReason = "SPIRV-Headers failed at $($res.Phase)" + $(if (Test-LooksLikeOOM -Output $res.Output) { ' (OOM/heap)' } else { '' })
            throw 'vvl-stack-abort'
        }

        # (b) SPIRV-Tools - needs SPIRV-Headers; point it at our prefix. Parallelism 1 (RAM).
        $commits['SPIRV-Tools'] = Invoke-PinnedClone -GitExe $gitExe -Repo 'SPIRV-Tools' -Dest $dirs['SPIRV-Tools'].Src
        $spirvToolsExtra = @(
            "-DSPIRV-Headers_SOURCE_DIR=$($dirs['SPIRV-Headers'].Src.Replace('\','/'))",
            "-DCMAKE_PREFIX_PATH=$prefixFwd",
            '-DSPIRV_SKIP_TESTS=ON',
            '-DSPIRV_SKIP_EXECUTABLES=OFF',
            '-DSPIRV_WERROR=OFF',
            '-DSPIRV_TOOLS_BUILD_STATIC=ON'
        )
        $res = Build-Component -Name 'SPIRV-Tools' -SrcDir $dirs['SPIRV-Tools'].Src -BuildDir $dirs['SPIRV-Tools'].Build -ExtraConfigure $spirvToolsExtra -Parallel 1
        if (-not $res.Ok) {
            $validationDeferredReason = "SPIRV-Tools failed at $($res.Phase)" + $(if (Test-LooksLikeOOM -Output $res.Output) { ' (OOM/heap)' } else { '' })
            throw 'vvl-stack-abort'
        }

        # (c) Vulkan-Utility-Libraries - provides the Vulkan::LayerSettings + Vulkan::UtilityHeaders
        #     IMPORTED targets that VVL's VkLayer_utils links against (VVL fails to configure without
        #     them). Header/util libraries; needs the installed Vulkan-Headers. Install to the prefix.
        $commits['Vulkan-Utility-Libraries'] = Invoke-PinnedClone -GitExe $gitExe -Repo 'Vulkan-Utility-Libraries' -Dest $dirs['Vulkan-Utility-Libraries'].Src
        $vulExtra = @(
            "-DVULKAN_HEADERS_INSTALL_DIR=$prefixFwd",
            "-DCMAKE_PREFIX_PATH=$prefixFwd",
            '-DBUILD_TESTS=OFF',
            '-DUPDATE_DEPS=OFF'
        )
        $res = Build-Component -Name 'Vulkan-Utility-Libraries' -SrcDir $dirs['Vulkan-Utility-Libraries'].Src -BuildDir $dirs['Vulkan-Utility-Libraries'].Build -ExtraConfigure $vulExtra -Parallel 1
        if (-not $res.Ok) {
            $validationDeferredReason = "Vulkan-Utility-Libraries failed at $($res.Phase)" + $(if (Test-LooksLikeOOM -Output $res.Output) { ' (OOM/heap)' } else { '' })
            throw 'vvl-stack-abort'
        }

        # (d) Vulkan-ValidationLayers - THE HEAVY ONE. Feed it our prebuilt install dirs so it
        #     never re-downloads deps (UPDATE_DEPS=OFF). Parallelism 1; retry once on OOM at 1
        #     (already 1, so the retry is a clean-rebuild attempt to clear transient memory).
        $commits['Vulkan-ValidationLayers'] = Invoke-PinnedClone -GitExe $gitExe -Repo 'Vulkan-ValidationLayers' -Dest $dirs['Vulkan-ValidationLayers'].Src
        $vvlExtra = @(
            '-DUPDATE_DEPS=OFF',
            "-DCMAKE_PREFIX_PATH=$prefixFwd",
            "-DVULKAN_HEADERS_INSTALL_DIR=$prefixFwd",
            "-DSPIRV_HEADERS_INSTALL_DIR=$prefixFwd",
            "-DSPIRV_TOOLS_INSTALL_DIR=$prefixFwd",
            "-DGLSLANG_INSTALL_DIR=$prefixFwd",
            "-DVULKAN_UTILITY_LIBRARIES_INSTALL_DIR=$prefixFwd",
            '-DBUILD_TESTS=OFF',
            '-DBUILD_WERROR=OFF'
        )
        $res = Build-Component -Name 'Vulkan-ValidationLayers' -SrcDir $dirs['Vulkan-ValidationLayers'].Src -BuildDir $dirs['Vulkan-ValidationLayers'].Build -ExtraConfigure $vvlExtra -Parallel 1
        if (-not $res.Ok -and ($res.Phase -eq 'build') -and (Test-LooksLikeOOM -Output $res.Output)) {
            Write-Host 'Vulkan-ValidationLayers build looked OOM at parallel 1; one clean-rebuild retry...'
            $res = Build-Component -Name 'Vulkan-ValidationLayers' -SrcDir $dirs['Vulkan-ValidationLayers'].Src -BuildDir $dirs['Vulkan-ValidationLayers'].Build -ExtraConfigure $vvlExtra -Parallel 1
        }
        if (-not $res.Ok) {
            $validationDeferredReason = "Vulkan-ValidationLayers failed at $($res.Phase)" + $(if (Test-LooksLikeOOM -Output $res.Output) { ' (OOM/heap)' } else { '' })
            throw 'vvl-stack-abort'
        }

        $validationManifest = Find-ValidationManifest -Root $Prefix
        if ($validationManifest) {
            $validationLayersBuilt = $true
            Write-Host ''
            Write-Host "--- validation layer verified ---"
            Write-Host "manifest : $validationManifest"
        }
        else {
            $validationDeferredReason = 'VVL install completed but VkLayer_khronos_validation.json manifest not found under prefix'
        }
    }
    catch {
        if ($_.Exception.Message -ne 'vvl-stack-abort') {
            # An unexpected error in the layer stack - still degrade gracefully, do not kill the run.
            $validationDeferredReason = "unexpected error in layer stack: $($_.Exception.Message)"
        }
        Write-Host ''
        Write-Host "NOTE: validation-layer stack DEFERRED (non-fatal): $validationDeferredReason"
        Write-Host '      Essentials are intact; re-run later (or with -Force) to retry the layers.'
    }
}

# ---------------------------------------------------------------------------
# Step 6: report + clean disposable intermediates
# ---------------------------------------------------------------------------
Write-Step 'Step 6/6: report'
Write-Host ''
Write-Host '=== Vulkan SDK bootstrap (open-source, Apache-2.0/MIT, built from source) ==='
Write-Host "Install prefix          : $Prefix"
Write-Host "Pinned tag (lockstep)   : $Ref"
Write-Host "Compiler                : $compilerResolved (cmake $cmakeVersionString)"
Write-Host ''
Write-Host "essentialsInstalled     : $essentialsInstalled"
Write-Host "  Vulkan-Headers        : $($commits['Vulkan-Headers'])  -> $($headersHdr.FullName)"
Write-Host "  Vulkan-Loader         : $($commits['Vulkan-Loader'])  -> $loaderLib"
if ($loaderDll) { Write-Host "                          $($loaderDll.FullName)" }
Write-Host "  glslang               : $($commits['glslang'])  -> $glslangBin"
Write-Host ''
Write-Host "validationLayersBuilt   : $validationLayersBuilt"
if ($validationLayersBuilt) {
    Write-Host "  SPIRV-Headers         : $($commits['SPIRV-Headers'])"
    Write-Host "  SPIRV-Tools           : $($commits['SPIRV-Tools'])"
    Write-Host "  Vulkan-Utility-Libraries: $($commits['Vulkan-Utility-Libraries'])"
    Write-Host "  Vulkan-ValidationLayers: $($commits['Vulkan-ValidationLayers'])"
    Write-Host "  layer manifest        : $validationManifest"
    Write-Host "  (DEV-ONLY: runtime-loaded via VK_LAYER_PATH; not linked/shipped, not in default build)"
}
else {
    Write-Host "  DEFERRED reason       : $validationDeferredReason"
    Write-Host "  (essentials suffice for the renderer port; add layers later by re-running)"
}

# Clean disposable build trees + shallow src clones (install prefix is always kept).
if ($KeepIntermediate) {
    Write-Host ''
    Write-Host "Intermediates kept (-KeepIntermediate): $BuildRoot ; $SrcRoot"
}
else {
    Write-Host ''
    foreach ($tree in @($BuildRoot, $SrcRoot)) {
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
    Write-Host '(intermediates cleaned; pass -KeepIntermediate to keep external/_vulkan-build + external/_vulkan-src for debugging)'
}

# essentialsInstalled gates exit success; deferred layers never fail the run.
if (-not $essentialsInstalled) { exit 1 }
exit 0

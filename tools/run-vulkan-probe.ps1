#Requires -Version 5.1
<#
.SYNOPSIS
    Configure + build the headless Vulkan probe (vrclient_vulkan_probe) against the
    from-source SDK bootstrapped into external/vulkan, then RUN it to prove the SDK is
    usable: VkInstance -> enumerate physical devices -> VkDevice(one queue) -> teardown.
    If the validation layer was built, also run a VALIDATION pass and assert ZERO
    validation errors/warnings across the clean lifecycle.

.DESCRIPTION
    Renderer-port PREREQUISITE verification (NOT the product, NOT the default build, NOT
    the CTest 40/40 suite). The probe is a NEW additive target guarded behind the
    OFF-by-default CMake option VRCLIENT_BUILD_VULKAN_PROBE, so the default OpenXR-OFF
    build is untouched; this driver opts it IN, in its OWN build dir (default build/vk-probe),
    never the default CI cache.

    Mirrors the proven structure of tools/build-vulkan.ps1 / scripts/ci/build-and-test.ps1:
    dot-source tools/setup-build-env.ps1 for the pinned toolchain (CMake/Ninja, clang-cl
    when resolved), Ninja generator, Invoke-Native with merged stderr.

    Runs in two passes:
      1. ESSENTIALS pass (no layer): vrclient_vulkan_probe with no flags. Proves the
         bootstrapped loader + headers create an instance, enumerate devices, and create a
         device. A machine with no Vulkan ICD is reported as an ENVIRONMENT note (still
         exits 0 once the instance lifecycle succeeds) unless -RequireDevice is given.
      2. VALIDATION pass (only if external/vulkan ships VkLayer_khronos_validation.json):
         sets VK_LAYER_PATH to the layer dir (the manifest lands in external/vulkan/bin at
         this SDK tag), DISABLES third-party implicit layers (VK_LOADER_LAYERS_DISABLE=*)
         so only OUR explicit VK_LAYER_KHRONOS_validation participates, runs the probe with
         --validate, and asserts exit 0 (lifecycle clean AND zero validation messages).
         The layer is DEV-ONLY and runtime-loaded here via VK_LAYER_PATH; it is never linked
         or shipped.

.PARAMETER BuildDir
    CMake binary dir for the probe (relative to repo root or absolute). Default: build/vk-probe.
    Kept SEPARATE from the default CI cache (build/ci) so configuring the probe never
    perturbs the default build's CMakeCache.

.PARAMETER VulkanPrefix
    The bootstrapped SDK prefix. Default: <repo>/external/vulkan.

.PARAMETER Compiler
    msvc | clang-cl | auto (default auto). Passed to setup-build-env.ps1.

.PARAMETER RequireDevice
    Treat "no physical device / ICD" as a failure (passes --require-device to the probe).
    Off by default so a headless/no-GPU host still passes the loader+headers proof.

.NOTES
    [PowerShell] powershell -NoProfile -ExecutionPolicy Bypass -File tools\run-vulkan-probe.ps1
    [PowerShell] tools\run-vulkan-probe.ps1 -RequireDevice
#>
[CmdletBinding()]
param(
    [string]$BuildDir = 'build/vk-probe',
    [string]$VulkanPrefix = '',
    [ValidateSet('msvc', 'clang-cl', 'auto')][string]$Compiler = 'auto',
    [switch]$RequireDevice
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

if ([string]::IsNullOrWhiteSpace($VulkanPrefix)) {
    $VulkanPrefix = Join-Path $RepoRoot 'external\vulkan'
}
elseif (-not [System.IO.Path]::IsPathRooted($VulkanPrefix)) {
    $VulkanPrefix = Join-Path $RepoRoot $VulkanPrefix
}
if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDirFull = $BuildDir }
else { $BuildDirFull = Join-Path $RepoRoot $BuildDir }

function Write-Step { param([string]$Message) Write-Host "`n=== $Message ===" }

# Runs a native executable with stderr merged; returns @{ ExitCode; Output(string[]) }.
# Mirrors build-vulkan.ps1 / build-and-test.ps1.
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
# Step 1: toolchain
# ---------------------------------------------------------------------------
Write-Step "Step 1/5: import build environment (tools/setup-build-env.ps1) [compiler: $Compiler]"
. (Join-Path $RepoRoot 'tools\setup-build-env.ps1') -Compiler $Compiler
$CMakeExe = $env:VRCLIENT_CMAKE_EXE
$NinjaExe = $env:VRCLIENT_NINJA_EXE
$compilerResolved = $env:VRCLIENT_COMPILER
if (-not $compilerResolved) { $compilerResolved = 'msvc' }
$ClangClExe = $env:VRCLIENT_CLANG_CL_EXE
if (-not $CMakeExe) { throw 'run-vulkan-probe: VRCLIENT_CMAKE_EXE not set after setup-build-env.' }
if (-not $NinjaExe) { throw 'run-vulkan-probe: VRCLIENT_NINJA_EXE not set after setup-build-env.' }
Write-Host "cmake   : $CMakeExe"
Write-Host "ninja   : $NinjaExe"
Write-Host "compiler: $compilerResolved"
Write-Host "SDK     : $VulkanPrefix"

# Probe CMake version for the >= 3.29 CMAKE_LINKER_TYPE native LLD selection (clang-cl).
$cmakeSupportsLinkerType = $false
$cv = Invoke-Native -Exe $CMakeExe -Arguments @('--version')
if (($cv.Output -join "`n") -match 'cmake version (\d+)\.(\d+)\.(\d+)') {
    $cmv = New-Object System.Version([int]$matches[1], [int]$matches[2], [int]$matches[3])
    $cmakeSupportsLinkerType = ($cmv -ge (New-Object System.Version(3, 29, 0)))
}

# Sanity: the bootstrapped loader must exist (else build-vulkan.ps1 has not run).
$loaderLib = Get-ChildItem -LiteralPath $VulkanPrefix -Recurse -Filter 'vulkan-1.lib' -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $loaderLib) {
    throw "run-vulkan-probe: no vulkan-1.lib under $VulkanPrefix. Run tools/build-vulkan.ps1 first."
}

# Detect the validation layer manifest (DEV-ONLY, runtime-loaded). If absent, the
# validation pass is reported DEFERRED and only the essentials pass runs.
$layerManifest = Get-ChildItem -LiteralPath $VulkanPrefix -Recurse -Filter 'VkLayer_khronos_validation.json' -ErrorAction SilentlyContinue | Select-Object -First 1
$layerDir = $null
if ($layerManifest) { $layerDir = Split-Path -Parent $layerManifest.FullName }

# ---------------------------------------------------------------------------
# Step 2: configure (probe ON, separate build dir, SDK on CMAKE_PREFIX_PATH)
# ---------------------------------------------------------------------------
Write-Step "Step 2/5: CMake configure (VRCLIENT_BUILD_VULKAN_PROBE=ON) -> $BuildDirFull"
$prefixFwd = $VulkanPrefix.Replace('\', '/')
$configureFlags = @(
    '-S', $RepoRoot,
    '-B', $BuildDirFull,
    '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_MAKE_PROGRAM=$($NinjaExe.Replace('\','/'))",
    '-DVRCLIENT_BUILD_VULKAN_PROBE=ON',
    '-DVRCLIENT_BUILD_OPENXR_RUNTIME=OFF',
    '-DVRCLIENT_BUILD_TESTS=OFF',
    "-DCMAKE_PREFIX_PATH=$prefixFwd",
    "-DVRCLIENT_VULKAN_PREFIX=$prefixFwd"
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
if ($r.ExitCode -ne 0) { throw "run-vulkan-probe: configure failed (exit $($r.ExitCode))." }

# ---------------------------------------------------------------------------
# Step 3: build the probe target only
# ---------------------------------------------------------------------------
Write-Step 'Step 3/5: build vrclient_vulkan_probe'
$r = Invoke-Native -Exe $CMakeExe -Arguments @('--build', $BuildDirFull, '--config', 'Release', '--target', 'vrclient_vulkan_probe', '--parallel', '2')
$r.Output | ForEach-Object { Write-Host "  $_" }
if ($r.ExitCode -ne 0) { throw "run-vulkan-probe: build failed (exit $($r.ExitCode))." }

$probeExe = Get-ChildItem -LiteralPath $BuildDirFull -Recurse -Filter 'vrclient_vulkan_probe.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $probeExe) { throw "run-vulkan-probe: built probe exe not found under $BuildDirFull." }
Write-Host "probe exe: $($probeExe.FullName)"

# ---------------------------------------------------------------------------
# Step 4: ESSENTIALS pass (no validation layer)
# ---------------------------------------------------------------------------
Write-Step 'Step 4/5: run probe (essentials pass: instance + enumerate + device)'
$essentialArgs = @()
if ($RequireDevice) { $essentialArgs += '--require-device' }
$r = Invoke-Native -Exe $probeExe.FullName -Arguments $essentialArgs -WorkDir (Split-Path -Parent $probeExe.FullName)
$r.Output | ForEach-Object { Write-Host "  $_" }
$essentialsExit = $r.ExitCode
$essentialsOk = ($essentialsExit -eq 0)
$deviceFound = [bool](($r.Output -join "`n") -match 'physical device count = ([1-9]\d*)')
Write-Host "essentials pass exit=$essentialsExit (deviceFound=$deviceFound)"
if (-not $essentialsOk) { throw "run-vulkan-probe: ESSENTIALS pass failed (exit $essentialsExit). The bootstrapped loader/headers are not usable." }

# ---------------------------------------------------------------------------
# Step 5: VALIDATION pass (only if the layer was built)
# ---------------------------------------------------------------------------
$validationStatus = 'deferred'
$validationDetail = ''
if (-not $layerDir) {
    Write-Step 'Step 5/5: validation pass DEFERRED (no VkLayer_khronos_validation.json under prefix)'
    $validationDetail = "validation layer not installed under $VulkanPrefix; ran essentials only"
}
elseif (-not $deviceFound) {
    Write-Step 'Step 5/5: validation pass SKIPPED (no physical device on this host)'
    $validationStatus = 'skipped-no-device'
    $validationDetail = 'no Vulkan ICD/device present; cannot exercise device-create validation'
}
else {
    Write-Step "Step 5/5: run probe (validation pass: VK_LAYER_KHRONOS_validation, assert ZERO errors)"
    Write-Host "layer dir (VK_LAYER_PATH): $layerDir"
    # Save + set environment for THIS pass only, then restore.
    $savedLayerPath = $env:VK_LAYER_PATH
    try {
        # Point the loader at OUR explicit-layer dir so VK_LAYER_KHRONOS_validation is
        # discoverable (the manifest's library_path is relative, so the dir containing
        # the .json + .dll is the right value). The probe explicitly requests ONLY that
        # layer; the host's third-party IMPLICIT layers (OBS_HOOK, Steam, EOS, NV) stay
        # passive and do not emit VALIDATION-typed messages for a clean lifecycle. We do
        # NOT set VK_LOADER_LAYERS_DISABLE: forcing the disable filter makes the LOADER
        # itself inject GENERAL-typed "layer forced disabled" warnings through the debug
        # callback, which are loader chatter, not validation findings. The probe already
        # discounts non-VALIDATION-typed messages, so the assertion reflects OUR layer's
        # verdict on the instance/device lifecycle alone.
        $env:VK_LAYER_PATH = $layerDir
        $r = Invoke-Native -Exe $probeExe.FullName -Arguments @('--validate') -WorkDir (Split-Path -Parent $probeExe.FullName)
    }
    finally {
        $env:VK_LAYER_PATH = $savedLayerPath
    }
    $r.Output | ForEach-Object { Write-Host "  $_" }
    $validationExit = $r.ExitCode
    if ($validationExit -eq 0) {
        $validationStatus = 'clean'
        $validationDetail = 'VK_LAYER_KHRONOS_validation active; ZERO validation errors/warnings across instance+device lifecycle'
    }
    elseif ($validationExit -eq 3) {
        $validationStatus = 'layer-unavailable'
        $validationDetail = 'probe could not see the layer even with VK_LAYER_PATH set (exit 3)'
        throw "run-vulkan-probe: validation pass could not load the layer (exit 3). VK_LAYER_PATH=$layerDir"
    }
    elseif ($validationExit -eq 4) {
        $validationStatus = 'errors'
        $validationDetail = 'validation layer reported error(s)/warning(s) during a clean lifecycle (exit 4)'
        throw "run-vulkan-probe: validation pass produced validation messages (exit 4) - see output above."
    }
    else {
        $validationStatus = 'failed'
        $validationDetail = "validation pass exited $validationExit"
        throw "run-vulkan-probe: validation pass failed (exit $validationExit)."
    }
}

# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------
Write-Step 'RESULT'
Write-Host "SDK prefix          : $VulkanPrefix"
Write-Host "probe exe           : $($probeExe.FullName)"
Write-Host "essentials pass     : OK (exit 0; device found = $deviceFound)"
Write-Host "validation pass     : $validationStatus"
Write-Host "  detail            : $validationDetail"
if ($layerManifest) { Write-Host "layer manifest      : $($layerManifest.FullName) (DEV-ONLY, runtime-loaded via VK_LAYER_PATH; not linked/shipped)" }
exit 0

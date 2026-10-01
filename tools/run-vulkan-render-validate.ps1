#Requires -Version 5.1
<#
.SYNOPSIS
    Configure + build the headless Vulkan RENDER-PATH validation harness
    (vrclient_vulkan_render_validate) against the from-source SDK in external/vulkan,
    then RUN it to prove the ported Vulkan test-scene renderer (VulkanTestSceneRenderer:
    initialize -> prepareEyeTargets -> renderEye) is correct over an OFFSCREEN VkImage
    with NO XrSession/headset. If the validation layer is present, run a VALIDATION pass
    and assert ZERO validation findings (HARD CONSTRAINT #4).

.DESCRIPTION
    Renderer-port verification (NOT the product, NOT the default build, NOT the CTest
    40/40 suite). The target is additive, guarded behind the OFF-by-default CMake option
    VRCLIENT_BUILD_VULKAN_PROBE, in its OWN build dir (default build/vk-render-validate),
    never the default CI cache. Mirrors tools/run-vulkan-probe.ps1.

.PARAMETER BuildDir   CMake binary dir. Default: build/vk-render-validate.
.PARAMETER VulkanPrefix  Bootstrapped SDK prefix. Default: <repo>/external/vulkan.
.PARAMETER Compiler   msvc | clang-cl | auto (default auto).
.PARAMETER RequireDevice  Treat "no ICD/device" as a failure.

.NOTES
    [PowerShell] powershell -NoProfile -ExecutionPolicy Bypass -File tools\run-vulkan-render-validate.ps1
#>
[CmdletBinding()]
param(
    [string]$BuildDir = 'build/vk-render-validate',
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

Write-Step "Step 1/5: import build environment (tools/setup-build-env.ps1) [compiler: $Compiler]"
. (Join-Path $RepoRoot 'tools\setup-build-env.ps1') -Compiler $Compiler
$CMakeExe = $env:VRCLIENT_CMAKE_EXE
$NinjaExe = $env:VRCLIENT_NINJA_EXE
$compilerResolved = $env:VRCLIENT_COMPILER
if (-not $compilerResolved) { $compilerResolved = 'msvc' }
$ClangClExe = $env:VRCLIENT_CLANG_CL_EXE
if (-not $CMakeExe) { throw 'render-validate: VRCLIENT_CMAKE_EXE not set after setup-build-env.' }
if (-not $NinjaExe) { throw 'render-validate: VRCLIENT_NINJA_EXE not set after setup-build-env.' }
Write-Host "cmake   : $CMakeExe"
Write-Host "compiler: $compilerResolved"
Write-Host "SDK     : $VulkanPrefix"

$cmakeSupportsLinkerType = $false
$cv = Invoke-Native -Exe $CMakeExe -Arguments @('--version')
if (($cv.Output -join "`n") -match 'cmake version (\d+)\.(\d+)\.(\d+)') {
    $cmv = New-Object System.Version([int]$matches[1], [int]$matches[2], [int]$matches[3])
    $cmakeSupportsLinkerType = ($cmv -ge (New-Object System.Version(3, 29, 0)))
}

$loaderLib = Get-ChildItem -LiteralPath $VulkanPrefix -Recurse -Filter 'vulkan-1.lib' -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $loaderLib) { throw "render-validate: no vulkan-1.lib under $VulkanPrefix. Run tools/build-vulkan.ps1 first." }

$layerManifest = Get-ChildItem -LiteralPath $VulkanPrefix -Recurse -Filter 'VkLayer_khronos_validation.json' -ErrorAction SilentlyContinue | Select-Object -First 1
$layerDir = $null
if ($layerManifest) { $layerDir = Split-Path -Parent $layerManifest.FullName }

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
if ($r.ExitCode -ne 0) { throw "render-validate: configure failed (exit $($r.ExitCode))." }

Write-Step 'Step 3/5: build vrclient_vulkan_render_validate'
$r = Invoke-Native -Exe $CMakeExe -Arguments @('--build', $BuildDirFull, '--config', 'Release', '--target', 'vrclient_vulkan_render_validate', '--parallel', '2')
$r.Output | ForEach-Object { Write-Host "  $_" }
if ($r.ExitCode -ne 0) { throw "render-validate: build failed (exit $($r.ExitCode))." }

$exe = Get-ChildItem -LiteralPath $BuildDirFull -Recurse -Filter 'vrclient_vulkan_render_validate.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $exe) { throw "render-validate: built exe not found under $BuildDirFull." }
Write-Host "exe: $($exe.FullName)"

Write-Step 'Step 4/5: run render-validate (essentials pass: render to offscreen target)'
$essentialArgs = @()
if ($RequireDevice) { $essentialArgs += '--require-device' }
$r = Invoke-Native -Exe $exe.FullName -Arguments $essentialArgs -WorkDir (Split-Path -Parent $exe.FullName)
$r.Output | ForEach-Object { Write-Host "  $_" }
$essentialsExit = $r.ExitCode
$essentialsOk = ($essentialsExit -eq 0)
$deviceFound = -not [bool](($r.Output -join "`n") -match 'NO physical device')
Write-Host "essentials pass exit=$essentialsExit (deviceFound=$deviceFound)"
if (-not $essentialsOk) { throw "render-validate: ESSENTIALS pass failed (exit $essentialsExit)." }

$validationStatus = 'deferred'
$validationDetail = ''
$validationExit = $null
$layerActive = $false
$validationFindingCount = $null
$thrownError = $null
if (-not $layerDir) {
    Write-Step 'Step 5/5: validation pass DEFERRED (no VkLayer_khronos_validation.json under prefix)'
    $validationDetail = "validation layer not installed under $VulkanPrefix; ran essentials only"
}
elseif (-not $deviceFound) {
    Write-Step 'Step 5/5: validation pass SKIPPED (no physical device on this host)'
    $validationStatus = 'skipped-no-device'
    $validationDetail = 'no Vulkan ICD/device present; cannot exercise the render path'
}
else {
    Write-Step "Step 5/5: run render-validate (--validate: VK_LAYER_KHRONOS_validation, assert ZERO findings)"
    Write-Host "layer dir (VK_LAYER_PATH): $layerDir"
    $savedLayerPath = $env:VK_LAYER_PATH
    try {
        $env:VK_LAYER_PATH = $layerDir
        $r = Invoke-Native -Exe $exe.FullName -Arguments @('--validate') -WorkDir (Split-Path -Parent $exe.FullName)
    }
    finally {
        $env:VK_LAYER_PATH = $savedLayerPath
    }
    $r.Output | ForEach-Object { Write-Host "  $_" }
    $validationExit = $r.ExitCode
    $layerActive = $true
    # Capture the harness's reported finding count ("validation finding count = N").
    if (($r.Output -join "`n") -match 'validation finding count = (\d+)') {
        $validationFindingCount = [int]$matches[1]
    }
    if ($validationExit -eq 0) {
        $validationStatus = 'clean'
        if ($null -eq $validationFindingCount) { $validationFindingCount = 0 }
        $validationDetail = 'VK_LAYER_KHRONOS_validation active; ZERO validation findings across the render path'
    }
    elseif ($validationExit -eq 3) {
        $validationStatus = 'layer-unavailable'
        $layerActive = $false
        $thrownError = "render-validate: validation pass could not load the layer (exit 3). VK_LAYER_PATH=$layerDir"
    }
    elseif ($validationExit -eq 4) {
        $validationStatus = 'errors'
        $thrownError = "render-validate: validation pass produced validation findings (exit 4) - see output above."
    }
    else {
        $validationStatus = 'failed'
        $thrownError = "render-validate: validation pass failed (exit $validationExit)."
    }
}

# ---- Emit a ledgered evidence artifact (HARD CONSTRAINT #4 paper trail) so the
# validation-clean render-path claim is reproducible from the evidence ledger,
# not just transient console output (review finding #7).
function Get-GitValue { param([string[]]$GitArgs)
    $g = Invoke-Native -Exe 'git' -Arguments $GitArgs
    if ($g.ExitCode -eq 0) { return (($g.Output -join "`n").Trim()) }
    return ''
}
$commit = Get-GitValue @('-C', $RepoRoot, 'rev-parse', 'HEAD')
$shortCommit = Get-GitValue @('-C', $RepoRoot, 'rev-parse', '--short', 'HEAD')
$branch = Get-GitValue @('-C', $RepoRoot, 'rev-parse', '--abbrev-ref', 'HEAD')
$statusOut = Get-GitValue @('-C', $RepoRoot, 'status', '--porcelain')
$dirty = -not [string]::IsNullOrWhiteSpace($statusOut)
$nowUtc = [DateTime]::UtcNow
$tsStamp = $nowUtc.ToString('yyyyMMdd-HHmmss')
$tsIso = $nowUtc.ToString('yyyy-MM-ddTHH:mm:ssZ')
$shortForName = if ($shortCommit) { $shortCommit } else { 'nogit' }

$validationGreen = ($validationStatus -eq 'clean') -or ($validationStatus -in @('deferred', 'skipped-no-device'))
$greenOverall = $essentialsOk -and $validationGreen

$runsDir = Join-Path $RepoRoot '.planning\evidence\runs'
if (Test-Path -LiteralPath $runsDir) {
    $ledger = [ordered]@{
        schema        = 'vrclient-render-validate/1'
        timestamp_utc = $tsIso
        repo_root     = $RepoRoot
        git           = [ordered]@{ commit = $commit; short = $shortCommit; branch = $branch; dirty = $dirty }
        parameters    = [ordered]@{
            build_dir      = $BuildDirFull
            build_type     = 'Release'
            compiler       = $compilerResolved
            vulkan_prefix  = $VulkanPrefix
            target         = 'vrclient_vulkan_render_validate'
            require_device = [bool]$RequireDevice
        }
        target        = [ordered]@{
            note = 'Headless Vulkan RENDER-PATH validation (VulkanTestSceneRenderer: initialize -> prepareEyeTargets -> renderEye over an OFFSCREEN VkImage). NOT the product, NOT the default CTest suite. OpenXR session/swapchain/headset render is compile/link-checked + DEFERRED.'
        }
        exe           = $exe.FullName
        essentials    = [ordered]@{ exit_code = $essentialsExit; device_found = $deviceFound; ok = $essentialsOk }
        validation    = [ordered]@{
            status                  = $validationStatus
            layer_active            = $layerActive
            exit_code               = $validationExit
            validation_finding_count = $validationFindingCount
            detail                  = $validationDetail
        }
        result        = [ordered]@{ green = $greenOverall; error = $thrownError }
    }
    $ledgerName = "$tsStamp-$shortForName-render-validate.json"
    $ledgerPath = Join-Path $runsDir $ledgerName
    $ledger | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $ledgerPath -Encoding UTF8
    Write-Host "ledger              : $ledgerPath"
}
else {
    Write-Host "ledger              : (skipped; $runsDir not found)"
}

# Honor the original failure contract AFTER the ledger is written, so a failing
# validation pass still leaves a durable, ledgered record of the failure.
if ($thrownError) { throw $thrownError }

Write-Step 'RESULT'
Write-Host "SDK prefix          : $VulkanPrefix"
Write-Host "exe                 : $($exe.FullName)"
Write-Host "essentials pass     : OK (exit 0; device found = $deviceFound)"
Write-Host "validation pass     : $validationStatus"
if ($null -ne $validationFindingCount) { Write-Host "validation findings : $validationFindingCount" }
Write-Host "  detail            : $validationDetail"
exit 0

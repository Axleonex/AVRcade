#Requires -Version 5.1
<##
.SYNOPSIS
    Build the local x86 fake OpenXR runtime used to exercise the GTA SA bridge.

.DESCRIPTION
    This is a deterministic integration-test runtime. It does not emulate a
    headset or replace a user's OpenXR runtime; it only lets the bridge reach
    session, view, swapchain, and frame-submission code on a host without an
    HMD. The runtime is never copied into the GTA installation.
##>
[CmdletBinding()]
param(
    [string]$BuildDir = '',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$BuildType = 'Release'
)

$ErrorActionPreference = 'Stop'
$repo = (Get-Item -LiteralPath (Join-Path $PSScriptRoot '..')).FullName
$source = Join-Path $repo 'tests\native\openxr_fake'
$repoIsUnc = $repo.StartsWith('\\', [StringComparison]::Ordinal)
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repo 'build\gtasa-fake-openxr-x86'
} elseif (-not [IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $repo $BuildDir
}

function Convert-ToBuildPath([string]$path) {
    $fullPath = [IO.Path]::GetFullPath($path)
    if (-not $repoIsUnc) { return $fullPath }
    $repoPrefix = $repo.TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "UNC builds require the source and build directories to stay under the repository: $fullPath"
    }
    return $fullPath.Substring($repoPrefix.Length)
}

$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars32.bat'
$cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
if (-not (Test-Path -LiteralPath $vcvars)) { throw "vcvars32.bat not found: $vcvars" }
if (-not (Test-Path -LiteralPath $cmake)) { throw "CMake not found: $cmake" }
if (-not (Test-Path -LiteralPath $ninja)) { throw "Ninja not found: $ninja" }

$cmakeSource = Convert-ToBuildPath $source
$cmakeBuild = Convert-ToBuildPath $BuildDir
$cmakeArgs = @(
    '-S', $cmakeSource,
    '-B', $cmakeBuild,
    '-G', 'Ninja',
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DCMAKE_MAKE_PROGRAM=$ninja"
)
$quotedArgs = ($cmakeArgs | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' }) -join ' '
$configure = '"' + $cmake + '" ' + $quotedArgs
$build = '"' + $cmake + '" --build "' + $cmakeBuild + '" --config ' + $BuildType
$command = if ($repoIsUnc) {
    'pushd "' + $repo + '" && call "' + $vcvars + '" && ' + $configure + ' && ' + $build + ' && popd'
} else {
    'call "' + $vcvars + '" && ' + $configure + ' && ' + $build
}
& cmd.exe /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw "Fake OpenXR runtime build failed with exit code $LASTEXITCODE" }

$manifest = Join-Path $source 'fake-openxr.json'
Copy-Item -LiteralPath $manifest -Destination (Join-Path $BuildDir 'fake-openxr.json') -Force
Write-Host "Fake x86 OpenXR runtime built: $(Join-Path $BuildDir 'fake_openxr_runtime.dll')"
Write-Host "Fake runtime manifest: $(Join-Path $BuildDir 'fake-openxr.json')"
Write-Host "OpenXR loader probe: $(Join-Path $BuildDir 'openxr_loader_probe.exe')"

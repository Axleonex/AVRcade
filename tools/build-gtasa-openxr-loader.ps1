#Requires -Version 5.1
<##
.SYNOPSIS
    Build the x86 OpenXR loader required by the GTA San Andreas ASI bridge.

.DESCRIPTION
    GTA San Andreas is a 32-bit process, so it cannot load the repository's
    existing x64 OpenXR loader. This script shallow-clones the pinned,
    Apache-2.0 Khronos loader source, builds its dynamic Windows loader under
    vcvars32.bat, and stages openxr_loader.dll beside the bridge artifact.

    The script never copies anything to the game directory.
#>
[CmdletBinding()]
param(
    [string]$BuildDir = '',
    [string]$Ref = 'release-1.1.43',
    [switch]$KeepIntermediate
)

$ErrorActionPreference = 'Stop'
$repo = (Get-Item -LiteralPath (Join-Path $PSScriptRoot '..')).FullName
$source = Join-Path $repo 'external\_openxr-src-x86'
$build = Join-Path $repo 'external\_openxr-build-x86'
$prefix = Join-Path $repo 'build\gtasa-x86\openxr'
$repoIsUnc = $repo.StartsWith('\\', [StringComparison]::Ordinal)
if (-not [string]::IsNullOrWhiteSpace($BuildDir)) {
    if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $repo $BuildDir }
    $build = $BuildDir
}
$stage = Join-Path $repo 'build\gtasa-x86'

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

$git = Get-Command git.exe -ErrorAction SilentlyContinue
if ($null -eq $git) { throw 'git.exe is required to fetch the pinned OpenXR loader source' }
if (Test-Path -LiteralPath $source) { Remove-Item -LiteralPath $source -Recurse -Force }
if (Test-Path -LiteralPath $build) { Remove-Item -LiteralPath $build -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $source), $stage | Out-Null

& $git.Source clone --depth 1 --branch $Ref https://github.com/KhronosGroup/OpenXR-SDK.git $source
if ($LASTEXITCODE -ne 0) { throw "OpenXR source clone failed with exit code $LASTEXITCODE" }

$cmakeSource = Convert-ToBuildPath $source
$cmakeBuild = Convert-ToBuildPath $build
$cmakePrefix = Convert-ToBuildPath $prefix
$cmakeArgs = @(
    '-S', $cmakeSource,
    '-B', $cmakeBuild,
    '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_MAKE_PROGRAM=$ninja",
    "-DCMAKE_INSTALL_PREFIX=$cmakePrefix",
    '-DDYNAMIC_LOADER=ON',
    '-DBUILD_TESTS=OFF',
    '-DBUILD_API_LAYERS=OFF',
    '-DBUILD_CONFORMANCE_TESTS=OFF',
    '-DBUILD_CONFORMANCE_CLI=OFF'
)
$quoted = ($cmakeArgs | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' }) -join ' '
$configure = '"' + $cmake + '" ' + $quoted
$compile = '"' + $cmake + '" --build "' + $cmakeBuild + '" --target openxr_loader --parallel 2'
$install = '"' + $cmake + '" --install "' + $cmakeBuild + '" --config Release'
$command = if ($repoIsUnc) {
    'pushd "' + $repo + '" && call "' + $vcvars + '" && ' + $configure + ' && ' + $compile + ' && ' + $install + ' && popd'
} else {
    'call "' + $vcvars + '" && ' + $configure + ' && ' + $compile + ' && ' + $install
}
& cmd.exe /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw "x86 OpenXR loader build failed with exit code $LASTEXITCODE" }

$loader = Get-ChildItem -LiteralPath $prefix -Recurse -Filter openxr_loader.dll -ErrorAction SilentlyContinue |
    Select-Object -First 1
if ($null -eq $loader) { throw "OpenXR install completed without openxr_loader.dll under $prefix" }
$staged = Join-Path $stage 'openxr_loader.dll'
Copy-Item -LiteralPath $loader.FullName -Destination $staged -Force
Write-Host "x86 OpenXR loader staged: $staged"

if (-not $KeepIntermediate) {
    if (Test-Path -LiteralPath $source) { Remove-Item -LiteralPath $source -Recurse -Force }
    if (Test-Path -LiteralPath $build) { Remove-Item -LiteralPath $build -Recurse -Force }
}

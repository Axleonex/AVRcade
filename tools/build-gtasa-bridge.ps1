#Requires -Version 5.1
<##
.SYNOPSIS
    Build the GTA San Andreas x86 ASI theater bridge.

.DESCRIPTION
    The main VRClient native build is x64. GTA San Andreas is PE32/x86, so this
    script deliberately imports vcvars32.bat and configures a separate Ninja
    build tree. The bridge dynamically resolves an x86 OpenXR loader at runtime;
    the x64 loader under external/openxr is never linked into this target.

    This command only builds under the repository. It does not copy anything to
    the game directory.
#>
[CmdletBinding()]
param(
    [string]$BuildDir = '',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$BuildType = 'Release'
)

$ErrorActionPreference = 'Stop'
$repo = (Get-Item -LiteralPath (Join-Path $PSScriptRoot '..')).FullName
$source = Join-Path $repo 'adapters\gta_sa'
$repoIsUnc = $repo.StartsWith('\\', [StringComparison]::Ordinal)
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repo 'build\gtasa-x86'
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
if ($LASTEXITCODE -ne 0) { throw "GTA SA bridge build failed with exit code $LASTEXITCODE" }

$artifact = Join-Path $BuildDir 'vrclient_gtasa_theater.asi'
if (-not (Test-Path -LiteralPath $artifact)) { throw "Build completed without expected artifact: $artifact" }
Write-Host "GTA SA bridge built: $artifact"

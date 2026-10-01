#Requires -Version 5.1
# [PowerShell invoking CMD] Build/check only; never installs into the game.
[CmdletBinding()]
param([string]$BuildDirectory = 'build\fnv-x86-current')
$ErrorActionPreference = 'Stop'
$repo = (Get-Item -LiteralPath (Join-Path $PSScriptRoot '..')).FullName
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer was not found.' }
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'MSVC x86 build tools were not found.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars32.bat'
$cmakeDir = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake'
$cmake = Join-Path $cmakeDir 'CMake\bin\cmake.exe'
$ctest = Join-Path $cmakeDir 'CMake\bin\ctest.exe'
$ninja = Join-Path $cmakeDir 'Ninja\ninja.exe'
foreach ($tool in @($vcvars, $cmake, $ctest, $ninja)) {
    if (-not (Test-Path -LiteralPath $tool)) { throw "Missing build tool: $tool" }
}
# ponytail: a standalone x86 build, leaving the x64 client and other games alone.
$command = 'pushd "{0}" && call "{1}" && "{2}" -S adapters\fallout_new_vegas -B "{5}" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_MAKE_PROGRAM={3}" && "{2}" --build "{5}" && "{4}" --test-dir "{5}" --output-on-failure && popd' -f $repo, $vcvars, $cmake, $ninja, $ctest, $BuildDirectory
& cmd.exe /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw "FNV native build/check failed: $LASTEXITCODE" }

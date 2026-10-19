#Requires -Version 5.1
[CmdletBinding()]
param([string]$BuildDirectory = '', [ValidateSet('x64','x86')][string]$Architecture = 'x64')
$ErrorActionPreference = 'Stop'
# [PowerShell]
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (!$BuildDirectory) { $BuildDirectory = Join-Path $root ('build\controller-remap-'+$Architecture) }
if (!(Test-Path -LiteralPath (Join-Path $root 'external\openxr\include\openxr\openxr.h'))) { throw 'OpenXR SDK headers missing. Run tools\build-openxr.ps1 before packaging.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
$vcvars = Join-Path $vs ('VC\Auxiliary\Build\vcvars'+$(if($Architecture -eq 'x64'){'64'}else{'32'})+'.bat')
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
foreach ($path in @($root,$BuildDirectory)) { if ($path.Contains('"') -or $path.Contains('%') -or $path.Contains('&')) { throw 'Unsupported compiler path.' } }
# [CMD] Compiler setup/build only; filesystem changes use PowerShell.
$line = 'call "'+$vcvars+'" && "'+$cmake+'" -S "'+$root+'\native\controller-remap" -B "'+$BuildDirectory+'" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DOPENXR_INCLUDE="'+$root+'\external\openxr\include" && "'+$cmake+'" --build "'+$BuildDirectory+'" --target avrcade-controller-remap'
& $env:ComSpec /d /s /c $line
if ($LASTEXITCODE -ne 0) { throw 'Controller remap build failed.' }
$package = Join-Path $root 'native\controller-remap\package'
if ($Architecture -eq 'x86') { $package = Join-Path $package 'x86' }
New-Item -ItemType Directory -Path $package -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'avrcade-controller-remap.dll') -Destination $package -Force
Copy-Item -LiteralPath (Join-Path $root 'native\controller-remap\layer.json') -Destination $package -Force
if ($Architecture -eq 'x86') {
    $manifestPath = Join-Path $package 'layer.json'
    $manifestText = [IO.File]::ReadAllText($manifestPath).Replace('XR_APILAYER_AVRCADE_game_controls','XR_APILAYER_AVRCADE_game_controls_x86')
    [IO.File]::WriteAllText($manifestPath,$manifestText)
}

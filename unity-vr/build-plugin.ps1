#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$GameDir,
    [Parameter(Mandatory = $true)][ValidatePattern('^[a-z0-9][a-z0-9-]*$')][string]$Slug,
    [string]$ReferenceGameDir = '',
    [string]$BepInExArchive = ''
)

$ErrorActionPreference = 'Stop'
$UnityVrRoot = (Resolve-Path -LiteralPath $PSScriptRoot).Path
$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $UnityVrRoot '..')).Path
$GameDir = [IO.Path]::GetFullPath($GameDir)
if (-not (Test-Path -LiteralPath $GameDir)) { New-Item -ItemType Directory -Force -Path $GameDir | Out-Null }
$ReferenceGameDir = if ($ReferenceGameDir) { [IO.Path]::GetFullPath($ReferenceGameDir) } else { $GameDir }

$managed = Get-ChildItem -LiteralPath $ReferenceGameDir -Directory -Filter '*_Data' -ErrorAction SilentlyContinue |
    ForEach-Object { Join-Path $_.FullName 'Managed' } |
    Where-Object { Test-Path -LiteralPath (Join-Path $_ 'Assembly-CSharp.dll') } |
    Select-Object -First 1
if (-not $managed) { throw "Mono gate failed: Assembly-CSharp.dll not found under $ReferenceGameDir\*_Data\Managed" }
if (Get-ChildItem -LiteralPath $ReferenceGameDir -Directory -Filter 'il2cpp_data' -Recurse -ErrorAction SilentlyContinue |
    Select-Object -First 1) { throw "Mono gate failed: il2cpp_data is present under $ReferenceGameDir" }
if (-not (Test-Path -LiteralPath (Join-Path $managed 'UnityEngine.XRModule.dll'))) {
    throw "XR ABI gate failed: UnityEngine.XRModule.dll is absent from $managed"
}

$bepinexVersion = '5.4.23.2'
$bepinexSha256 = 'f752ce4e838f4c305b9da1404b6745f2cff23b8bfd494f79f0c84d0a01f59b46'
$bepinexUrl = "https://github.com/BepInEx/BepInEx/releases/download/v$bepinexVersion/BepInEx_win_x64_$bepinexVersion.zip"
if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'winhttp.dll'))) {
    $archive = if ($BepInExArchive) { [IO.Path]::GetFullPath($BepInExArchive) } else {
        $cache = Join-Path $UnityVrRoot '.cache'
        New-Item -ItemType Directory -Force -Path $cache | Out-Null
        Join-Path $cache "BepInEx_win_x64_$bepinexVersion.zip"
    }
    if (-not (Test-Path -LiteralPath $archive)) {
        Write-Host "BEPINEX: fetching pinned v$bepinexVersion"
        Invoke-WebRequest -Uri $bepinexUrl -OutFile $archive
    }
    $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $bepinexSha256) { throw "BepInEx hash mismatch: expected=$bepinexSha256 actual=$actual" }
    Expand-Archive -LiteralPath $archive -DestinationPath $GameDir -Force
    Write-Host "BEPINEX: installed hash=verified -> $GameDir"
}

$bepinexCore = Join-Path $GameDir 'BepInEx\core'
if (-not (Test-Path -LiteralPath (Join-Path $bepinexCore 'BepInEx.dll'))) {
    throw "BepInEx core missing after install: $bepinexCore"
}
$props = @"
<Project>
  <PropertyGroup>
    <BepInExCoreDir>$([Security.SecurityElement]::Escape($bepinexCore))</BepInExCoreDir>
    <GameManagedDir>$([Security.SecurityElement]::Escape($managed))</GameManagedDir>
  </PropertyGroup>
</Project>
"@
[IO.File]::WriteAllText((Join-Path $UnityVrRoot 'references.local.props'), $props, [Text.UTF8Encoding]::new($false))

$dotnet = (Get-Command dotnet.exe -ErrorAction SilentlyContinue).Source
if (-not $dotnet -and (Test-Path 'G:\AIStorage\dotnet\dotnet.exe')) { $dotnet = 'G:\AIStorage\dotnet\dotnet.exe' }
if (-not $dotnet) { throw 'dotnet SDK not found' }
& $dotnet build (Join-Path $UnityVrRoot 'VrClient.UnityVR') -c Release --nologo
if ($LASTEXITCODE -ne 0) { throw "plugin build failed with exit code $LASTEXITCODE" }

$pluginDir = Join-Path $GameDir 'BepInEx\plugins\VRClient-UnityVR'
New-Item -ItemType Directory -Force -Path $pluginDir | Out-Null
$pluginDll = Join-Path $UnityVrRoot 'VrClient.UnityVR\bin\Release\netstandard2.1\VrClient.UnityVR.dll'
$logicDll = Join-Path $UnityVrRoot 'VrClient.UnityVR.Logic\bin\Release\netstandard2.1\VrClient.UnityVR.Logic.dll'
Copy-Item -LiteralPath $pluginDll, $logicDll -Destination $pluginDir -Force

$configDir = Join-Path $GameDir 'BepInEx\config'
New-Item -ItemType Directory -Force -Path $configDir | Out-Null
$configPath = Join-Path $configDir "vrclient-unityvr.$Slug.json"
if (-not (Test-Path -LiteralPath $configPath)) {
    [ordered]@{ CameraObjectName=''; DuplicateCamera=$false; WorldScale=1.0; DisableObjects=@(); UiMode='follow' } |
        ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $configPath -Encoding UTF8
}
$manifest = [ordered]@{ files = @(
    'BepInEx/plugins/VRClient-UnityVR/VrClient.UnityVR.dll',
    'BepInEx/plugins/VRClient-UnityVR/VrClient.UnityVR.Logic.dll'
) }
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $GameDir 'BepInEx\vrclient-install-manifest.json') -Encoding UTF8
Write-Host "RESULT: unityvr-build ok slug=$Slug managed=$managed plugin=$pluginDir"

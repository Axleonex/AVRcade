#Requires -Version 5.1
[CmdletBinding()]
param(
    [ValidatePattern('^\d+\.\d+\.\d+([-.][0-9A-Za-z.-]+)?$')]
    [string]$Version = '0.2.0',
    [string]$NativeBuildDir = 'build/package-native',
    [string]$FnvNativeBuildDir = 'build/fnv-x86',
    [string]$GtaSaBridgePath = '',
    [string]$InnoCompiler = '',
    [switch]$SkipManagedTests,
    [switch]$SkipInstaller
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$PackageRoot = Join-Path $RepoRoot 'build\package\windows'
$PayloadRoot = Join-Path $PackageRoot 'payload'
$AppPublish = Join-Path $PackageRoot 'publish-app'
$CliPublish = Join-Path $PackageRoot 'publish-cli'
$DistRoot = Join-Path $RepoRoot 'artifacts\dist'

function Assert-UnderRepo([string]$Path) {
    $full = [System.IO.Path]::GetFullPath($Path)
    $prefix = $RepoRoot.TrimEnd('\') + '\'
    if (-not $full.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "refusing filesystem mutation outside repository: $full"
    }
    return $full
}

function Reset-Directory([string]$Path) {
    $full = Assert-UnderRepo $Path
    if (Test-Path -LiteralPath $full) {
        Remove-Item -LiteralPath $full -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $full | Out-Null
}

function Invoke-Checked([string]$Exe, [string[]]$Arguments, [string]$Label) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

function Resolve-DotNet {
    $cmd = Get-Command dotnet.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $projectLocal = 'G:\AIStorage\dotnet\dotnet.exe'
    if (Test-Path -LiteralPath $projectLocal) { return $projectLocal }
    throw 'dotnet SDK not found (build-time dependency only)'
}

function Resolve-InnoCompiler {
    if ($InnoCompiler) {
        if (-not (Test-Path -LiteralPath $InnoCompiler)) {
            throw "Inno compiler not found: $InnoCompiler"
        }
        return (Resolve-Path -LiteralPath $InnoCompiler).Path
    }
    if ($env:INNO_ISCC -and (Test-Path -LiteralPath $env:INNO_ISCC)) {
        return (Resolve-Path -LiteralPath $env:INNO_ISCC).Path
    }
    $candidates = @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe')
    )
    $found = $candidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
    if (-not $found) {
        throw 'Inno Setup 6 compiler not found. Install build-time package JRSoftware.InnoSetup or pass -InnoCompiler.'
    }
    return (Resolve-Path -LiteralPath $found).Path
}

function Copy-VcRuntime([string]$Destination) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'vswhere not found; cannot locate app-local VC runtime DLLs'
    }
    $vsRoot = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
    if (-not $vsRoot) { throw 'Visual C++ build tools not found' }
    $redistRoot = Join-Path $vsRoot 'VC\Redist\MSVC'
    $crtDir = Get-ChildItem -LiteralPath $redistRoot -Directory |
        Sort-Object Name -Descending |
        ForEach-Object {
            Get-ChildItem -LiteralPath (Join-Path $_.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue |
                Sort-Object Name -Descending |
                Select-Object -First 1 -ExpandProperty FullName
        } |
        Where-Object { Test-Path -LiteralPath $_ } |
        Select-Object -First 1
    if (-not $crtDir) { throw "app-local Visual C++ runtime directory not found under $redistRoot" }
    Copy-Item -LiteralPath (Join-Path $crtDir 'msvcp140.dll') -Destination $Destination
    Copy-Item -LiteralPath (Join-Path $crtDir 'vcruntime140.dll') -Destination $Destination
    $vcruntime1 = Join-Path $crtDir 'vcruntime140_1.dll'
    if (Test-Path -LiteralPath $vcruntime1) {
        Copy-Item -LiteralPath $vcruntime1 -Destination $Destination
    }
}

$DotNet = Resolve-DotNet
$Iscc = if ($SkipInstaller) { $null } else { Resolve-InnoCompiler }

if (-not $GtaSaBridgePath) {
    throw 'GTA SA release verification requires -GtaSaBridgePath pointing to the tested x86 ASI artifact.'
}
& (Join-Path $PSScriptRoot 'assert-gtasa-bridge-sync.ps1') `
    -ProfilePath (Join-Path $RepoRoot 'config\legacy\gta-san-andreas.json') `
    -BridgePath $GtaSaBridgePath `
    -NativeProfilePath (Join-Path $RepoRoot 'config\legacy\gta-san-andreas-native.json')

if (-not $SkipManagedTests) {
    Invoke-Checked $DotNet @('test', (Join-Path $RepoRoot 'client\VrClient.sln'), '-c', 'Release', '--nologo') 'managed tests'
}

$nativeFull = if ([System.IO.Path]::IsPathRooted($NativeBuildDir)) {
    $NativeBuildDir
} else {
    Join-Path $RepoRoot $NativeBuildDir
}
$nativeFull = [System.IO.Path]::GetFullPath($nativeFull)
$fnvNativeFull = if ([System.IO.Path]::IsPathRooted($FnvNativeBuildDir)) {
    $FnvNativeBuildDir
} else {
    Join-Path $RepoRoot $FnvNativeBuildDir
}
$fnvNativeFull = [System.IO.Path]::GetFullPath($fnvNativeFull)
$safetyCli = Join-Path $nativeFull 'vrclient_safety_cli.exe'
$signCli = Join-Path $nativeFull 'vrclient_sign_cli.exe'
if (-not (Test-Path -LiteralPath $safetyCli) -or -not (Test-Path -LiteralPath $signCli)) {
    throw "Release native helpers missing in $nativeFull. Run: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\ci\build-and-test.ps1 -BuildDir $NativeBuildDir -BuildType Release -Compiler msvc -OpenXR off"
}
$fnvNativeFiles = @(
    'fnv-test-launcher.exe', 'fnv-runtime-check.exe', 'fnv-render-observer.exe',
    'vrclient_fnv_stereo.dll', 'openxr_loader.dll'
)
$missingFnvNative = @($fnvNativeFiles | Where-Object { -not (Test-Path -LiteralPath (Join-Path $fnvNativeFull $_)) })
if ($missingFnvNative.Count -gt 0) {
    throw "Fallout: New Vegas native release files missing in ${fnvNativeFull}: $($missingFnvNative -join ', '). Run tools\build-fnv-native.ps1 -BuildDirectory $FnvNativeBuildDir"
}

Reset-Directory $PackageRoot
New-Item -ItemType Directory -Force -Path $PayloadRoot, $AppPublish, $CliPublish, $DistRoot | Out-Null

$publishCommon = @(
    '-c', 'Release', '-r', 'win-x64', '--self-contained', 'true',
    '-p:PublishSingleFile=true',
    '-p:IncludeNativeLibrariesForSelfExtract=true',
    '-p:EnableCompressionInSingleFile=true',
    '-p:DebugType=None', '-p:DebugSymbols=false',
    "-p:Version=$Version", '--nologo'
)
Invoke-Checked $DotNet (@('publish', (Join-Path $RepoRoot 'client\VrClient.App\VrClient.App.csproj')) + $publishCommon + @('-o', $AppPublish)) 'app publish'
Invoke-Checked $DotNet (@('publish', (Join-Path $RepoRoot 'client\VrClient.Cli\VrClient.Cli.csproj')) + $publishCommon + @('-o', $CliPublish)) 'CLI publish'

Copy-Item -LiteralPath (Join-Path $AppPublish 'vrclient-app.exe') -Destination $PayloadRoot
Copy-Item -LiteralPath (Join-Path $CliPublish 'vrclient.exe') -Destination $PayloadRoot
Copy-Item -LiteralPath (Join-Path $RepoRoot 'config') -Destination $PayloadRoot -Recurse
& (Join-Path $PSScriptRoot 'assert-gtasa-bridge-sync.ps1') `
    -ProfilePath (Join-Path $PayloadRoot 'config\legacy\gta-san-andreas.json') `
    -BridgePath $GtaSaBridgePath `
    -NativeProfilePath (Join-Path $PayloadRoot 'config\legacy\gta-san-andreas-native.json')
Copy-Item -LiteralPath (Join-Path $RepoRoot 'installer\README.txt') -Destination $PayloadRoot

$fnvAdapterDest = New-Item -ItemType Directory -Force -Path (Join-Path $PayloadRoot 'adapters\fallout_new_vegas')
Copy-Item -LiteralPath (Join-Path $RepoRoot 'adapters\fallout_new_vegas\adapter.json') -Destination $fnvAdapterDest.FullName
Copy-Item -LiteralPath (Join-Path $RepoRoot 'adapters\fallout_new_vegas\dependency-manifest.schema.json') -Destination $fnvAdapterDest.FullName

$nativeDest = New-Item -ItemType Directory -Force -Path (Join-Path $PayloadRoot 'native')
Copy-Item -LiteralPath $safetyCli, $signCli -Destination $nativeDest.FullName
Copy-VcRuntime $nativeDest.FullName
# The GTA San Andreas VR bridge is installed into the game folder by the app
# ("Install VR bridge"), so the tested x86 bridge and its OpenXR loader ship
# beside the other native helpers.
$gtaBridgeFull = (Resolve-Path -LiteralPath $GtaSaBridgePath).Path
$gtaLoaderFull = Join-Path (Split-Path -Parent $gtaBridgeFull) 'openxr_loader.dll'
if (-not (Test-Path -LiteralPath $gtaLoaderFull)) {
    throw "x86 openxr_loader.dll not found beside the GTA SA bridge: $gtaLoaderFull"
}
$gtaNativeDest = New-Item -ItemType Directory -Force -Path (Join-Path $nativeDest.FullName 'gta-san-andreas')
Copy-Item -LiteralPath $gtaBridgeFull, $gtaLoaderFull -Destination $gtaNativeDest.FullName
# The Cyberpunk 2077 VR backend is installed into the game folder by the app
# ("Install VR backend"). It ships only as a complete, hash-verified set; without
# it the app says so and keeps the monitor modes.
$cyberpunkBackend = Join-Path $RepoRoot 'build\cyberpunk-backend'
& (Join-Path $PSScriptRoot 'collect-cyberpunk-backend.ps1') -OutDir $cyberpunkBackend | Out-Host
if ($LASTEXITCODE -eq 0) {
    Copy-Item -LiteralPath $cyberpunkBackend -Destination (Join-Path $nativeDest.FullName 'cyberpunk-2077') -Recurse
} else {
    Write-Warning 'Cyberpunk 2077 VR backend is incomplete; this package ships without it.'
}
$fnvNativeDest = New-Item -ItemType Directory -Force -Path (Join-Path $nativeDest.FullName 'fallout-new-vegas')
foreach ($fileName in $fnvNativeFiles) {
    Copy-Item -LiteralPath (Join-Path $fnvNativeFull $fileName) -Destination $fnvNativeDest.FullName
}

# The licences of the bundled components require their texts to travel with every copy.
Copy-Item -LiteralPath (Join-Path $RepoRoot 'licenses') -Destination $PayloadRoot -Recurse
Copy-Item -LiteralPath (Join-Path $RepoRoot 'THIRD-PARTY-NOTICES.md'), (Join-Path $RepoRoot 'LICENSE') -Destination $PayloadRoot

$docsDest = New-Item -ItemType Directory -Force -Path (Join-Path $PayloadRoot 'docs')
Copy-Item -LiteralPath (Join-Path $RepoRoot 'docs\app\README.md') -Destination $docsDest.FullName
Copy-Item -LiteralPath (Join-Path $RepoRoot 'docs\onboarding') -Destination $docsDest.FullName -Recurse
Copy-Item -LiteralPath (Join-Path $RepoRoot 'docs\troubleshooting') -Destination $docsDest.FullName -Recurse
Copy-Item -LiteralPath (Join-Path $RepoRoot 'docs\safety\unified-safety.md') -Destination $docsDest.FullName
Copy-Item -LiteralPath (Join-Path $RepoRoot 'docs\fallout-new-vegas') -Destination $docsDest.FullName -Recurse

$commit = (& git -C $RepoRoot rev-parse HEAD 2>$null)
if (-not $commit) { $commit = 'unknown' }
$manifestFiles = @(
    Get-ChildItem -LiteralPath $PayloadRoot -File -Recurse | Sort-Object FullName | ForEach-Object {
        [ordered]@{
            path = $_.FullName.Substring($PayloadRoot.Length + 1).Replace('\', '/')
            size = $_.Length
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
)
$releaseManifest = [ordered]@{
    schema = 'vrclient-release/1'
    version = $Version
    commit = "$commit".Trim()
    platform = 'win-x64'
    self_contained = $true
    files = $manifestFiles
}
$releaseManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $PayloadRoot 'release-manifest.json') -Encoding UTF8

$savedRepoRoot = $env:VRCLIENT_REPO_ROOT
try {
    Remove-Item Env:VRCLIENT_REPO_ROOT -ErrorAction SilentlyContinue
    $gamesOutput = & (Join-Path $PayloadRoot 'vrclient.exe') games 2>&1
    $gamesText = $gamesOutput -join "`n"
    if ($LASTEXITCODE -ne 0 -or $gamesText -notmatch 'RESULT: games=\d+' -or
        $gamesText -match 'GAME: fallout-(3|new-vegas) ') {
        throw "staged CLI exposed an unpublished Fallout game or failed to list games: $($gamesOutput -join ' ')"
    }
    $safetyOutput = & (Join-Path $PayloadRoot 'vrclient.exe') safety repo 2>&1
    if ($LASTEXITCODE -ne 0 -or (($safetyOutput -join "`n") -notmatch 'crosscheck=available')) {
        throw "staged CLI did not locate packaged native safety helper: $($safetyOutput -join ' ')"
    }
    foreach ($fileName in $fnvNativeFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $fnvNativeDest.FullName $fileName))) {
            throw "staged Fallout: New Vegas native file is missing: $fileName"
        }
    }
}
finally {
    if ($null -eq $savedRepoRoot) { Remove-Item Env:VRCLIENT_REPO_ROOT -ErrorAction SilentlyContinue }
    else { $env:VRCLIENT_REPO_ROOT = $savedRepoRoot }
}

$portableArchive = Join-Path $DistRoot "AVRcade-$Version-win-x64.zip"
if (Test-Path -LiteralPath $portableArchive) {
    Remove-Item -LiteralPath $portableArchive -Force
}
Compress-Archive -Path (Join-Path $PayloadRoot '*') -DestinationPath $portableArchive -CompressionLevel Optimal
$portableHash = (Get-FileHash -LiteralPath $portableArchive -Algorithm SHA256).Hash.ToLowerInvariant()
"$portableHash  AVRcade-$Version-win-x64.zip" | Set-Content -LiteralPath (Join-Path $DistRoot "AVRcade-$Version-win-x64.sha256") -Encoding ASCII

if ($SkipInstaller) {
    Write-Host "RESULT: package ok version=$Version portable=$portableArchive sha256=$portableHash installer=skipped"
    return
}

$iss = Join-Path $RepoRoot 'installer\VRClient.iss'
Invoke-Checked $Iscc @("/DAppVersion=$Version", "/DSourceRoot=$PayloadRoot", "/DOutputDir=$DistRoot", $iss) 'installer compile'

$installer = Join-Path $DistRoot "AVRcade-Setup-$Version.exe"
if (-not (Test-Path -LiteralPath $installer)) { throw "installer output missing: $installer" }
$installerHash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
"$installerHash  AVRcade-Setup-$Version.exe" | Set-Content -LiteralPath (Join-Path $DistRoot "AVRcade-Setup-$Version.sha256") -Encoding ASCII

Write-Host "RESULT: package ok version=$Version portable=$portableArchive portable_sha256=$portableHash installer=$installer installer_sha256=$installerHash"

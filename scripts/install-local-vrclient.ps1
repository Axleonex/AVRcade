#Requires -Version 5.1
[CmdletBinding()]
param([string]$FnvNativeBuildDir = 'build\fnv-x86')

$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$DotNet = 'G:\AIStorage\dotnet\dotnet.exe'
if (-not (Test-Path -LiteralPath $DotNet)) {
    $dotnetCommand = Get-Command dotnet.exe -ErrorAction SilentlyContinue
    if (-not $dotnetCommand) { throw 'The .NET 8 SDK is required to publish AVRcade.' }
    $DotNet = $dotnetCommand.Source
}

$BuildRoot = Join-Path $RepoRoot 'build\local-install'
$RepoStage = Join-Path $BuildRoot 'staging'
$ProgramsRoot = Join-Path $env:LOCALAPPDATA 'Programs'
$InstallRoot = Join-Path $ProgramsRoot 'VRClient'
$InstallStage = Join-Path $ProgramsRoot 'VRClient.staging'
$RollbackRoot = Join-Path $ProgramsRoot 'VRClient.rollback'
$InstalledExe = Join-Path $InstallRoot 'vrclient-app.exe'
$DesktopShortcut = Join-Path $env:USERPROFILE 'Desktop\AVRcade.lnk'
$StartMenuShortcut = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\AVRcade.lnk'
$LegacyDesktopShortcut = Join-Path $env:USERPROFILE 'Desktop\VRClient.lnk'
$LegacyStartMenuShortcut = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\VRClient.lnk'
$StatusHelper = Join-Path $env:USERPROFILE 'Desktop\VRClient Status.cmd'

function Assert-UnderRepo([string]$Path) {
    $full = [System.IO.Path]::GetFullPath($Path)
    $prefix = $RepoRoot.TrimEnd('\') + '\'
    if (-not $full.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing repository mutation outside the VRClient checkout: $full"
    }
    return $full
}

function Assert-CanonicalInstallPath([string]$Path) {
    $full = [System.IO.Path]::GetFullPath($Path).TrimEnd('\')
    $allowed = @($InstallRoot, $InstallStage, $RollbackRoot) |
        ForEach-Object { [System.IO.Path]::GetFullPath($_).TrimEnd('\') }
    if ($full -notin $allowed) {
        throw "Refusing local-install mutation outside the canonical VRClient paths: $full"
    }
    return $full
}

function Reset-RepoDirectory([string]$Path) {
    $full = Assert-UnderRepo $Path
    if (Test-Path -LiteralPath $full) {
        Remove-Item -LiteralPath $full -Recurse -Force
    }
    New-Item -ItemType Directory -Path $full -Force | Out-Null
}

function Remove-CanonicalDirectory([string]$Path) {
    $full = Assert-CanonicalInstallPath $Path
    if (Test-Path -LiteralPath $full) {
        Remove-Item -LiteralPath $full -Recurse -Force
    }
}

function Invoke-Checked([string]$Executable, [string[]]$Arguments, [string]$Label) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

function Copy-PublishedFilesWithRetry([string]$Source, [string]$Destination) {
    $attempts = 10
    for ($attempt = 1; $attempt -le $attempts; $attempt++) {
        try {
            Get-ChildItem -LiteralPath $Source -Force |
                Copy-Item -Destination $Destination -Recurse -Force -ErrorAction Stop
            return
        }
        catch [System.IO.IOException] {
            if ($attempt -eq $attempts) { throw }
            Start-Sleep -Seconds 1
        }
    }
}

function Test-AppWindow([string]$Executable) {
    $workingDirectory = Split-Path -Parent $Executable
    $process = Start-Process -FilePath $Executable -WorkingDirectory $workingDirectory -PassThru
    try {
        # A cold single-file .NET extraction can exceed 20 seconds on the first
        # launch; subsequent warm launches are much faster.
        $deadline = [DateTime]::UtcNow.AddSeconds(90)
        do {
            Start-Sleep -Milliseconds 250
            $process.Refresh()
        } while (-not $process.HasExited -and
                 $process.MainWindowHandle -eq 0 -and
                 [DateTime]::UtcNow -lt $deadline)

        if ($process.HasExited -or
            $process.MainWindowHandle -eq 0 -or
            $process.MainWindowTitle -notlike 'AVRcade*') {
            throw "AVRcade did not open a window from $Executable (exited=$($process.HasExited), title='$($process.MainWindowTitle)')"
        }

        [void]$process.CloseMainWindow()
        if (-not $process.WaitForExit(5000)) {
            Stop-Process -Id $process.Id -Force
        }
    }
    finally {
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
    }
}

function Set-Shortcut([string]$ShortcutPath, [string]$TargetPath) {
    $shortcutDirectory = Split-Path -Parent $ShortcutPath
    New-Item -ItemType Directory -Path $shortcutDirectory -Force | Out-Null
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($ShortcutPath)
    $shortcut.TargetPath = $TargetPath
    $shortcut.WorkingDirectory = Split-Path -Parent $TargetPath
    $shortcut.Description = 'Open AVRcade'
    $shortcut.IconLocation = "$TargetPath,0"
    $shortcut.Save()
}

function Assert-ShortcutTarget([string]$ShortcutPath, [string]$ExpectedTarget) {
    if (-not (Test-Path -LiteralPath $ShortcutPath)) {
        throw "AVRcade shortcut was not created: $ShortcutPath"
    }
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($ShortcutPath)
    $actual = [System.IO.Path]::GetFullPath($shortcut.TargetPath)
    $expected = [System.IO.Path]::GetFullPath($ExpectedTarget)
    if (-not $actual.Equals($expected, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "AVRcade shortcut target mismatch: $ShortcutPath -> $actual"
    }
}

function Remove-LegacyShortcut([string]$ShortcutPath, [string]$ExpectedTarget) {
    if (-not (Test-Path -LiteralPath $ShortcutPath)) { return }
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($ShortcutPath)
    $actual = [System.IO.Path]::GetFullPath($shortcut.TargetPath)
    $expected = [System.IO.Path]::GetFullPath($ExpectedTarget)
    if ($actual.Equals($expected, [System.StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $ShortcutPath -Force
    }
}

function Assert-InstalledAppClosed {
    $runningCanonical = Get-Process -Name 'vrclient-app' -ErrorAction SilentlyContinue |
        Where-Object {
            try {
                [System.IO.Path]::GetFullPath($_.Path).Equals(
                    [System.IO.Path]::GetFullPath($InstalledExe),
                    [System.StringComparison]::OrdinalIgnoreCase)
            }
            catch { $false }
        }
    if ($runningCanonical) {
        throw 'Close the installed AVRcade window before updating the app.'
    }
}

function Find-GtaSaInstall {
    $override = $env:VRCLIENT_GTASA_DIR
    if ($override -and (Test-Path -LiteralPath (Join-Path $override 'gta_sa.exe') -PathType Leaf)) {
        return [System.IO.Path]::GetFullPath($override)
    }
    foreach ($drive in Get-PSDrive -PSProvider FileSystem) {
        $candidate = Join-Path $drive.Root 'Grand Theft Auto San Andreas + Utilities\GTA San Andreas'
        if (Test-Path -LiteralPath (Join-Path $candidate 'gta_sa.exe') -PathType Leaf) {
            return $candidate
        }
    }
    return $null
}

$GtaSaInstall = Find-GtaSaInstall
if ($GtaSaInstall) {
    $GtaSaProfile = Join-Path $RepoRoot 'config\legacy\gta-san-andreas.json'
    $GtaSaNativeProfile = Join-Path $RepoRoot 'config\legacy\gta-san-andreas-native.json'
    $GtaSaBridge = Join-Path $GtaSaInstall 'vrclient_gtasa_theater.asi'
    $GtaSaInstalledNative = Join-Path $GtaSaInstall 'vrclient_gtasa_theater.json'
}
$GtaSaSyncRequired = $GtaSaInstall -and (Test-Path -LiteralPath $GtaSaBridge -PathType Leaf)
if ($GtaSaSyncRequired) {
    $nativeArguments = @{ NativeProfilePath = $GtaSaNativeProfile }
    if (Test-Path -LiteralPath $GtaSaInstalledNative -PathType Leaf) {
        $nativeArguments.InstalledNativeProfilePath = $GtaSaInstalledNative
    }
    & (Join-Path $PSScriptRoot 'assert-gtasa-bridge-sync.ps1') `
        -ProfilePath $GtaSaProfile -BridgePath $GtaSaBridge `
        @nativeArguments
}
Assert-InstalledAppClosed

Reset-RepoDirectory $BuildRoot
New-Item -ItemType Directory -Path $RepoStage -Force | Out-Null

$publishArguments = @(
    'publish', (Join-Path $RepoRoot 'client\VrClient.App\VrClient.App.csproj'),
    '-c', 'Release', '-r', 'win-x64', '--self-contained', 'true',
    '-p:PublishSingleFile=true',
    '-p:IncludeNativeLibrariesForSelfExtract=true',
    '-p:EnableCompressionInSingleFile=true',
    '-p:UseSharedCompilation=false',
    '-p:DebugType=None', '-p:DebugSymbols=false',
    '-o', $RepoStage, '--nologo'
)
Invoke-Checked $DotNet $publishArguments 'AVRcade self-contained publish'
$cliPublishArguments = @(
    'publish', (Join-Path $RepoRoot 'client\VrClient.Cli\VrClient.Cli.csproj'),
    '-c', 'Release', '-r', 'win-x64', '--self-contained', 'true',
    '-p:PublishSingleFile=true',
    '-p:IncludeNativeLibrariesForSelfExtract=true',
    '-p:EnableCompressionInSingleFile=true',
    '-p:UseSharedCompilation=false',
    '-p:DebugType=None', '-p:DebugSymbols=false',
    '-o', $RepoStage, '--nologo'
)
Invoke-Checked $DotNet $cliPublishArguments 'AVRcade CLI self-contained publish'
Copy-Item -LiteralPath (Join-Path $RepoRoot 'config') -Destination $RepoStage -Recurse
$FnvAdapterStage = New-Item -ItemType Directory -Path (Join-Path $RepoStage 'adapters\fallout_new_vegas') -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot 'adapters\fallout_new_vegas\adapter.json') -Destination $FnvAdapterStage.FullName
Copy-Item -LiteralPath (Join-Path $RepoRoot 'adapters\fallout_new_vegas\dependency-manifest.schema.json') -Destination $FnvAdapterStage.FullName
$Fallout3AdapterStage = New-Item -ItemType Directory -Path (Join-Path $RepoStage 'adapters\fallout_3') -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot 'adapters\fallout_3\adapter.json') -Destination $Fallout3AdapterStage.FullName
Copy-Item -LiteralPath (Join-Path $RepoRoot 'adapters\fallout_3\fallout3-native-profile-steam-1.7.0.3.ini') -Destination $Fallout3AdapterStage.FullName

$NativeStage = New-Item -ItemType Directory -Path (Join-Path $RepoStage 'native') -Force
$NativeHelperRoot = Join-Path $RepoRoot 'build\package-native'
foreach ($helperName in @('vrclient_safety_cli.exe', 'vrclient_sign_cli.exe')) {
    $helperPath = Join-Path $NativeHelperRoot $helperName
    if (-not (Test-Path -LiteralPath $helperPath)) {
        throw "Required native helper is missing: $helperPath"
    }
    Copy-Item -LiteralPath $helperPath -Destination $NativeStage.FullName
}

$FnvNativeRoot = if ([System.IO.Path]::IsPathRooted($FnvNativeBuildDir)) {
    [System.IO.Path]::GetFullPath($FnvNativeBuildDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $FnvNativeBuildDir))
}
$FnvNativeStage = New-Item -ItemType Directory -Path (Join-Path $NativeStage.FullName 'fallout-new-vegas') -Force
foreach ($fnvName in @('fnv-test-launcher.exe', 'fnv-runtime-check.exe', 'fnv-render-observer.exe', 'vrclient_fnv_stereo.dll', 'openxr_loader.dll')) {
    $fnvPath = Join-Path $FnvNativeRoot $fnvName
    if (-not (Test-Path -LiteralPath $fnvPath)) {
        throw "Required Fallout: New Vegas native release file is missing: $fnvPath"
    }
    Copy-Item -LiteralPath $fnvPath -Destination $FnvNativeStage.FullName
}

# Reuse the app-local VC runtime already produced by the verified Windows
# packaging path when present. The native helpers remain usable on developer
# machines with the runtime installed even when this optional cache is absent.
$PackagedNativeRoot = Join-Path $RepoRoot 'build\package\windows\payload\native'
foreach ($runtimeName in @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')) {
    $runtimePath = Join-Path $PackagedNativeRoot $runtimeName
    if (Test-Path -LiteralPath $runtimePath) {
        Copy-Item -LiteralPath $runtimePath -Destination $NativeStage.FullName
    }
}

$StagedExe = Join-Path $RepoStage 'vrclient-app.exe'
if (-not (Test-Path -LiteralPath $StagedExe)) {
    throw "Published AVRcade executable is missing: $StagedExe"
}
$StagedCli = Join-Path $RepoStage 'vrclient.exe'
if (-not (Test-Path -LiteralPath $StagedCli)) {
    throw "Published AVRcade CLI executable is missing: $StagedCli"
}

Remove-CanonicalDirectory $InstallStage
Remove-CanonicalDirectory $RollbackRoot
New-Item -ItemType Directory -Path $InstallStage -Force | Out-Null
Copy-PublishedFilesWithRetry $RepoStage $InstallStage
if ($GtaSaSyncRequired) {
    $stagedNativeArguments = @{
        NativeProfilePath = Join-Path $InstallStage 'config\legacy\gta-san-andreas-native.json'
    }
    if (Test-Path -LiteralPath $GtaSaInstalledNative -PathType Leaf) {
        $stagedNativeArguments.InstalledNativeProfilePath = $GtaSaInstalledNative
    }
    & (Join-Path $PSScriptRoot 'assert-gtasa-bridge-sync.ps1') `
        -ProfilePath (Join-Path $InstallStage 'config\legacy\gta-san-andreas.json') `
        -BridgePath $GtaSaBridge @stagedNativeArguments
}
# Keep user-retained executable snapshots when refreshing the local install.
if (Test-Path -LiteralPath $InstallRoot) {
    Get-ChildItem -LiteralPath $InstallRoot -File -Filter 'vrclient-app.before-*.exe' |
        Copy-Item -Destination $InstallStage -Force
}

# Smoke-test from the local staging directory. Executables launched directly
# from the network-backed repository can trigger Windows' security warning even
# when they have no zone marker. The current install remains untouched here.
$LocalStagedExe = Join-Path $InstallStage 'vrclient-app.exe'
Test-AppWindow $LocalStagedExe

# Publishing can take minutes. Check again immediately before replacing the
# installed files in case the user opened VRClient during the build.
Assert-InstalledAppClosed
$hadPreviousInstall = Test-Path -LiteralPath $InstallRoot
$oldMoved = $false
$stageMoved = $false
try {
    if ($hadPreviousInstall) {
        Move-Item -LiteralPath (Assert-CanonicalInstallPath $InstallRoot) `
            -Destination (Assert-CanonicalInstallPath $RollbackRoot)
        $oldMoved = $true
    }
    Move-Item -LiteralPath (Assert-CanonicalInstallPath $InstallStage) `
        -Destination (Assert-CanonicalInstallPath $InstallRoot)
    $stageMoved = $true
    Test-AppWindow $InstalledExe
}
catch {
    if ($stageMoved) {
        Remove-CanonicalDirectory $InstallRoot
    }
    if ($oldMoved -and (Test-Path -LiteralPath $RollbackRoot)) {
        Move-Item -LiteralPath (Assert-CanonicalInstallPath $RollbackRoot) `
            -Destination (Assert-CanonicalInstallPath $InstallRoot)
    }
    throw
}

Remove-CanonicalDirectory $RollbackRoot
Set-Shortcut $DesktopShortcut $InstalledExe
Set-Shortcut $StartMenuShortcut $InstalledExe
Assert-ShortcutTarget $DesktopShortcut $InstalledExe
Assert-ShortcutTarget $StartMenuShortcut $InstalledExe
Remove-LegacyShortcut $LegacyDesktopShortcut $InstalledExe
Remove-LegacyShortcut $LegacyStartMenuShortcut $InstalledExe

if (Test-Path -LiteralPath $StatusHelper) {
    Remove-Item -LiteralPath $StatusHelper -Force
}

$result = [ordered]@{
    schema = 'vrclient-local-install/1'
    installed_executable = $InstalledExe
    desktop_shortcut = $DesktopShortcut
    start_menu_shortcut = $StartMenuShortcut
    status_helper_removed = -not (Test-Path -LiteralPath $StatusHelper)
}
$result | ConvertTo-Json -Compress

#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$InstallerPath
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$Installer = (Resolve-Path -LiteralPath $InstallerPath).Path
$SmokeRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot 'build\package\installer-smoke'))
$SmokeParent = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot 'build\package'))
$InstallLog = Join-Path $SmokeParent 'installer-smoke-install.log'
$appProcess = $null

function Assert-SmokePath([string]$Path) {
    $full = [System.IO.Path]::GetFullPath($Path)
    $prefix = $SmokeParent.TrimEnd('\') + '\'
    if (-not $full.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "refusing cleanup outside smoke root: $full"
    }
    return $full
}

function Invoke-Cli([string]$Exe, [string[]]$Arguments, [string]$Expected) {
    $output = & $Exe @Arguments 2>&1
    $code = $LASTEXITCODE
    $text = $output -join "`n"
    if ($code -ne 0 -or $text -notmatch $Expected) {
        throw "CLI gate failed: $Exe $($Arguments -join ' ') exit=$code output=$text"
    }
    return $text
}

$existing = Get-ChildItem 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall' -ErrorAction SilentlyContinue |
    Get-ItemProperty -ErrorAction SilentlyContinue |
    Where-Object { $_.DisplayName -in @('VRClient', 'AVRcade') }
if ($existing -and $existing.InstallLocation -and
    ([System.IO.Path]::GetFullPath($existing.InstallLocation) -ne $SmokeRoot)) {
    throw "A non-smoke AVRcade install already exists at $($existing.InstallLocation); refusing to replace it during verification."
}

$SmokeRoot = Assert-SmokePath $SmokeRoot
if (Test-Path -LiteralPath $SmokeRoot) {
    Remove-Item -LiteralPath $SmokeRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $SmokeParent | Out-Null

$result = [ordered]@{
    schema = 'vrclient-installer-smoke/1'
    installer = $Installer
    installer_sha256 = (Get-FileHash -LiteralPath $Installer -Algorithm SHA256).Hash.ToLowerInvariant()
    install_exit = $null
    required_files = $false
    games_gate = $false
    safety_gate = $false
    fallout_hidden_gate = $false
    app_window = $false
    uninstall_exit = $null
    install_removed = $false
}

try {
    $installArgs = @(
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', '/NOICONS',
        "/DIR=$SmokeRoot", "/LOG=$InstallLog"
    )
    $install = Start-Process -FilePath $Installer -ArgumentList $installArgs -Wait -PassThru
    $result.install_exit = $install.ExitCode
    if ($install.ExitCode -ne 0) { throw "installer exited $($install.ExitCode)" }

    $required = @(
        'vrclient-app.exe', 'vrclient.exe',
        'config\modpacks\repo.modpack.json',
        'config\modpacks\lethal-company.modpack.json',
        'config\modpacks\fallout-new-vegas.dependencies.json',
        'adapters\fallout_new_vegas\adapter.json',
        'native\vrclient_safety_cli.exe',
        'native\vrclient_sign_cli.exe',
        'native\fallout-new-vegas\fnv-test-launcher.exe',
        'native\fallout-new-vegas\fnv-runtime-check.exe',
        'native\fallout-new-vegas\fnv-render-observer.exe',
        'native\fallout-new-vegas\vrclient_fnv_stereo.dll',
        'native\fallout-new-vegas\openxr_loader.dll',
        'native\gta-san-andreas\vrclient_gtasa_theater.asi',
        'native\gta-san-andreas\openxr_loader.dll',
        'config\redengine\native\cyberpunk-2077-backend.json',
        'LICENSE', 'THIRD-PARTY-NOTICES.md', 'licenses\README.txt',
        'native\msvcp140.dll', 'release-manifest.json', 'unins000.exe'
    )
    $missing = @($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $SmokeRoot $_)) })
    if ($missing.Count -gt 0) { throw "installed payload missing: $($missing -join ', ')" }
    # Every licence file the index names must have been installed.
    $licenseDir = Join-Path $SmokeRoot 'licenses'
    $namedLicenses = [regex]::Matches((Get-Content -LiteralPath (Join-Path $licenseDir 'README.txt') -Raw),
        '(?m)\s([\w.-]+\.(?:txt|md))\s*$') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
    $missingLicenses = @($namedLicenses | Where-Object { -not (Test-Path -LiteralPath (Join-Path $licenseDir $_)) })
    if ($namedLicenses.Count -lt 10 -or $missingLicenses.Count -gt 0) {
        throw "installed licence texts incomplete: named=$($namedLicenses.Count) missing=$($missingLicenses -join ', ')"
    }
    $result.required_files = $true

    # The Cyberpunk VR backend is optional in a package, but never partial.
    $cyberpunkBackend = 'absent'
    $cyberpunkPayload = Join-Path $SmokeRoot 'native\cyberpunk-2077'
    if (Test-Path -LiteralPath $cyberpunkPayload) {
        $backendPackage = Get-Content -LiteralPath (Join-Path $SmokeRoot 'config\redengine\native\cyberpunk-2077-backend.json') -Raw |
            ConvertFrom-Json
        foreach ($file in $backendPackage.backend.files) {
            $installedFile = Join-Path $cyberpunkPayload $file.path
            if (-not (Test-Path -LiteralPath $installedFile) -or
                (Get-FileHash -LiteralPath $installedFile -Algorithm SHA256).Hash -ne $file.sha256) {
                throw "installed Cyberpunk VR backend file is missing or changed: $($file.path)"
            }
        }
        $cyberpunkBackend = 'included'
    }

    $savedRepoRoot = $env:VRCLIENT_REPO_ROOT
    try {
        Remove-Item Env:VRCLIENT_REPO_ROOT -ErrorAction SilentlyContinue
        $cli = Join-Path $SmokeRoot 'vrclient.exe'
        $gamesOutput = Invoke-Cli $cli @('games') 'RESULT: games=\d+'
        $result.games_gate = $true
        if ($gamesOutput -match 'GAME: fallout-(3|new-vegas) ') {
            throw "installed CLI exposed an unpublished Fallout game: $gamesOutput"
        }
        $result.fallout_hidden_gate = $true
        [void](Invoke-Cli $cli @('safety', 'repo') 'crosscheck=available:permitted=true')
        $result.safety_gate = $true

        $appProcess = Start-Process -FilePath (Join-Path $SmokeRoot 'vrclient-app.exe') -WorkingDirectory $SmokeRoot -PassThru
        $deadline = [DateTime]::UtcNow.AddSeconds(20)
        do {
            Start-Sleep -Milliseconds 250
            $appProcess.Refresh()
        } while (-not $appProcess.HasExited -and $appProcess.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)
        if ($appProcess.HasExited -or $appProcess.MainWindowHandle -eq 0 -or $appProcess.MainWindowTitle -notlike 'AVRcade*') {
            throw 'installed app did not open an AVRcade window within 20 seconds'
        }
        $result.app_window = $true
        [void]$appProcess.CloseMainWindow()
        if (-not $appProcess.WaitForExit(5000)) { Stop-Process -Id $appProcess.Id -Force }
        $appProcess = $null
    }
    finally {
        if ($null -eq $savedRepoRoot) { Remove-Item Env:VRCLIENT_REPO_ROOT -ErrorAction SilentlyContinue }
        else { $env:VRCLIENT_REPO_ROOT = $savedRepoRoot }
    }

    $uninstaller = Join-Path $SmokeRoot 'unins000.exe'
    $uninstall = Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') -Wait -PassThru
    $result.uninstall_exit = $uninstall.ExitCode
    if ($uninstall.ExitCode -ne 0) { throw "uninstaller exited $($uninstall.ExitCode)" }
    Start-Sleep -Seconds 1
    $result.install_removed = -not (Test-Path -LiteralPath $SmokeRoot)
    if (-not $result.install_removed) { throw "uninstall left the install directory behind: $SmokeRoot" }

    Write-Host "RESULT: installer-smoke ok install=0 files=ok deferred-games=hidden cyberpunk-backend=$cyberpunkBackend safety=ok app=window uninstall=0 removed=true"
}
finally {
    if ($appProcess -and -not $appProcess.HasExited) {
        Stop-Process -Id $appProcess.Id -Force -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath (Join-Path $SmokeRoot 'unins000.exe')) {
        Start-Process -FilePath (Join-Path $SmokeRoot 'unins000.exe') -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') -Wait -ErrorAction SilentlyContinue | Out-Null
    }
    if (Test-Path -LiteralPath $SmokeRoot) {
        $safe = Assert-SmokePath $SmokeRoot
        Remove-Item -LiteralPath $safe -Recurse -Force
    }
    $result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $SmokeParent 'installer-smoke-result.json') -Encoding UTF8
}

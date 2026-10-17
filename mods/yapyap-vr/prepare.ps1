param(
    [ValidateSet('Stage', 'Install', 'Rollback', 'UpdatePlugin')]
    [string]$Action = 'Stage',
    [string]$GameDir = 'W:\SteamLibrary\steamapps\common\YAPYAP',
    [string]$BepInExPackRoot,
    [string]$UuvrBuildRoot,
    [string]$UuvrSourceRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$stageRoot = Join-Path $projectRoot 'artifacts\pre-headset'
$stageFiles = Join-Path $stageRoot 'files'
$stageManifest = Join-Path $stageRoot 'manifest.json'
$installManifest = Join-Path $GameDir 'BepInEx\avrcade-yapyap-vr-install.json'
$expectedUuvrRevision = 'de8e9218f8a9310778f2f57c189bef9ccc8c4dc8'

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Resolve-GameTarget([string]$RelativePath) {
    $parts = $RelativePath.Replace('\', '/').Split('/')
    if ([IO.Path]::IsPathRooted($RelativePath) -or
        @($parts | Where-Object { $_ -in @('', '.', '..') }).Count -gt 0) {
        throw "Invalid stage path: $RelativePath"
    }
    $root = [IO.Path]::GetFullPath($GameDir).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $target = [IO.Path]::GetFullPath((Join-Path $root ($RelativePath.Replace('/', [IO.Path]::DirectorySeparatorChar))))
    if (-not $target.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Stage path leaves the game folder: $RelativePath"
    }
    return $target
}

function Copy-StageTree([string]$SourceRoot, [string]$DestinationPrefix) {
    if (-not (Test-Path -LiteralPath $SourceRoot -PathType Container)) {
        throw "Required build input is missing: $SourceRoot"
    }
    $sourceFull = [IO.Path]::GetFullPath($SourceRoot).TrimEnd('\', '/')
    foreach ($file in Get-ChildItem -LiteralPath $sourceFull -Recurse -File) {
        $relative = $file.FullName.Substring($sourceFull.Length).TrimStart('\', '/')
        $stageRelative = if ($DestinationPrefix) {
            $DestinationPrefix.TrimEnd('\', '/') + '/' + $relative.Replace('\', '/')
        } else { $relative.Replace('\', '/') }
        $target = Join-Path $stageFiles ($stageRelative.Replace('/', [IO.Path]::DirectorySeparatorChar))
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target -Force
    }
}

switch ($Action) {
    'Stage' {
        if (-not $BepInExPackRoot -or -not $UuvrBuildRoot -or -not $UuvrSourceRoot) {
            throw 'Stage requires BepInExPackRoot, UuvrBuildRoot, and UuvrSourceRoot.'
        }
        if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'yapyap.exe')) -or
            -not (Test-Path -LiteralPath (Join-Path $GameDir 'UnityPlayer.dll'))) {
            throw 'The YAPYAP Steam install is missing its executable or UnityPlayer.dll.'
        }
        $revision = (& git -C $UuvrSourceRoot rev-parse HEAD).Trim()
        if ($LASTEXITCODE -ne 0 -or $revision -ne $expectedUuvrRevision) {
            throw "UUVR source revision differs from the reviewed revision: $revision"
        }
        $pluginOutput = Join-Path $projectRoot 'artifacts\Release\netstandard2.1'
        foreach ($name in @('YapyapVrControls.dll', 'YapyapVrControls.Core.dll')) {
            if (-not (Test-Path -LiteralPath (Join-Path $pluginOutput $name))) {
                throw "Build the YAPYAP plugin first: missing $name"
            }
        }
        New-Item -ItemType Directory -Force -Path $stageFiles | Out-Null
        Copy-StageTree $BepInExPackRoot ''
        Copy-StageTree (Join-Path $UuvrBuildRoot 'plugins') 'BepInEx/plugins/AVRcade-UUVR'
        Copy-StageTree (Join-Path $UuvrBuildRoot 'patchers\CopyToGame\Data') 'yapyap_Data'
        Copy-StageTree (Join-Path $UuvrBuildRoot 'patchers\CopyToGame\Plugins\x64') 'yapyap_Data/Plugins'
        Copy-StageTree $pluginOutput 'BepInEx/plugins/AVRcade-YAPYAP-VR'
        $bindingTarget = Join-Path $stageFiles 'yapyap_Data\StreamingAssets\SteamVR\bindings_oculus_touch.json'
        Copy-Item -LiteralPath (Join-Path $projectRoot 'bindings_oculus_touch.json') -Destination $bindingTarget -Force
        Copy-Item -LiteralPath (Join-Path $projectRoot 'bindings_knuckles.json') -Destination (Join-Path $stageFiles 'yapyap_Data\StreamingAssets\SteamVR\bindings_knuckles.json') -Force
        Copy-Item -LiteralPath (Join-Path $projectRoot 'actions.json') -Destination (Join-Path $stageFiles 'yapyap_Data\StreamingAssets\SteamVR\actions.json') -Force
        $licenseTarget = Join-Path $stageFiles 'BepInEx\plugins\AVRcade-UUVR\LICENSE-GPL-3.0.txt'
        Copy-Item -LiteralPath (Join-Path $UuvrSourceRoot 'LICENSE') -Destination $licenseTarget -Force

        $entries = @(Get-ChildItem -LiteralPath $stageFiles -Recurse -File | ForEach-Object {
            [ordered]@{
                path = $_.FullName.Substring($stageFiles.Length).TrimStart('\', '/').Replace('\', '/')
                sha256 = Get-Sha256 $_.FullName
                bytes = $_.Length
            }
        } | Sort-Object path)
        $manifest = [ordered]@{
            schema = 'avrcade-yapyap-pre-headset/1'
            game = 'YAPYAP'
            uuvr_repository = 'https://github.com/Raicuparta/uuvr'
            uuvr_revision = $revision
            headset_verified = $false
            entries = $entries
        }
        [IO.File]::WriteAllText($stageManifest,
            ($manifest | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
        Write-Output "STAGED: $($entries.Count) verified-input files at $stageRoot"
    }
    'Install' {
        if (-not (Test-Path -LiteralPath $stageManifest)) { throw 'Run Stage first.' }
        if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'yapyap.exe'))) {
            throw 'YAPYAP executable not found in GameDir.'
        }
        if (Test-Path -LiteralPath $installManifest) {
            throw 'AVRcade YAPYAP VR is already installed; rollback before reinstalling.'
        }
        $manifest = Get-Content -LiteralPath $stageManifest -Raw | ConvertFrom-Json
        $toCopy = @()
        foreach ($entry in $manifest.entries) {
            $source = Join-Path $stageFiles ($entry.path.Replace('/', [IO.Path]::DirectorySeparatorChar))
            $target = Resolve-GameTarget $entry.path
            if (-not (Test-Path -LiteralPath $source) -or (Get-Sha256 $source) -ne $entry.sha256) {
                throw "Staged file changed: $($entry.path)"
            }
            if (Test-Path -LiteralPath $target) {
                if ((Get-Sha256 $target) -ne $entry.sha256) {
                    throw "Existing game or mod file differs; nothing installed: $($entry.path)"
                }
            } else { $toCopy += $entry }
        }
        $installed = @()
        try {
            foreach ($entry in $toCopy) {
                $source = Join-Path $stageFiles ($entry.path.Replace('/', [IO.Path]::DirectorySeparatorChar))
                $target = Resolve-GameTarget $entry.path
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
                Copy-Item -LiteralPath $source -Destination $target
                $installed += $entry
            }
            $record = [ordered]@{
                schema = 'avrcade-yapyap-install/1'
                uuvr_revision = $manifest.uuvr_revision
                headset_verified = $false
                installed = $installed
            }
            [IO.File]::WriteAllText($installManifest,
                ($record | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
        } catch {
            foreach ($entry in $installed) {
                $target = Resolve-GameTarget $entry.path
                if ((Test-Path -LiteralPath $target) -and (Get-Sha256 $target) -eq $entry.sha256) {
                    Remove-Item -LiteralPath $target -Force
                }
            }
            throw
        }
        Write-Output "INSTALLED: $($installed.Count) files into $GameDir; manifest=$installManifest"
    }
    'UpdatePlugin' {
        # Replaces the bridge DLLs and its Quest binding in an existing install. BepInEx and
        # UUVR stay as installed, so their changed config does not block the update.
        if (-not (Test-Path -LiteralPath $installManifest)) {
            throw "AVRcade YAPYAP VR is not installed; run Stage and Install instead."
        }
        if (Get-Process -Name 'yapyap' -ErrorAction SilentlyContinue) {
            throw 'Close YAPYAP before updating the VR plugin.'
        }
        $pluginOutput = Join-Path $projectRoot 'artifacts\Release\netstandard2.1'
        $record = Get-Content -LiteralPath $installManifest -Raw | ConvertFrom-Json
        $stage = if (Test-Path -LiteralPath $stageManifest) {
            Get-Content -LiteralPath $stageManifest -Raw | ConvertFrom-Json
        } else { $null }
        $updates = @()
        $sources = [ordered]@{
            'BepInEx/plugins/AVRcade-YAPYAP-VR/YapyapVrControls.dll' = Join-Path $pluginOutput 'YapyapVrControls.dll'
            'BepInEx/plugins/AVRcade-YAPYAP-VR/YapyapVrControls.Core.dll' = Join-Path $pluginOutput 'YapyapVrControls.Core.dll'
            'yapyap_Data/StreamingAssets/SteamVR/bindings_oculus_touch.json' = Join-Path $projectRoot 'bindings_oculus_touch.json'
            'yapyap_Data/StreamingAssets/SteamVR/bindings_knuckles.json' = Join-Path $projectRoot 'bindings_knuckles.json'
            'yapyap_Data/StreamingAssets/SteamVR/actions.json' = Join-Path $projectRoot 'actions.json'
        }
        foreach ($relative in $sources.Keys) {
            $source = $sources[$relative]
            if (-not (Test-Path -LiteralPath $source)) { throw "Required update input missing: $source" }
            $entry = @($record.installed | Where-Object { $_.path -eq $relative })
            if ($entry.Count -ne 1) { throw "The install record does not own $relative; nothing updated." }
            $target = Resolve-GameTarget $relative
            if ((Test-Path -LiteralPath $target) -and (Get-Sha256 $target) -ne $entry[0].sha256) {
                throw "Installed file was changed outside AVRcade; nothing updated: $relative"
            }
            $updates += [pscustomobject]@{ Entry = $entry[0]; Source = $source; Target = $target; Relative = $relative }
        }
        foreach ($update in $updates) {
            Copy-Item -LiteralPath $update.Source -Destination $update.Target -Force
            $update.Entry.sha256 = Get-Sha256 $update.Target
            $update.Entry.bytes = (Get-Item -LiteralPath $update.Target).Length
            if ($stage) {
                $staged = Join-Path $stageFiles ($update.Relative.Replace('/', [IO.Path]::DirectorySeparatorChar))
                Copy-Item -LiteralPath $update.Source -Destination $staged -Force
                foreach ($stagedEntry in @($stage.entries | Where-Object { $_.path -eq $update.Relative })) {
                    $stagedEntry.sha256 = $update.Entry.sha256
                    $stagedEntry.bytes = $update.Entry.bytes
                }
            }
        }
        [IO.File]::WriteAllText($installManifest,
            ($record | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
        if ($stage) {
            [IO.File]::WriteAllText($stageManifest,
                ($stage | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
        }
        Write-Output "UPDATED: $($updates.Count) AVRcade bridge files in $GameDir"
    }
    'Rollback' {
        if (-not (Test-Path -LiteralPath $installManifest)) {
            throw "No AVRcade YAPYAP VR install manifest: $installManifest"
        }
        $record = Get-Content -LiteralPath $installManifest -Raw | ConvertFrom-Json
        $remaining = @()
        $removed = 0
        foreach ($entry in $record.installed) {
            $target = Resolve-GameTarget $entry.path
            if (-not (Test-Path -LiteralPath $target)) { continue }
            if ((Get-Sha256 $target) -ne $entry.sha256) {
                $remaining += $entry
                continue
            }
            Remove-Item -LiteralPath $target -Force
            $removed++
        }
        if ($remaining.Count -eq 0) {
            Remove-Item -LiteralPath $installManifest -Force
        } else {
            $record.installed = $remaining
            [IO.File]::WriteAllText($installManifest,
                ($record | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
        }
        Write-Output "ROLLED_BACK: $removed files removed; $($remaining.Count) user-modified files preserved"
    }
}

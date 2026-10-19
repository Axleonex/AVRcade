#Requires -Version 5.1
# Applies the already-published app/CLI and Cyberpunk metadata to the local app.
# Existing game packages for other titles are not part of this update.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$install = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Programs\VRClient'))
if (-not (Test-Path -LiteralPath (Join-Path $install 'vrclient-app.exe'))) {
    throw 'The existing per-user AVRcade install was not found.'
}
if (Get-Process -Name 'vrclient-app' -ErrorAction SilentlyContinue) {
    throw 'Close AVRcade before applying its Cyberpunk update.'
}
$evidence = Join-Path $repo 'artifacts\cyberpunk-upstream-0.1.7'
$backup = Join-Path $install ('.update-backups\cyberpunk-0.1.7-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$files = [ordered]@{
    'vrclient-app.exe' = 'artifacts\cyberpunk-upstream-0.1.7\publish\app\vrclient-app.exe'
    'vrclient.exe' = 'artifacts\cyberpunk-upstream-0.1.7\publish\cli\vrclient.exe'
    'config\redengine\native\cyberpunk-2077-backend.json' = 'config\redengine\native\cyberpunk-2077-backend.json'
    'config\redengine\native\cyberpunk-2077.json' = 'config\redengine\native\cyberpunk-2077.json'
    'config\controller-maps\cyberpunk-2077.json' = 'config\controller-maps\cyberpunk-2077.json'
    'config\mod-credits.json' = 'config\mod-credits.json'
    'config\schemas\redengine-native-profile.schema.json' = 'config\schemas\redengine-native-profile.schema.json'
    'config\schemas\controller-map.schema.json' = 'config\schemas\controller-map.schema.json'
    'licenses\cyberpunk-vr-port-LICENSE.txt' = 'licenses\cyberpunk-vr-port-LICENSE.txt'
    'THIRD-PARTY-NOTICES.md' = 'THIRD-PARTY-NOTICES.md'
}
foreach ($relative in $files.Keys) {
    if (-not (Test-Path -LiteralPath (Join-Path $repo $files[$relative]) -PathType Leaf)) {
        throw "Published update source is missing: $($files[$relative])"
    }
}
$applied = @()
$hashes = [ordered]@{}
try {
    foreach ($relative in $files.Keys) {
        $target = [IO.Path]::GetFullPath((Join-Path $install $relative))
        if (-not $target.StartsWith($install + '\', [StringComparison]::OrdinalIgnoreCase)) {
            throw "Update path leaves the install: $target"
        }
        $old = Join-Path $backup $relative
        $existed = Test-Path -LiteralPath $target -PathType Leaf
        if ($existed) {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $old) | Out-Null
            Copy-Item -LiteralPath $target -Destination $old
        }
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        $source = Join-Path $repo $files[$relative]
        $pending = $target + '.cyberpunk-update-pending'
        Copy-Item -LiteralPath $source -Destination $pending -Force
        $expected = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
        if ((Get-FileHash -LiteralPath $pending -Algorithm SHA256).Hash -ne $expected) {
            throw "Update copy hash mismatch: $relative"
        }
        Move-Item -LiteralPath $pending -Destination $target -Force
        $applied += [pscustomobject]@{ Target = $target; Backup = $old; Existed = $existed }
        $hashes[$relative] = $expected
    }
} catch {
    [array]::Reverse($applied)
    foreach ($item in $applied) {
        if ($item.Existed) { Copy-Item -LiteralPath $item.Backup -Destination $item.Target -Force }
        else { Remove-Item -LiteralPath $item.Target -Force }
    }
    throw
}
$receipt = [ordered]@{
    installed_at = (Get-Date -Format o)
    version = '0.1.7'
    creator = 'dariulone'
    install = $install
    rollback = $backup
    files = $hashes
}
$receipt | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $evidence 'local-app-update.json') -Encoding UTF8
$receipt | ConvertTo-Json -Depth 4 -Compress

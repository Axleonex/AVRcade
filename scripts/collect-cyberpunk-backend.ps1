#Requires -Version 5.1
<#
Stages the Cyberpunk 2077 VR backend files that AVRcade ships, so the packager
and a source checkout can install them into a player's game folder.

Every file is taken by its pinned SHA-256 from
config/redengine/native/cyberpunk-2077-backend.json. Sources, in order:
  1. -GameRoot: a Cyberpunk 2077 folder where the backend is already installed.
  2. This checkout's build output, kept artifacts and config.
Nothing is copied unless its hash matches, so a stale build can never be staged.
Exits 1 and lists what is still missing when the set is incomplete.
#>
[CmdletBinding()]
param(
    [string]$GameRoot,
    [string]$OutDir
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $OutDir) { $OutDir = Join-Path $root 'build\cyberpunk-backend' }
$package = Get-Content -LiteralPath (Join-Path $root 'config\redengine\native\cyberpunk-2077-backend.json') -Raw |
    ConvertFrom-Json

$searchRoots = @('build', 'artifacts\cyberpunk-dependencies', 'external', 'config\redengine') |
    ForEach-Object { Join-Path $root $_ } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Container }
$outFull = [System.IO.Path]::GetFullPath($OutDir).TrimEnd('\') + '\'

function Test-Hash([string]$Path, [string]$Expected) {
    (Test-Path -LiteralPath $Path -PathType Leaf) -and
        (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -eq $Expected
}

$missing = @()
foreach ($file in $package.backend.files) {
    $destination = Join-Path $OutDir $file.path
    if (Test-Hash $destination $file.sha256) {
        Write-Host "staged   $($file.path)"
        continue
    }

    $source = $null
    if ($GameRoot -and (Test-Hash (Join-Path $GameRoot $file.path) $file.sha256)) {
        $source = Join-Path $GameRoot $file.path
    }
    else {
        $name = Split-Path -Leaf $file.path
        $source = $searchRoots |
            ForEach-Object { Get-ChildItem -LiteralPath $_ -Recurse -File -Filter $name -ErrorAction SilentlyContinue } |
            Where-Object { -not $_.FullName.StartsWith($outFull, [System.StringComparison]::OrdinalIgnoreCase) } |
            Where-Object { Test-Hash $_.FullName $file.sha256 } |
            Select-Object -First 1 -ExpandProperty FullName
    }

    if (-not $source) {
        Write-Host "MISSING  $($file.path)"
        $missing += $file.path
        continue
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Force
    Write-Host "copied   $($file.path)"
}

$total = @($package.backend.files).Count
if ($missing.Count -gt 0) {
    Write-Host "RESULT: collect-cyberpunk-backend incomplete staged=$($total - $missing.Count)/$total out=$OutDir"
    Write-Host 'Run again with -GameRoot pointing at a Cyberpunk 2077 folder that has the working VR backend installed.'
    exit 1
}
Write-Host "RESULT: collect-cyberpunk-backend ok staged=$total/$total out=$OutDir"

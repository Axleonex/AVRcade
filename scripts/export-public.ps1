#Requires -Version 5.1
<#
Builds the clean copy of AVRcade that is published to GitHub.

The working repository also holds planning notes, test evidence and hand-off
documents that name the developer's machines and accounts. This script copies
only the publishable files into a separate folder, then scans that folder for
personal identifiers. It exits 1 if the scan finds anything, so a copy that
passes can be committed as-is.

Two git-ignored lists at the repository root drive it, one entry per line:
  .export-private-paths.txt  regular expressions for paths that stay private
  .export-private-terms.txt  words that must not appear in any published file
#>
[CmdletBinding()]
param(
    [string]$OutDir
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if (-not $OutDir) { $OutDir = Join-Path $root 'build\public-export' }

function Read-List([string]$name) {
    $path = Join-Path $root $name
    if (-not (Test-Path -LiteralPath $path)) { return @() }
    @(Get-Content -LiteralPath $path | ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') })
}

# Planning notes, evidence and hand-off documents stay private. Without the list
# nothing could be told apart, so the export refuses to run.
$privatePaths = Read-List '.export-private-paths.txt'
if ($privatePaths.Count -eq 0) { throw 'export-public: .export-private-paths.txt is missing or empty' }
$privatePathPattern = '(' + ($privatePaths -join '|') + ')'

$files = & git -C $root ls-files -co --exclude-standard
if ($LASTEXITCODE -ne 0) { throw 'git ls-files failed' }
$publish = @($files | Where-Object { $_ -notmatch $privatePathPattern })

if (Test-Path -LiteralPath $OutDir) {
    # Keep a .git folder so the export can be re-run over a published checkout.
    Get-ChildItem -LiteralPath $OutDir -Force | Where-Object Name -ne '.git' |
        Remove-Item -Recurse -Force
}
foreach ($relative in $publish) {
    $destination = Join-Path $OutDir $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath (Join-Path $root $relative) -Destination $destination
}

# Personal identifiers that must never be published.
$patterns = [ordered]@{
    'Windows user folder' = '(?i)[A-Z]:[\\/]+Users[\\/]+(?!Public\b|Default\b|<|%|\$|\{|you\b|user\b|name\b|username\b|Alice\b|u\b)[A-Za-z0-9._-]+'
    'Steam account id'    = '\b7656119\d{10}\b'
    'personal e-mail'     = '(?i)[A-Z0-9._%+-]+@(gmail|outlook|hotmail|yahoo|proton|icloud)\.[a-z.]+'
    'private network IP'  = '\b(100\.(6[4-9]|[7-9]\d|1[01]\d|12[0-7])|192\.168|10\.\d{1,3})\.\d{1,3}\.\d{1,3}\b'
    'Windows host name'   = '\b(DESKTOP|LAPTOP)-[A-Z0-9]{7}\b'
}
$terms = Read-List '.export-private-terms.txt'
if ($terms.Count -gt 0) {
    $patterns['private term'] = '(?i)(' + (($terms | ForEach-Object { [regex]::Escape($_) }) -join '|') + ')'
}

$latin1 = [System.Text.Encoding]::GetEncoding(28591)
$hits = @()
foreach ($relative in $publish) {
    # Third-party licence texts carry their authors' own contact details.
    if ($relative -like 'licenses/*') { continue }
    # Every file is scanned, binaries included: a compiled file can embed the path
    # it was built in. Dropping NUL bytes makes UTF-16 strings readable as well.
    $text = $latin1.GetString([System.IO.File]::ReadAllBytes((Join-Path $OutDir $relative))).Replace("`0", '')
    foreach ($name in $patterns.Keys) {
        $match = [regex]::Match($text, $patterns[$name])
        if ($match.Success) {
            $line = ($text.Substring(0, $match.Index) -split "`n").Count
            $hits += "$name  ${relative}:$line"
        }
    }
}

$large = @($publish | Where-Object { (Get-Item -LiteralPath (Join-Path $OutDir $_)).Length -gt 5MB })
$hits | Sort-Object | ForEach-Object { Write-Host "PRIVATE  $_" }
$large | ForEach-Object { Write-Host "LARGE    $_" }
if ($hits.Count -gt 0 -or $large.Count -gt 0) {
    Write-Host "RESULT: export-public blocked files=$($publish.Count) private_hits=$($hits.Count) large=$($large.Count) out=$OutDir"
    exit 1
}
Write-Host "RESULT: export-public ok files=$($publish.Count) private_hits=0 out=$OutDir"

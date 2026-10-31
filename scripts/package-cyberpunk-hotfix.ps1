#Requires -Version 5.1
<# Assemble a managed-code hotfix using an intact 0.5.4 payload as the native baseline.
   Build AppExecutable and CliExecutable from this checkout with dotnet publish first.
   Outputs are local candidates; this script never publishes or installs them. #>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BasePayload,
    [Parameter(Mandatory)][string]$AppExecutable,
    [Parameter(Mandatory)][string]$CliExecutable,
    [ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version = '0.5.5',
    [string]$OutputDirectory,
    [string]$InnoCompiler,
    [switch]$SkipInstaller,
    [switch]$ReleasePackage
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (!$OutputDirectory) { $OutputDirectory = Join-Path $repo 'artifacts\dist\cyberpunk-hotfix' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'Choose a new output directory; existing candidates are preserved.' }
$baseline = [IO.Path]::GetFullPath($BasePayload)
$commit = (git -C $repo rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Could not identify package source commit.' }
if ($ReleasePackage -and (git -C $repo status --porcelain --untracked-files=no)) {
    throw 'Commit tracked source changes before preparing a release package.'
}
$releaseNotes = Join-Path $repo "docs\release\WINDOWS-V$Version.md"
if (!(Test-Path -LiteralPath $releaseNotes -PathType Leaf)) { throw 'Release notes are missing for this version.' }
$manifest = Get-Content -LiteralPath (Join-Path $baseline 'release-manifest.json') -Raw | ConvertFrom-Json
if ($manifest.schema -ne 'vrclient-release/1' -or $manifest.version -ne '0.5.4' -or
    $manifest.commit -ne 'a5dba313d47ff44e42cfe8b5f8b7f3279a9eb114') {
    throw 'The baseline must be the official AVRcade 0.5.4 payload.'
}
function Resolve-Within([string]$Root, [string]$Relative) {
    $prefix = $Root.TrimEnd('\') + '\'
    $path = [IO.Path]::GetFullPath((Join-Path $Root $Relative))
    if (!$path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Invalid payload path: $Relative" }
    return $path
}
foreach ($file in $manifest.files) {
    $source = Resolve-Within $baseline $file.path
    if (!(Test-Path -LiteralPath $source -PathType Leaf) -or
        (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $file.sha256) {
        throw "Baseline hash mismatch: $($file.path)"
    }
}
foreach ($binary in @($AppExecutable, $CliExecutable)) {
    if (!(Test-Path -LiteralPath $binary -PathType Leaf)) { throw "Published binary missing: $binary" }
    if ((Get-Item -LiteralPath $binary).VersionInfo.ProductVersion -notlike "$Version*") {
        throw "Publish the candidate with -p:Version=$Version before packaging: $binary"
    }
    if ($ReleasePackage -and (Get-Item -LiteralPath $binary).VersionInfo.ProductVersion -ne "$Version+$commit") {
        throw "Release binary version/source does not match $Version+$commit. Rebuild from this commit: $binary"
    }
}
if (!$SkipInstaller) {
    if (!$InnoCompiler) { $InnoCompiler = Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe' }
    if (!(Test-Path -LiteralPath $InnoCompiler -PathType Leaf)) { throw 'Specify the Inno Setup compiler with -InnoCompiler.' }
}
$payload = Join-Path $output 'payload'
New-Item -ItemType Directory -Path $payload -Force | Out-Null
foreach ($file in $manifest.files) {
    $target = Resolve-Within $payload $file.path
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath (Resolve-Within $baseline $file.path) -Destination $target
}
Copy-Item -LiteralPath $AppExecutable -Destination (Join-Path $payload 'AVRcade.exe') -Force
Copy-Item -LiteralPath $CliExecutable -Destination (Join-Path $payload 'avrcade-cli.exe') -Force
Copy-Item -LiteralPath (Join-Path $repo 'docs\release\WINDOWS-V0.5.4.md') -Destination (Join-Path $payload 'docs\release\WINDOWS-V0.5.4.md') -Force
Get-ChildItem -LiteralPath (Join-Path $repo 'docs\release') -Filter 'WINDOWS-V0.5*.md' -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $payload ('docs\release\' + $_.Name)) -Force
}
Copy-Item -LiteralPath (Join-Path $repo 'README.md') -Destination (Join-Path $payload 'README.md') -Force
Copy-Item -LiteralPath $releaseNotes -Destination (Join-Path $payload "docs\release\WINDOWS-V$Version.md") -Force
$files = @(Get-ChildItem -LiteralPath $payload -File -Recurse | ForEach-Object {
    [ordered]@{path=$_.FullName.Substring($payload.Length + 1).Replace('\','/'); size=$_.Length;
        sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
} | Sort-Object { $_.path })
$release = [ordered]@{schema='vrclient-release/1'; version=$Version; commit=$commit;
    platform='win-x64'; self_contained=$true; candidate=(!$ReleasePackage);
    baseline_version=$manifest.version; baseline_commit=$manifest.commit; files=$files}
$release | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $payload 'release-manifest.json') -Encoding UTF8
$zip = Join-Path $output "AVRcade-$Version-win-x64.zip"
Compress-Archive -Path (Join-Path $payload '*') -DestinationPath $zip -CompressionLevel Optimal
if (!$SkipInstaller) {
    & $InnoCompiler "/DAppVersion=$Version" "/DSourceRoot=$payload" "/DOutputDir=$output" (Join-Path $repo 'installer\AVRcade.iss')
    if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
}
Copy-Item -LiteralPath $zip -Destination (Join-Path $output 'AVRcade-win-x64.zip')
if (!$SkipInstaller) {
    Copy-Item -LiteralPath (Join-Path $output "AVRcade-Setup-$Version.exe") -Destination (Join-Path $output 'AVRcade-Setup.exe')
}
foreach ($asset in @("AVRcade-$Version-win-x64.zip", "AVRcade-Setup-$Version.exe")) {
    $path = Join-Path $output $asset
    if (!(Test-Path -LiteralPath $path)) { continue }
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    $checksumName = [IO.Path]::GetFileNameWithoutExtension($asset) + '.sha256'
    "$hash  $asset" | Set-Content -LiteralPath (Join-Path $output $checksumName) -Encoding ASCII
}
Get-ChildItem -LiteralPath $output -File | Where-Object {$_.Extension -in '.zip','.exe'} | ForEach-Object {
    "{0}  {1}" -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $_.Name
} | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding ASCII
Write-Output "Local package ready (not published): $output"

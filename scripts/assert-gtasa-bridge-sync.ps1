#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ProfilePath,
    [Parameter(Mandatory = $true)][string]$BridgePath,
    [string]$NativeProfilePath,
    [string]$InstalledNativeProfilePath,
    [string]$GameExecutablePath
)

$ErrorActionPreference = 'Stop'

function Assert-File([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "GTA SA sync check: $Label is missing: $Path"
    }
}

function Assert-Hash([string]$Expected, [string]$Path, [string]$Label) {
    if ($Expected -notmatch '^[0-9a-fA-F]{64}$') {
        throw "GTA SA sync check: $Label has no valid pinned SHA-256 in $ProfilePath"
    }
    Assert-File $Path $Label
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if (-not $actual.Equals($Expected, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "GTA SA sync check: $Label hash mismatch. Profile expects $Expected; $Path is $actual. Update and verify the profile before publishing AVRcade."
    }
}

Assert-File $ProfilePath 'profile'
$profile = Get-Content -LiteralPath $ProfilePath -Raw | ConvertFrom-Json
if ($profile.game.slug -ne 'gta-san-andreas' -or
    $profile.bridge.status -ne 'pinned' -or
    $profile.bridge.architecture -ne 'x86' -or
    $profile.bridge.path -ne 'vrclient_gtasa_theater.asi') {
    throw "GTA SA sync check: unsupported or unpinned bridge profile: $ProfilePath"
}
Assert-Hash $profile.bridge.sha256 $BridgePath 'VR bridge'

if ($GameExecutablePath) {
    Assert-Hash $profile.game.executable_sha256_observed $GameExecutablePath 'game executable'
}
if ($InstalledNativeProfilePath -and -not $NativeProfilePath) {
    throw 'GTA SA sync check: NativeProfilePath is required when InstalledNativeProfilePath is supplied.'
}
if ($NativeProfilePath) {
    Assert-File $NativeProfilePath 'packaged native profile'
    $nativeHash = (Get-FileHash -LiteralPath $NativeProfilePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $bridgeBytes = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($BridgePath))
    if (-not $bridgeBytes.Contains($nativeHash)) {
        throw "GTA SA sync check: VR bridge does not embed native profile hash $nativeHash. Rebuild the ASI against $NativeProfilePath before publishing AVRcade."
    }
    if ($InstalledNativeProfilePath) {
        Assert-Hash $nativeHash $InstalledNativeProfilePath 'installed native profile'
    }
}

Write-Host "GTA SA sync check: passed bridge=$BridgePath profile=$ProfilePath"

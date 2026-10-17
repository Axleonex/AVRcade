param(
    [ValidateSet('Install', 'Remove', 'Status')]
    [string]$Action = 'Status',
    [string]$GameDir = 'W:\SteamLibrary\steamapps\common\YAPYAP'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# QuotaDifficulty 1.0.0 from https://thunderstore.io/c/yapyap/p/psh_games/QuotaDifficulty/
# Its default Original preset leaves gameplay values unchanged.
$expectedSha256 = 'C59215F5A72A697386808043BD46D3EE516A2EB82406CB734C39AE037B7062C5'
$source = Join-Path $PSScriptRoot 'artifacts\extra-mod-test\QuotaDifficulty.dll'
$gameRoot = [IO.Path]::GetFullPath($GameDir).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
$target = [IO.Path]::GetFullPath((Join-Path $gameRoot 'BepInEx\plugins\AVRcade-extra-test\QuotaDifficulty.dll'))
if (-not $target.StartsWith($gameRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Extra mod target leaves the YAPYAP game folder.'
}
if (-not (Test-Path -LiteralPath (Join-Path $gameRoot 'yapyap.exe'))) {
    throw 'YAPYAP executable not found in GameDir.'
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

switch ($Action) {
    'Status' {
        if (-not (Test-Path -LiteralPath $target)) { Write-Output 'EXTRA_MOD: absent'; break }
        if ((Get-Sha256 $target) -eq $expectedSha256) {
            Write-Output 'EXTRA_MOD: installed and hash verified (QuotaDifficulty 1.0.0; default Original)'
        } else {
            Write-Output 'EXTRA_MOD: target exists but differs; manual review needed'
        }
    }
    'Install' {
        if (Get-Process yapyap -ErrorAction SilentlyContinue) { throw 'Close YAPYAP before changing plugins.' }
        if (-not (Test-Path -LiteralPath (Join-Path $gameRoot 'BepInEx\avrcade-yapyap-vr-install.json'))) {
            throw 'Install the AVRcade YAPYAP VR test package first.'
        }
        if (-not (Test-Path -LiteralPath $source) -or (Get-Sha256 $source) -ne $expectedSha256) {
            throw 'The staged QuotaDifficulty DLL is missing or has changed.'
        }
        if (Test-Path -LiteralPath $target) {
            if ((Get-Sha256 $target) -ne $expectedSha256) { throw 'Existing extra mod differs; nothing replaced.' }
            Write-Output 'EXTRA_MOD: already installed and hash verified'
            break
        }
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        Copy-Item -LiteralPath $source -Destination $target
        if ((Get-Sha256 $target) -ne $expectedSha256) { throw 'Extra mod copy failed hash check.' }
        Write-Output 'EXTRA_MOD: installed QuotaDifficulty 1.0.0 alongside AVRcade VR'
    }
    'Remove' {
        if (Get-Process yapyap -ErrorAction SilentlyContinue) { throw 'Close YAPYAP before changing plugins.' }
        if (-not (Test-Path -LiteralPath $target)) { Write-Output 'EXTRA_MOD: already absent'; break }
        if ((Get-Sha256 $target) -ne $expectedSha256) { throw 'Extra mod changed; preserving it for review.' }
        Remove-Item -LiteralPath $target -Force
        Write-Output 'EXTRA_MOD: removed test DLL; VR-only launch is available again'
    }
}

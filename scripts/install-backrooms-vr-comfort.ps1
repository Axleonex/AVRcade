# [PowerShell] powershell -ExecutionPolicy Bypass -File scripts/install-backrooms-vr-comfort.ps1
[CmdletBinding()]
param(
    [string]$ProfileDirectory = (Join-Path $env:APPDATA 'UnrealVRMod\Backrooms-Win64-Shipping'),
    [string]$InstalledCatalogue = (Join-Path $env:LOCALAPPDATA 'Programs\VRClient\config\unreal\escape-the-backrooms.uevr.json'),
    [switch]$SkipInstalledCatalogue,
    # Retained for compatibility; the reviewed title-camera helper installs by default.
    [switch]$EnableExperimentalMenuAnchor
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (Get-Process -Name 'Backrooms', 'Backrooms-Win64-Shipping' -ErrorAction SilentlyContinue) {
    throw 'Close Escape the Backrooms before installing the comfort helper.'
}
$config = Join-Path $ProfileDirectory 'config.txt'
$dll = Join-Path $repo 'build\backrooms-vr\BackroomsTextInput.dll'
$lua = Join-Path $repo 'mods\backrooms-vr\backrooms_names.lua'
$lensLua = Join-Path $repo 'mods\backrooms-vr\backrooms_lens.lua'
$hudLua = Join-Path $repo 'mods\backrooms-vr\backrooms_hud.lua'
$menuLua = Join-Path $repo 'mods\backrooms-vr\backrooms_menu.lua'
$requiredFiles = @($config, $dll, $lua, $lensLua, $hudLua, $menuLua)
foreach ($required in $requiredFiles) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing required file: $required" }
}
$backup = Join-Path $ProfileDirectory ('comfort-backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $backup | Out-Null
$utf8 = New-Object System.Text.UTF8Encoding($false)
function Backup-File([string]$source, [string]$relative) {
    if (Test-Path -LiteralPath $source -PathType Leaf) {
        $destination = Join-Path $backup $relative
        New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination
    }
}
Backup-File $config 'config.txt'
$text = [IO.File]::ReadAllText($config)
$settings = @{
    UI_Distance = '1.500000'; UI_Size = '1.000000'
    UI_FollowView = 'false'; UI_Framework_FollowView = 'false'
    VR_DecoupledPitchUIAdjust = 'false'
    FrameworkConfig_EnableL3R3Toggle = 'false'
}
foreach ($entry in $settings.GetEnumerator()) {
    $key = $entry.Key
    $value = $entry.Value
    if ($text -match "(?m)^$key=") {
        $text = [regex]::Replace($text, "(?m)^$key=[^\r\n]*", "$key=$value")
    } else {
        $text = $text.TrimEnd("`r", "`n") + "`r`n$key=$value`r`n"
    }
}
[IO.File]::WriteAllText($config, $text, $utf8)
$installFiles = @(@($dll, 'plugins\BackroomsTextInput.dll'), @($lua, 'scripts\backrooms_names.lua'), @($lensLua, 'scripts\backrooms_lens.lua'), @($hudLua, 'scripts\backrooms_hud.lua'), @($menuLua, 'scripts\backrooms_menu.lua'))
foreach ($item in $installFiles) {
    $target = Join-Path $ProfileDirectory $item[1]
    Backup-File $target $item[1]
    New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $item[0] -Destination $target -Force
}
# The older installed catalogue lacked these overrides. Keep its pinned upstream
# hashes intact; only add the local comfort settings already in the source catalogue.
if (-not $SkipInstalledCatalogue -and (Test-Path -LiteralPath $InstalledCatalogue -PathType Leaf)) {
    Backup-File $InstalledCatalogue 'installed-catalogue.json'
    $catalogue = [IO.File]::ReadAllText($InstalledCatalogue)
    $parsed = $catalogue | ConvertFrom-Json
    if ($parsed.game_slug -ne 'escape-the-backrooms') { throw 'Refusing to change a different game catalogue.' }
    $overrides = '"config_overrides": ' + ($settings | ConvertTo-Json -Compress)
    if ($parsed.uevr_profile.PSObject.Properties.Name -contains 'config_overrides') {
        # Preserve other overrides when updating an existing object.
        foreach ($entry in $settings.GetEnumerator()) {
            $parsed.uevr_profile.config_overrides | Add-Member -NotePropertyName $entry.Key -NotePropertyValue $entry.Value -Force
        }
        $catalogue = $parsed | ConvertTo-Json -Depth 40
    } else {
        $catalogue = [regex]::Replace($catalogue, '("uevr_profile"\s*:\s*\{)', ('$1' + "`r`n    $overrides,"))
    }
    $null = $catalogue | ConvertFrom-Json
    [IO.File]::WriteAllText($InstalledCatalogue, $catalogue, $utf8)
}
Write-Output "Installed Backrooms keyboard and fixed menu. Menu size=1.0, distance=1.5. Backup: $backup"
Write-Output 'Left stick selects keys; right A types; right B or the large DONE button finishes. Left lower erases; left upper shifts or reopens. Single-button message popup: right A or right B confirms. Both-stick settings shortcut disabled for Backrooms.'
Write-Output 'VR lens guard installed: local VHS barrel distortion disabled while the headset is active.'
Write-Output 'Gameplay HUD comfort installed: reviewed indicator groups form a central cluster at 85% rendered size. Crosshair and interactive menus retain their placement.'
Write-Output 'Menu room and panel anchor once per menu session, including pause/settings, lobby forms and dialogs. The reference stays held through both eye renders. Full stereo head tracking remains active; normal gameplay aiming resumes when menus close. Additional-screen headset verification remains pending.'

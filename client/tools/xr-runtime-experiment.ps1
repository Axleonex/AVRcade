param(
    [Parameter(Mandatory)][string]$Name,
    [string]$CfgPinValue = $null,      # value for [Internal] OpenXRRuntimeFile; $null = leave untouched, "" = clear
    [string]$EnvRuntimeJson = $null,   # value for XR_RUNTIME_JSON on the game process
    [int]$WaitSeconds = 60
)
$ErrorActionPreference = "Stop"
$gameDir = "C:\Program Files (x86)\Steam\steamapps\common\REPO"
$cfg = Join-Path $gameDir "BepInEx\config\io.daxcess.repoxr.cfg"
$log = Join-Path $gameDir "BepInEx\LogOutput.log"
$outDir = "artifacts\steamvr-fix\exp-$Name"
New-Item -ItemType Directory -Force $outDir | Out-Null

# Ensure verbose loader logging
(Get-Content $cfg) -replace '^ExtendedDebugging = .*$', 'ExtendedDebugging = true' | Set-Content $cfg

if ($null -ne $CfgPinValue) {
    (Get-Content $cfg) -replace '^OpenXRRuntimeFile = .*$', "OpenXRRuntimeFile = $CfgPinValue" | Set-Content $cfg
}
Copy-Item $cfg (Join-Path $outDir "cfg-before-launch.txt")

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = Join-Path $gameDir "REPO.exe"
$psi.WorkingDirectory = $gameDir
$psi.UseShellExecute = $false
if ($EnvRuntimeJson) { $psi.Environment["XR_RUNTIME_JSON"] = $EnvRuntimeJson }
$p = [System.Diagnostics.Process]::Start($psi)
Start-Sleep -Seconds $WaitSeconds
try { Stop-Process -Id $p.Id -Force -ErrorAction Stop } catch {}
Start-Sleep -Seconds 3

Copy-Item $log (Join-Path $outDir "LogOutput.log")
Copy-Item $cfg (Join-Path $outDir "cfg-after-exit.txt")
$attempts = Select-String -Path $log -Pattern 'Attempting to initialize|OpenXR runtime being used|OpenXR Loader' | ForEach-Object { $_.Line.Trim() }
$attempts | Set-Content (Join-Path $outDir "attempt-lines.txt")
Write-Output "EXPERIMENT $Name COMPLETE"
$attempts | Select-Object -First 12 | Write-Output

#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$AttachLoader,
    [Parameter(Mandatory = $true)][string]$HostExe,
    [Parameter(Mandatory = $true)][string]$PayloadDll,
    [Parameter(Mandatory = $true)][string]$EvidenceFile,
    [int]$AttachDelayMilliseconds = 750,
    [switch]$RequireHeadsetAttempt
)

$ErrorActionPreference = 'Stop'
foreach ($path in @($AttachLoader, $HostExe, $PayloadDll)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required late-attach test path is missing: $path"
    }
}
if (Test-Path -LiteralPath $EvidenceFile) {
    Remove-Item -LiteralPath $EvidenceFile -Force
}
$evidenceDir = Split-Path -Parent $EvidenceFile
New-Item -ItemType Directory -Path $evidenceDir -Force | Out-Null

$hostInfo = [System.Diagnostics.ProcessStartInfo]::new()
$hostInfo.FileName = [System.IO.Path]::GetFullPath($HostExe)
$hostInfo.Arguments = '--continuous'
$hostInfo.WorkingDirectory = Split-Path -Parent $hostInfo.FileName
$hostInfo.UseShellExecute = $false
$hostProcess = [System.Diagnostics.Process]::new()
$hostProcess.StartInfo = $hostInfo
if (-not $hostProcess.Start()) {
    throw 'Failed to start the controlled late-attach host'
}
try {
    Start-Sleep -Milliseconds $AttachDelayMilliseconds
    & $AttachLoader --pid $hostProcess.Id --dll $PayloadDll --evidence $EvidenceFile
    if ($LASTEXITCODE -ne 0) {
        throw "Late-attach loader exited $LASTEXITCODE"
    }

    $deadline = (Get-Date).AddSeconds(12)
    do {
        Start-Sleep -Milliseconds 200
        if (Test-Path -LiteralPath $EvidenceFile) {
            try {
                $evidence = Get-Content -LiteralPath $EvidenceFile -Raw |
                    ConvertFrom-Json
                $headsetResultFieldsPresent =
                    $evidence.PSObject.Properties.Name -contains 'runtime_frame_attempted' -and
                    $evidence.PSObject.Properties.Name -contains 'runtime_frame_result'
                $headsetAttemptSatisfied =
                    -not $RequireHeadsetAttempt -or
                    ($evidence.runtime_start_attempted -eq $true -and
                     $headsetResultFieldsPresent)
                if ($evidence.ready -and $evidence.present_count -ge 4 -and
                    $headsetAttemptSatisfied) {
                    Write-Host 'PASS: late-attached observer discovered the existing D3D12 presentation path'
                    exit 0
                }
            } catch {
            }
        }
    } while ((Get-Date) -lt $deadline)
    throw 'Late-attached observer did not produce ready evidence'
} finally {
    if (-not $hostProcess.HasExited) {
        Stop-Process -Id $hostProcess.Id -Force
        $hostProcess.WaitForExit(5000)
    }
}

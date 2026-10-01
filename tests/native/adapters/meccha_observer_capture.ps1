#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$WithDll,
    [Parameter(Mandatory = $true)][string]$HostExe,
    [Parameter(Mandatory = $true)][string]$PayloadDll,
    [Parameter(Mandatory = $true)][string]$EvidenceFile
)

$ErrorActionPreference = 'Stop'

foreach ($path in @($WithDll, $HostExe, $PayloadDll)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required observer test path is missing: $path"
    }
}

if (Test-Path -LiteralPath $EvidenceFile) {
    Remove-Item -LiteralPath $EvidenceFile -Force
}
$evidenceDir = Split-Path -Parent $EvidenceFile
if ($evidenceDir) {
    New-Item -ItemType Directory -Path $evidenceDir -Force | Out-Null
}

$previousEvidence = $env:VRCLIENT_MECCHA_OBSERVER_EVIDENCE
try {
    $env:VRCLIENT_MECCHA_OBSERVER_EVIDENCE = [System.IO.Path]::GetFullPath($EvidenceFile)
    & $WithDll "/d:$PayloadDll" $HostExe
    if ($LASTEXITCODE -ne 0) {
        throw "Observer fixture exited $LASTEXITCODE"
    }
} finally {
    $env:VRCLIENT_MECCHA_OBSERVER_EVIDENCE = $previousEvidence
}

if (-not (Test-Path -LiteralPath $EvidenceFile)) {
    throw "Observer did not write evidence"
}

$evidence = Get-Content -LiteralPath $EvidenceFile -Raw | ConvertFrom-Json
if (-not $evidence.ready) { throw "Observer never reached ready state" }
if ($evidence.renderer -ne 'd3d12') { throw "Observer renderer was not d3d12" }
if ($evidence.present_count -lt 4) { throw "Observer missed Present calls" }
if ($evidence.resize_count -lt 1) { throw "Observer missed ResizeBuffers" }
if ($evidence.width -ne 800 -or $evidence.height -ne 450) {
    throw "Observer did not refresh resized swapchain dimensions"
}
if ($evidence.buffer_count -ne 2) { throw "Observer buffer count mismatch" }

Write-Host 'PASS: transparent D3D12 observer captured device, queue, swapchain, Present, and ResizeBuffers'

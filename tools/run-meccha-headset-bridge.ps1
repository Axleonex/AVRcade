#Requires -Version 5.1
[CmdletBinding()]
param(
    [switch]$PrivateOfflineConfirmed,
    [switch]$SceneRelay,
    [switch]$OrientationOnly,
    [switch]$Stereo,
    [switch]$LocalEvidence,
    [string]$BuildDir = 'build/meccha-headset-bridge',
    [int]$EvidenceTimeoutSeconds = 120,
    [int]$MinimumStableFrames = 300,
    [int]$MinimumStableOrientationApplications = 60
)

$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Set-Location -LiteralPath $root

function Stop-WithBlocker {
    param([Parameter(Mandatory = $true)][string]$Message)
    Write-Host "RESULT: BLOCKED - $Message" -ForegroundColor Red
    exit 20
}

if (-not $PrivateOfflineConfirmed) {
    Stop-WithBlocker 'pass -PrivateOfflineConfirmed only for a private/offline Meccha headset test'
}
if ($MinimumStableFrames -lt 60) {
    Stop-WithBlocker 'MinimumStableFrames must be at least 60'
}
if ($MinimumStableOrientationApplications -lt 30) {
    Stop-WithBlocker 'MinimumStableOrientationApplications must be at least 30'
}
$useSceneRelay = $SceneRelay -or $OrientationOnly
$modeCount = @($SceneRelay, $OrientationOnly, $Stereo) |
    Where-Object { $_ } |
    Measure-Object |
    Select-Object -ExpandProperty Count
if ($modeCount -gt 1) {
    Stop-WithBlocker 'SceneRelay, OrientationOnly, and Stereo are mutually exclusive'
}
$useSceneRelay = $useSceneRelay -or $Stereo
$requiresOrientation = $OrientationOnly -or $Stereo

$runningGame = @(Get-Process -ErrorAction SilentlyContinue |
    Where-Object { $_.ProcessName -match '^PenguinHotel($|-Win64-Shipping$)' })
if ($runningGame.Count -gt 0) {
    Stop-WithBlocker 'Meccha is already running; close it normally before this headset test'
}

$blockedProcesses = @(Get-Process -ErrorAction SilentlyContinue |
    Where-Object {
        $_.ProcessName -match
            'EasyAntiCheat|BEService|BattlEye|UEVR|UnrealVRMod|VRClient'
    })
if ($blockedProcesses.Count -gt 0) {
    Stop-WithBlocker (
        'an anti-cheat or external-injector process is active: ' +
        (($blockedProcesses | Select-Object -ExpandProperty ProcessName -Unique) -join ', ')
    )
}

$blockedServices = @(Get-CimInstance Win32_Service -ErrorAction SilentlyContinue |
    Where-Object {
        $_.State -eq 'Running' -and
        ($_.Name -match 'EasyAntiCheat|BEService|BattlEye' -or
         $_.DisplayName -match 'Easy Anti-Cheat|BattlEye')
    })
if ($blockedServices.Count -gt 0) {
    Stop-WithBlocker (
        'an anti-cheat service is running: ' +
        (($blockedServices | Select-Object -ExpandProperty Name -Unique) -join ', ')
    )
}

$preflightOutput = @(
    dotnet run --project client\VrClient.Cli\VrClient.Cli.csproj -- `
        native-preflight meccha-chameleon 2>&1
)
if ($LASTEXITCODE -ne 0) {
    $preflightOutput | ForEach-Object { Write-Host $_ }
    Stop-WithBlocker 'exact Meccha build/hash preflight failed'
}
$executableLine = $preflightOutput |
    Where-Object { $_ -like 'NATIVE-PREFLIGHT: executable=*' } |
    Select-Object -Last 1
if (-not $executableLine) {
    Stop-WithBlocker 'preflight did not resolve the shipping executable'
}
$shippingExe = $executableLine.Substring('NATIVE-PREFLIGHT: executable='.Length)

$withDll = Join-Path $root 'Detours-4.0.1\Detours-4.0.1\bin.X64\withdll.exe'
if (-not (Test-Path -LiteralPath $withDll)) {
    Stop-WithBlocker 'the approved user-built Detours files are unavailable'
}
$openxrConfig = Get-ChildItem -LiteralPath (Join-Path $root 'external\openxr') `
    -Recurse -Filter 'OpenXRConfig.cmake' -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $openxrConfig) {
    Stop-WithBlocker 'the project-local OpenXR loader has not been built'
}

Set-ExecutionPolicy -Scope Process Bypass -Force
. (Join-Path $root 'tools\setup-build-env.ps1') -Compiler msvc -Quiet
$resolvedBuildDir = [System.IO.Path]::GetFullPath((Join-Path $root $BuildDir))
& $env:VRCLIENT_CMAKE_EXE -S $root -B $resolvedBuildDir -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$($env:VRCLIENT_NINJA_EXE)" `
    -DCMAKE_BUILD_TYPE=Release `
    -DVRCLIENT_BUILD_OPENXR_RUNTIME=ON `
    -DVRCLIENT_BUILD_TESTS=ON `
    -DVRCLIENT_ENABLE_DETOURS_SMOKE=ON `
    "-DVRCLIENT_DETOURS_WITHDLL=$($withDll.Replace('\', '/'))" `
    "-DCMAKE_PREFIX_PATH=$((Join-Path $root 'external\openxr').Replace('\', '/'))"
if ($LASTEXITCODE -ne 0) {
    Stop-WithBlocker 'headset bridge configure failed'
}
$payloadTarget = if ($OrientationOnly) {
    'vrclient_meccha_orientation_payload'
} elseif ($Stereo) {
    'vrclient_meccha_stereo_payload'
} elseif ($SceneRelay) {
    'vrclient_meccha_scene_relay_payload'
} else {
    'vrclient_meccha_headset_payload'
}
& $env:VRCLIENT_CMAKE_EXE --build $resolvedBuildDir `
    --target $payloadTarget vrclient_dll_attach_loader
if ($LASTEXITCODE -ne 0) {
    Stop-WithBlocker 'headset bridge build failed'
}

$payloadName = if ($OrientationOnly) {
    'vrclient_meccha_orientation_payload.dll'
} elseif ($Stereo) {
    'vrclient_meccha_stereo_payload.dll'
} elseif ($SceneRelay) {
    'vrclient_meccha_scene_relay_payload.dll'
} else {
    'vrclient_meccha_headset_payload.dll'
}
$payload = Join-Path $resolvedBuildDir $payloadName
$attachLoader = Join-Path $resolvedBuildDir 'vrclient_dll_attach_loader.exe'
if (-not (Test-Path -LiteralPath $payload) -or
    -not (Test-Path -LiteralPath $attachLoader)) {
    Stop-WithBlocker 'headset payload or attach loader was not produced'
}

$timestamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
$artifactLane = if ($OrientationOnly) {
    'meccha-orientation'
} elseif ($Stereo) {
    'meccha-stereo'
} elseif ($SceneRelay) {
    'meccha-scene-relay'
} else {
    'meccha-headset'
}
$artifactRoot = if ($LocalEvidence) {
    Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) `
        'VRClient\artifacts'
} else {
    Join-Path $root 'artifacts'
}
$artifactDir = Join-Path $artifactRoot "$artifactLane\$timestamp"
New-Item -ItemType Directory -Path $artifactDir -Force | Out-Null
$evidenceFile = Join-Path $artifactDir 'session-evidence.json'
$preflightOutput | Set-Content -LiteralPath (Join-Path $artifactDir 'preflight.log')

$steamLaunchUri = if ($Stereo) {
    'steam://run/4704690//-emulatestereo/'
} else {
    'steam://run/4704690'
}
if ($Stereo) {
    Write-Host `
        'STEAM: approve the launch-options prompt on the desktop to start Meccha.' `
        -ForegroundColor Yellow
}
Start-Process $steamLaunchUri
$shippingProcess = $null
$launchDeadline = (Get-Date).AddSeconds(120)
do {
    Start-Sleep -Milliseconds 500
    $shippingProcess = Get-Process -ErrorAction SilentlyContinue |
        Where-Object { $_.ProcessName -eq 'PenguinHotel-Win64-Shipping' } |
        Select-Object -First 1
} while (-not $shippingProcess -and (Get-Date) -lt $launchDeadline)
if (-not $shippingProcess) {
    Stop-WithBlocker 'Steam did not start the Meccha shipping process within 120 seconds'
}
if (-not $shippingProcess.Path -or
    -not [System.IO.Path]::GetFullPath($shippingProcess.Path).Equals(
        [System.IO.Path]::GetFullPath($shippingExe),
        [System.StringComparison]::OrdinalIgnoreCase)) {
    Stop-WithBlocker "Steam started an unexpected shipping process: $($shippingProcess.Path)"
}

$postLaunchBlocked = @(Get-Process -ErrorAction SilentlyContinue |
    Where-Object {
        $_.ProcessName -match
            'EasyAntiCheat|BEService|BattlEye|UEVR|UnrealVRMod'
    })
$postLaunchBlockedServices = @(Get-CimInstance Win32_Service -ErrorAction SilentlyContinue |
    Where-Object {
        $_.State -eq 'Running' -and
        ($_.Name -match 'EasyAntiCheat|BEService|BattlEye' -or
         $_.DisplayName -match 'Easy Anti-Cheat|BattlEye')
    })
if ($postLaunchBlocked.Count -gt 0 -or
    $postLaunchBlockedServices.Count -gt 0) {
    Stop-WithBlocker 'an anti-cheat or external-injector signal appeared after Steam launch'
}

# The headset payload is substantially larger than the flat observer and starts
# a graphics runtime. Do not load it into the shipping process while Unreal is
# still creating/resizing its startup swapchain. The controlled regression
# reproduces D3D12 device removal when attachment overlaps that window and stays
# healthy when attachment happens after it.
Write-Host 'HEADSET: waiting for the Unreal startup swapchain to settle...'
Start-Sleep -Seconds 10
if ($shippingProcess.HasExited) {
    Stop-WithBlocker 'Meccha exited during the startup-settle interval'
}
$settledBlockedProcesses = @(Get-Process -ErrorAction SilentlyContinue |
    Where-Object {
        $_.ProcessName -match
            'EasyAntiCheat|BEService|BattlEye|UEVR|UnrealVRMod'
    })
$settledBlockedServices = @(Get-CimInstance Win32_Service -ErrorAction SilentlyContinue |
    Where-Object {
        $_.State -eq 'Running' -and
        ($_.Name -match 'EasyAntiCheat|BEService|BattlEye' -or
         $_.DisplayName -match 'Easy Anti-Cheat|BattlEye')
    })
if ($settledBlockedProcesses.Count -gt 0 -or
    $settledBlockedServices.Count -gt 0) {
    Stop-WithBlocker 'an anti-cheat or external-injector signal appeared before headset attachment'
}

$attachOutput = @(
    & $attachLoader --pid $shippingProcess.Id --dll $payload `
        --evidence $evidenceFile 2>&1
)
$attachOutput | Set-Content -LiteralPath (Join-Path $artifactDir 'attach.log')
if ($LASTEXITCODE -ne 0) {
    $attachOutput | ForEach-Object { Write-Host $_ }
    Stop-WithBlocker 'headset bridge attach failed'
}

$deadline = (Get-Date).AddSeconds($EvidenceTimeoutSeconds)
do {
    Start-Sleep -Milliseconds 500
    if ($shippingProcess.HasExited) {
        Stop-WithBlocker 'Meccha exited before headset evidence was ready'
    }
    if (Test-Path -LiteralPath $evidenceFile) {
        try {
            $evidence = Get-Content -LiteralPath $evidenceFile -Raw |
                ConvertFrom-Json
            if ($evidence.runtime_start_attempted -eq $true -and
                $evidence.runtime_start_result -lt 0) {
                Stop-WithBlocker (
                    'OpenXR/D3D12 startup failed with VrRuntimeResult ' +
                    $evidence.runtime_start_result
                )
            }
            if ($evidence.runtime_frame_attempted -eq $true -and
                $evidence.runtime_frame_result -lt 0) {
                Stop-WithBlocker (
                    'Headset frame submission failed with VrRuntimeResult ' +
                    $evidence.runtime_frame_result
                )
            }
            if ($requiresOrientation -and
                $evidence.orientation_status -and
                $evidence.orientation_status -notin @('starting', 'ready')) {
                Stop-WithBlocker (
                    'Orientation-only camera hook failed closed with status ' +
                    $evidence.orientation_status
                )
            }
            if ($requiresOrientation -and
                $evidence.orientation_tracking_fault_latched -eq $true) {
                Stop-WithBlocker 'orientation tracking lost freshness after activation; writes are latched off for this launch'
            }
            if ($requiresOrientation -and
                $evidence.orientation_pose_sample_result -lt 0) {
                Stop-WithBlocker (
                    'independent OpenXR pose sampling failed with VrRuntimeResult ' +
                    $evidence.orientation_pose_sample_result
                )
            }
            if ($Stereo -and
                $evidence.orientation_stable_camera_id -ne 0 -and
                $evidence.orientation_secondary_stable_camera_id -ne 0 -and
                $evidence.orientation_observed_vertical_fov_degrees -gt 0 -and
                ([Math]::Abs(
                        $evidence.orientation_observed_vertical_fov_degrees -
                            110.0
                    ) -gt 2.0 -or
                 [Math]::Abs(
                        $evidence.orientation_observed_aspect_ratio - 1.0
                    ) -gt 0.01)) {
                Stop-WithBlocker (
                    'Meccha did not expose the required symmetric 110-degree ' +
                    'stereo source projection; observed vertical FOV=' +
                    $evidence.orientation_observed_vertical_fov_degrees +
                    ' aspect=' + $evidence.orientation_observed_aspect_ratio
                )
            }
            $orientationReady = -not $requiresOrientation -or (
                $evidence.orientation_status -eq 'ready' -and
                $evidence.orientation_signature_verified -eq $true -and
                $evidence.orientation_hook_installed -eq $true -and
                $evidence.orientation_stable_camera_id -ne 0 -and
                $evidence.orientation_pose_sample_valid -eq $true -and
                $evidence.orientation_pose_sampler_active -eq $true -and
                $evidence.orientation_pose_sample_success_count -ge 60 -and
                $evidence.orientation_tracking_ready -eq $true -and
                $evidence.orientation_tracking_fault_latched -ne $true -and
                $evidence.orientation_apply_count -ge
                    $MinimumStableOrientationApplications -and
                $evidence.consecutive_orientation_apply_count -ge
                    $MinimumStableOrientationApplications
            )
            $stereoReady = -not $Stereo -or (
                $evidence.scene_source_layout -eq 'side_by_side_stereo' -and
                $evidence.flat_output_restored_from_eye -eq 'left' -and
                $evidence.orientation_stable_camera_id -ne 0 -and
                $evidence.orientation_secondary_stable_camera_id -ne 0 -and
                $evidence.orientation_secondary_stable_camera_id -ne
                    $evidence.orientation_stable_camera_id -and
                $evidence.orientation_stereo_pair_snapshot_available -eq $true -and
                $evidence.orientation_stereo_completed_pair_count -ge 30 -and
                $evidence.orientation_stereo_incomplete_pair_count -le 1 -and
                $evidence.orientation_stereo_pose_frame_mismatch_count -eq 0 -and
                $evidence.orientation_write_reject_count -eq 0 -and
                $evidence.orientation_stereo_pair_render_generation -gt 0 -and
                $evidence.orientation_stereo_eye_0_camera_id -eq
                    $evidence.orientation_stable_camera_id -and
                $evidence.orientation_stereo_eye_1_camera_id -eq
                    $evidence.orientation_secondary_stable_camera_id -and
                [Math]::Abs(
                    $evidence.orientation_observed_vertical_fov_degrees - 110.0
                ) -le 2.0 -and
                [Math]::Abs(
                    $evidence.orientation_observed_aspect_ratio - 1.0
                ) -le 0.01
            )
            if ($evidence.headset_ready -and
                $evidence.headset_frame_count -ge $MinimumStableFrames -and
                $evidence.eye_count -eq 2 -and
                (-not $useSceneRelay -or $evidence.scene_capture_count -ge 2) -and
                $orientationReady -and
                $stereoReady) {
                Write-Host "HEADSET: frames=$($evidence.headset_frame_count)"
                Write-Host "HEADSET: eyes=$($evidence.eye_count)"
                Write-Host "HEADSET: orientation_valid=$($evidence.head_pose_orientation_valid)"
                if ($useSceneRelay) {
                    Write-Host "SCENE: captures=$($evidence.scene_capture_count)"
                }
                if ($Stereo) {
                    Write-Host "STEREO: source_layout=$($evidence.scene_source_layout)"
                    Write-Host "STEREO: camera_ids=$($evidence.orientation_stable_camera_id),$($evidence.orientation_secondary_stable_camera_id)"
                    Write-Host "STEREO: coherent_pairs=$($evidence.orientation_stereo_completed_pair_count)"
                    Write-Host "STEREO: incomplete_pairs=$($evidence.orientation_stereo_incomplete_pair_count)"
                    Write-Host "STEREO: duplicate_eye_views=$($evidence.orientation_stereo_duplicate_eye_count)"
                    Write-Host "STEREO: pose_frame_mismatches=$($evidence.orientation_stereo_pose_frame_mismatch_count)"
                    Write-Host "STEREO: eye_call_delta_ns=$($evidence.orientation_stereo_eye_observation_delta_ns)"
                    Write-Host "STEREO: source_eye_separation_uu=$($evidence.orientation_stereo_eye_separation_uu)"
                    Write-Host "FLAT: restored_eye=$($evidence.flat_output_restored_from_eye)"
                }
                if ($requiresOrientation) {
                    Write-Host "CAMERA: stable_id=$($evidence.orientation_stable_camera_id)"
                    Write-Host "CAMERA: orientation_applications=$($evidence.orientation_apply_count)"
                    Write-Host "CAMERA: consecutive=$($evidence.consecutive_orientation_apply_count)"
                    Write-Host "CAMERA: missing_pose_skips=$($evidence.orientation_missing_pose_skip_count)"
                    Write-Host "CAMERA: stale_pose_skips=$($evidence.orientation_stale_pose_skip_count)"
                    Write-Host "CAMERA: pose_sample_attempts=$($evidence.orientation_pose_sample_attempt_count)"
                    Write-Host "CAMERA: pose_sample_successes=$($evidence.orientation_pose_sample_success_count)"
                }
                Write-Host "FLAT: size=$($evidence.width)x$($evidence.height)"
                Write-Host "EVIDENCE: $artifactDir"
                if ($OrientationOnly) {
                    Write-Host 'RESULT: READY - headset rotation should move the relayed Meccha view; position and stereoscopic depth are intentionally unchanged'
                } elseif ($Stereo) {
                    Write-Host 'RESULT: READY - engine-generated side-by-side views are routed to distinct headset eyes; verify depth, scale, and flat-output stability'
                } elseif ($SceneRelay) {
                    Write-Host 'RESULT: READY - the same live Meccha scene should appear in both eyes; verify the flat game is still normal'
                } else {
                    Write-Host 'RESULT: READY - left eye should be red and right eye blue; verify the flat game is still normal'
                }
                exit 0
            }
        } catch {
            # A read can land between CreateFile and FlushFileBuffers.
        }
    }
} while ((Get-Date) -lt $deadline)

Stop-WithBlocker "no ready headset evidence appeared within $EvidenceTimeoutSeconds seconds"

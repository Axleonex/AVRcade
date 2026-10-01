#Requires -Version 5.1
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$failures = [System.Collections.Generic.List[string]]::new()

function Test-Claim {
    param(
        [Parameter(Mandatory = $true)][int]$Id,
        [Parameter(Mandatory = $true)][bool]$Condition,
        [Parameter(Mandatory = $true)][string]$Detail
    )

    $verdict = if ($Condition) { 'PASS' } else { 'FAIL' }
    Write-Host "claim#$Id $verdict $Detail"
    if (-not $Condition) {
        $failures.Add("claim#$Id $Detail")
    }
}

function Read-RepoText {
    param([Parameter(Mandatory = $true)][string]$RelativePath)
    return [System.IO.File]::ReadAllText((Join-Path $root $RelativePath))
}

$requiredPaths = @(
    'PASSOVER-NATIVE-MECCHA.md',
    '.planning/phases/10-native-unreal-conversion/10-01-PLAN.md',
    'docs/unreal/NATIVE-ARCHITECTURE.md',
    'docs/unreal/MECCHA-CAMERA-SEAM.md',
    'config/schemas/unreal-native-profile.schema.json',
    'config/unreal/native/meccha-chameleon.json',
    'src/native/runtime/openxr/d3d12_binding.cpp',
    'src/native/runtime/openxr/openxr_runtime.cpp',
    'src/native/runtime/public/vr_runtime_api.h',
    'src/native/adapters/unreal/native_unreal_contract.cpp',
    'src/native/adapters/unreal/unreal_camera_contract.cpp',
    'src/native/adapters/unreal/unreal_camera_observer.cpp',
    'src/native/adapters/unreal/unreal_scene_view_decoder.cpp',
    'src/native/adapters/unreal/unreal_orientation_only.cpp',
    'src/native/adapters/unreal/meccha_orientation_tracking_gate.cpp',
    'src/native/adapters/unreal/meccha_camera_identity_gate.cpp',
    'src/native/adapters/unreal/meccha_orientation_service.cpp',
    'src/native/adapters/unreal/meccha_observer_payload.cpp',
    'src/native/adapters/unreal/meccha_orientation_payload.def',
    'src/native/adapters/unreal/meccha_stereo_payload.def',
    'src/native/adapters/unreal/meccha_camera_identity_gate.cpp',
    'src/native/adapters/unreal/meccha_camera_observer_payload.cpp',
    'src/native/adapters/unreal/unreal_view_seam_locator.cpp',
    'src/native/adapters/unreal/unreal_frame_contract.cpp',
    'src/native/adapters/unreal/d3d12_observer.cpp',
    'src/native/adapters/unreal/d3d12_scene_relay_renderer.cpp',
    'adapters/meccha_chameleon/native_adapter.cpp',
    'tests/native/runtime/d3d12_stereo_probe.cpp',
    'tests/native/runtime/openxr_pose_sampling_static_tests.py',
    'tests/native/adapters/native_unreal_tests.cpp',
    'tools/meccha_native_preflight.py',
    'tools/run-meccha-headset-bridge.ps1',
    'tools/run-meccha-camera-observer.ps1',
    'tests/native/adapters/validate_unreal_native_profile.py',
    'artifacts/meccha-observer/20260801-163707/flat-confirmation.md',
    'artifacts/meccha-headset/20260801-163923/session-evidence.json',
    'artifacts/meccha-headset/20260801-163923/headset-confirmation.md',
    'artifacts/meccha-scene-relay/20260801-164124/session-evidence.json',
    'artifacts/meccha-scene-relay/20260801-164124/visual-confirmation.md',
    'artifacts/meccha-camera-observer/20260801-180847/session-evidence.json',
    'artifacts/meccha-camera-observer/20260801-180847/flat-confirmation.md',
    'artifacts/meccha-orientation/20260801-215700/failed-human-gate.md',
    'artifacts/meccha-orientation/20260801-221129/flat-confirmation.md',
    'artifacts/meccha-orientation/20260801-224652/failed-human-gate.md',
    'artifacts/meccha-orientation/20260802-021633/session-evidence.json',
    'artifacts/meccha-orientation/20260802-021633/human-confirmation.md',
    'artifacts/meccha-orientation/20260802-061542/session-evidence.json',
    'artifacts/meccha-orientation/20260802-061542/human-direction-gate.md',
    'artifacts/meccha-orientation/20260802-065232/session-evidence.json',
    'artifacts/meccha-orientation/20260802-065232/human-yaw-measurement.md'
)

$missing = @($requiredPaths | Where-Object {
    -not [System.IO.File]::Exists((Join-Path $root $_))
})
Test-Claim 1 ($missing.Count -eq 0) "authoritative native paths exist; missing=$($missing -join ',')"

$oldPassover = Read-RepoText 'PASSOVER.md'
Test-Claim 2 ($oldPassover -match 'SUPERSEDED FOR CURRENT MECCHA WORK') 'root legacy passover points readers to the native passover'

$legacyDocs = @(
    'docs/unreal/UEVR-ORCHESTRATION.md',
    'config/unreal/profiles/meccha-chameleon/VRCLIENT-PROFILE-README.md',
    'docs/app/README.md',
    'docs/release/WINDOWS-V0.2.md'
)
$unmarked = @($legacyDocs | Where-Object {
    (Read-RepoText $_) -notmatch '(?i)status:.*(legacy|historical)'
})
Test-Claim 3 ($unmarked.Count -eq 0) "legacy/historical docs are bannered; unmarked=$($unmarked -join ',')"

$legacyScript = Read-RepoText 'scripts/meccha-rtx-qa.ps1'
Test-Claim 4 ($legacyScript -match 'LEGACY UEVR FALLBACK') 'old Meccha RTX script cannot be mistaken for native closure'

$nativeProfile = Get-Content -LiteralPath (Join-Path $root 'config/unreal/native/meccha-chameleon.json') -Raw | ConvertFrom-Json
Test-Claim 5 (
    $nativeProfile.renderer.observed_api -eq 'd3d12' -and
    $nativeProfile.renderer.observation_state -eq 'game_observed' -and
    $nativeProfile.game.steam_buildid_observed -eq '24517175' -and
    $nativeProfile.game.executable_sha256_observed -eq
        '2192bea467070e9ac051d587a5b2d3afd9fd525580692c361db67a371f0883ce'
) 'live Meccha renderer and current build identity are recorded exactly'
Test-Claim 6 ($nativeProfile.conversion.capabilities.graphics_bridge -eq 'planned') 'game/OpenXR graphics bridge remains planned'
Test-Claim 7 ([bool]$nativeProfile.verification.external_converter_absent_required) 'native evidence requires external converters absent'

$cmake = Read-RepoText 'CMakeLists.txt'
$cmakeRefs = @(
    'src/native/runtime/openxr/d3d12_device.cpp',
    'src/native/runtime/openxr/d3d12_binding.cpp',
    'src/native/adapters/unreal/native_unreal_contract.cpp',
    'src/native/adapters/unreal/unreal_camera_contract.cpp',
    'src/native/adapters/unreal/unreal_camera_observer.cpp',
    'src/native/adapters/unreal/unreal_scene_view_decoder.cpp',
    'src/native/adapters/unreal/unreal_orientation_only.cpp',
    'src/native/adapters/unreal/meccha_orientation_tracking_gate.cpp',
    'src/native/adapters/unreal/meccha_camera_pin.cpp',
    'src/native/adapters/unreal/unreal_view_seam_locator.cpp',
    'src/native/adapters/unreal/d3d12_observer.cpp',
    'src/native/adapters/unreal/unreal_openxr_bridge.cpp',
    'src/native/adapters/unreal/d3d12_stereo_diagnostic_renderer.cpp',
    'src/native/adapters/unreal/d3d12_scene_relay_renderer.cpp',
    'adapters/meccha_chameleon/native_adapter.cpp',
    'tests/native/runtime/d3d12_stereo_probe.cpp',
    'tests/native/adapters/validate_unreal_native_profile.py'
)
$missingRefs = @($cmakeRefs | Where-Object { $cmake -notmatch [regex]::Escape($_) })
Test-Claim 8 ($missingRefs.Count -eq 0) "CMake references native sources/checks; missing=$($missingRefs -join ',')"

$nativePassover = Read-RepoText 'PASSOVER-NATIVE-MECCHA.md'
$cameraSeam = Read-RepoText 'docs/unreal/MECCHA-CAMERA-SEAM.md'
$diagnosticEvidence = Get-Content -LiteralPath (
    Join-Path $root 'artifacts/meccha-headset/20260801-163923/session-evidence.json'
) -Raw | ConvertFrom-Json
Test-Claim 9 (
    $diagnosticEvidence.headset_ready -eq $true -and
    $diagnosticEvidence.headset_frame_count -ge 1500 -and
    $diagnosticEvidence.eye_count -eq 2 -and
    $nativePassover -match 'Diagnostic eyes \| Live headset verified' -and
    $nativePassover -match 'not a game\s+camera'
) 'handoff records the live red/blue headset bridge without promoting the game camera'

$sceneEvidence = Get-Content -LiteralPath (
    Join-Path $root 'artifacts/meccha-scene-relay/20260801-164124/session-evidence.json'
) -Raw | ConvertFrom-Json
Test-Claim 10 (
    $sceneEvidence.headset_ready -eq $true -and
    $sceneEvidence.headset_frame_count -ge 1260 -and
    $sceneEvidence.scene_capture_count -ge 2757 -and
    $sceneEvidence.scene_capture_fail_count -eq 0 -and
    $sceneEvidence.eye_count -eq 2 -and
    $nativePassover -match 'Monoscopic live-scene relay \| Live headset verified' -and
    $nativePassover -match 'proves transport, not stereoscopic depth'
) 'handoff records the live scene relay while retaining the monoscopic limitation'

Test-Claim 11 ($nativePassover -match 'UEVR remains a legacy fallback') 'handoff routes UEVR as fallback only'

$cameraEvidence = Get-Content -LiteralPath (
    Join-Path $root 'artifacts/meccha-camera-observer/20260801-180847/session-evidence.json'
) -Raw | ConvertFrom-Json
Test-Claim 12 (
    $cameraEvidence.signature_verified -eq $true -and
    $cameraEvidence.camera_ready -eq $true -and
    $cameraEvidence.decoded_sample_count -ge 13360 -and
    $cameraEvidence.rejected_sample_count -eq 0 -and
    $cameraEvidence.capture_fault_count -eq 0 -and
    $cameraEvidence.recent_candidate_count -eq 1 -and
    $cameraEvidence.ambiguous_observation_count -eq 0 -and
    $nativePassover -match 'Live `FSceneView` camera provider \| Live flat verified' -and
    $nativePassover -match 'flat-confirmation\.md.*passed human gate'
) 'handoff records the exact live camera provider and passed flat-output human gate'

$orientationEvidence = Get-Content -LiteralPath (
    Join-Path $root 'artifacts/meccha-orientation/20260802-021633/session-evidence.json'
) -Raw | ConvertFrom-Json
$orientationConfirmation = Read-RepoText 'artifacts/meccha-orientation/20260802-021633/human-confirmation.md'
$directionGate = Read-RepoText 'artifacts/meccha-orientation/20260802-061542/human-direction-gate.md'
$yawMeasurement = Read-RepoText 'artifacts/meccha-orientation/20260802-065232/human-yaw-measurement.md'
Test-Claim 13 (
    $cmake -match 'vrclient_meccha_orientation_payload' -and
    $nativePassover -match 'Orientation-only game-camera payload \| Continuous/stable live response and yaw sign measured; monoscopic quality limit remains' -and
    $orientationEvidence.orientation_apply_count -ge 13701 -and
    $orientationEvidence.consecutive_orientation_apply_count -ge 13701 -and
    $orientationEvidence.orientation_pose_sample_success_count -ge 22316 -and
    $orientationEvidence.orientation_tracking_fault_latched -eq $false -and
    $orientationEvidence.orientation_missing_pose_skip_count -eq 0 -and
    $orientationEvidence.orientation_stale_pose_skip_count -eq 0 -and
    $orientationConfirmation -match 'stayed\s+continuous and no longer reset' -and
    $directionGate -match 'Stability result: PASS' -and
    $directionGate -match 'Direction result: FAIL' -and
    $yawMeasurement -match 'right hold[\s\S]*approximately `\+32\.48`' -and
    $yawMeasurement -match 'left is negative Unreal yaw and right is positive Unreal yaw' -and
    $yawMeasurement -match 'rules out another yaw inversion' -and
    $yawMeasurement -match 'does not prove exclusive causation' -and
    $yawMeasurement -match 'gain[\s\S]*timing[\s\S]*reference drift[\s\S]*FOV[\s\S]*projection' -and
    $nativePassover -match 'experimental true-stereo bootstrap' -and
    $nativePassover -match 'likely dominated' -and
    $cameraSeam -match 'confirmed\s+architectural limitation' -and
    $cameraSeam -match 'not a proven exclusive cause' -and
    $nativePassover -match 'V \* \(A\^T \* D \* A\)' -and
    $nativePassover -match 'orientation_tracking_fault_latched' -and
    $nativePassover -match 'vr_runtime_sample_head_pose' -and
    $cmake -match 'openxr_pose_sampling_static_checks' -and
    $cmake -match 'vr_meccha_evidence_dispatch_tests' -and
    $nativePassover -match 'OrientationOnly' -and
    $nativeProfile.conversion.capabilities.head_tracking -eq 'planned'
) 'orientation-only payload records continuous live response and controlled asynchronous persistence without promoting head tracking'

Test-Claim 14 (
    $cmake -match 'vrclient_meccha_stereo_payload' -and
    $nativePassover -match 'Automated live projection/transport gate passed; human stereo gate pending' -and
    $nativePassover -match 'exactly two stable' -and
    $nativePassover -match 'within two degrees' -and
    $nativePassover -match 'Packed backbuffer[\s\S]*dimensions select only the two texture regions' -and
    $nativePassover -match 'failed closed with zero camera writes' -and
    $nativePassover -match '20260802-194036' -and
    $nativePassover -match 'exactly 110-degree/1\.0 projection' -and
    $nativePassover -match 'full 60-sample warm-up' -and
    $cameraSeam -match 'Experimental side-by-side stereo payload' -and
    $cameraSeam -match 'decoded/configured Unreal angular projection' -and
    $cameraSeam -match '90\.25-degree/1\.0468 projection and zero camera writes' -and
    $nativeProfile.conversion.capabilities.stereo_views -eq 'planned' -and
    $nativeProfile.conversion.capabilities.head_tracking -eq 'planned'
) 'experimental stereo lane records the live automated projection gate without promoting human stereo capabilities'

if ($failures.Count -gt 0) {
    Write-Error ("Native Meccha doc audit failed:`n" + ($failures -join "`n"))
    exit 1
}

Write-Host 'PASS native Meccha documentation routing is internally consistent'

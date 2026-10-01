from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
RUNTIME = ROOT / "src/native/runtime/openxr/openxr_runtime.cpp"
PAYLOAD = ROOT / "src/native/adapters/unreal/meccha_observer_payload.cpp"
ORIENTATION_SERVICE = (
    ROOT / "src/native/adapters/unreal/meccha_orientation_service.cpp"
)
ORIENTATION_HEADER = (
    ROOT / "src/native/adapters/unreal/meccha_orientation_service.h"
)
STEREO_POSE_PAIR = (
    ROOT / "src/native/adapters/unreal/meccha_stereo_pose_pair.cpp"
)
NATIVE_UNREAL_TEST = ROOT / "tests/native/adapters/native_unreal_tests.cpp"
EVIDENCE_STATE = (
    ROOT / "src/native/adapters/unreal/meccha_evidence_dispatch_state.h"
)
EVIDENCE_TEST = ROOT / "tests/native/runtime/meccha_evidence_dispatch_tests.cpp"
LAUNCHER = ROOT / "tools/run-meccha-headset-bridge.ps1"


def function_body(text: str, signature: str) -> str:
    start = text.find(signature)
    if start < 0:
        raise AssertionError(f"could not isolate {signature}")
    opening = text.find("{", start + len(signature))
    if opening < 0:
        raise AssertionError(f"could not find body for {signature}")
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start : index + 1]
    raise AssertionError(f"unterminated body for {signature}")


def main() -> int:
    runtime = RUNTIME.read_text(encoding="utf-8")
    payload = PAYLOAD.read_text(encoding="utf-8")
    orientation_service = ORIENTATION_SERVICE.read_text(encoding="utf-8")
    orientation_header = ORIENTATION_HEADER.read_text(encoding="utf-8")
    stereo_pose_pair = STEREO_POSE_PAIR.read_text(encoding="utf-8")
    native_unreal_test = NATIVE_UNREAL_TEST.read_text(encoding="utf-8")
    evidence_state = EVIDENCE_STATE.read_text(encoding="utf-8")
    evidence_test = EVIDENCE_TEST.read_text(encoding="utf-8")
    launcher = LAUNCHER.read_text(encoding="utf-8")

    required_runtime = [
        "XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME",
        "xrConvertWin32PerformanceCounterToTimeKHR",
        "QueryPerformanceCounter",
        "xrLocateSpace(view_space_, local_space_",
        "OpenXrRuntime::sampleHeadPose",
    ]
    for token in required_runtime:
        if token not in runtime:
            raise AssertionError(f"current-time OpenXR pose sampler missing {token}")

    if "vr_runtime_sample_head_pose" not in payload:
        raise AssertionError("orientation payload does not use independent pose sampling")
    orientation_apply = function_body(
        orientation_service, "void applyOrientationOnly(void* options)"
    )
    if orientation_apply.count("latchTrackingFault();") != 1:
        raise AssertionError(
            "only structural pose-snapshot failure may permanently latch tracking"
        )
    for recoverable_result in ("MissingOrientation", "StalePose", "FuturePose"):
        result_branch = orientation_apply.find(recoverable_result)
        if result_branch < 0:
            raise AssertionError(f"missing recoverable branch: {recoverable_result}")
        if "latchTrackingFault();" in orientation_apply[result_branch : result_branch + 350]:
            raise AssertionError(
                f"recoverable freshness loss must skip and rewarm: {recoverable_result}"
            )
    if not re.search(r"CreateThread\([\s\S]*runOrientationPoseSampler", payload):
        raise AssertionError("orientation payload does not start a dedicated pose sampler")
    if not re.search(
        r"orientationPoseSamplerStop\.store\(true[\s\S]*WaitForSingleObject[\s\S]*vr_runtime_stop",
        payload,
    ):
        raise AssertionError("orientation sampler must stop and join before runtime teardown")
    if "WAIT_OBJECT_0" not in payload or "runtime_cleanup_safe" not in payload:
        raise AssertionError(
            "orientation runtime teardown must fail closed when the sampler does not join"
        )
    if not re.search(
        r"if \(runtime_cleanup_safe\)[\s\S]*vr_runtime_stop\(runtime\)"
        r"[\s\S]*vr_runtime_destroy\(runtime\)",
        payload,
    ):
        raise AssertionError(
            "OpenXR runtime teardown must be conditional on a successful sampler join"
        )

    sampler = function_body(payload, "DWORD WINAPI runOrientationPoseSampler(")
    if "writeEvidence()" in sampler:
        raise AssertionError(
            "orientation sampler must not perform blocking evidence I/O"
        )
    for removed_split_sampler_field in (
        "orientationPoseSampleResult",
        "orientationPoseSampleAttemptCount",
        "orientationPoseSampleSuccessCount",
    ):
        if removed_split_sampler_field in payload:
            raise AssertionError(
                f"split sampler telemetry remains: {removed_split_sampler_field}"
            )
    for coherent_telemetry_token in (
        "MecchaOrientationTelemetryBuffer",
        "pose_snapshot_available",
        "sampler_attempt_count",
        "sampler_success_count",
        "kReadAttempts = 64",
    ):
        if coherent_telemetry_token not in orientation_header:
            raise AssertionError(
                f"coherent orientation telemetry missing {coherent_telemetry_token}"
            )
    if "testOrientationTelemetrySnapshotNeverMixesGenerations" not in native_unreal_test:
        raise AssertionError("deterministic orientation telemetry regression missing")

    retry_match = re.search(
        r"kReadAttempts\s*=\s*(\d+)", orientation_header
    )
    if retry_match is None or int(retry_match.group(1)) < 32:
        raise AssertionError(
            "lock-free camera pose reads need a bounded contention retry budget"
        )
    if "_mm_pause()" not in orientation_header:
        raise AssertionError(
            "contended pose snapshot reads must yield between bounded retries"
        )
    evidence_snapshot = function_body(
        orientation_service, "MecchaOrientationSnapshot mecchaOrientationSnapshot("
    )
    if "state->telemetry.read" not in evidence_snapshot:
        raise AssertionError("evidence snapshot must use the coherent telemetry buffer")
    if "latchTrackingFault" in evidence_snapshot:
        raise AssertionError("read-only evidence snapshots must never latch tracking faults")
    if "state->telemetry.read" in function_body(orientation_service, "bool readPose("):
        raise AssertionError("evidence telemetry reads must stay off the camera hot path")

    camera_apply = function_body(orientation_service, "void applyOrientationOnly(")
    if "tracking_lock" in camera_apply:
        raise AssertionError(
            "camera orientation writes must not contend on tracking-state locks"
        )
    if "TryAcquireSRWLockExclusive" in camera_apply:
        raise AssertionError(
            "stereo eye hooks must serialize instead of dropping one eye on lock contention"
        )
    for token in (
        "tracking_state.ready()",
        "tracking_state.faultLatched()",
        "tracking_state.latchFault()",
    ):
        if token not in orientation_service:
            raise AssertionError(f"atomic orientation tracking state missing {token}")

    present_hook = function_body(payload, "HRESULT STDMETHODCALLTYPE hookedPresent(")
    for pair_coherence_token in (
        "publishMecchaStereoRenderGeneration",
        "snapshot.present_count",
    ):
        if pair_coherence_token not in present_hook:
            raise AssertionError(
                f"stereo pose pairing is not bounded by game Present: {pair_coherence_token}"
            )
    for pair_coherence_token in (
        "stereo_pose_pair.select",
        "stereo_render_generation.load",
        "ReadyDuplicateEye",
        "last_completed_stereo_pair",
        "pair_selection.write_enabled",
        "pair_selection.pose_validation_time_ns",
    ):
        if pair_coherence_token not in camera_apply:
            raise AssertionError(
                f"camera hook lacks coherent stereo pose selection: {pair_coherence_token}"
            )
    for coordinator_token in (
        "render_generation > active_render_generation_",
        "output->pose = active_pose_",
        "seen_eye_mask_",
        "incomplete_pair_count_",
    ):
        if coordinator_token not in stereo_pose_pair:
            raise AssertionError(
                f"Present-bounded stereo coordinator missing {coordinator_token}"
            )
    for regression in (
        "testStereoPosePairUsesOnePoseGenerationForBothEyes",
        "testStereoPosePairKeepsDuplicatesCoherentAndCountsIncompleteFrames",
    ):
        if regression not in native_unreal_test:
            raise AssertionError(f"stereo pose-pair regression missing {regression}")

    render_callback = function_body(payload, "VrRuntimeResult renderHeadsetFrame(")
    if "publishMecchaOrientationOnlyPose" in render_callback:
        raise AssertionError("render callback must not remain the orientation pose source")
    if "writeEvidence()" in render_callback:
        raise AssertionError(
            "headset render callback must not perform blocking evidence I/O"
        )
    for producer in (render_callback, sampler):
        for blocking_token in (
            "CreateFileW",
            "WriteFile",
            "FlushFileBuffers",
            "MoveFileExW",
        ):
            if blocking_token in producer:
                raise AssertionError(
                    f"render producer must not call {blocking_token}"
                )

    bridge = function_body(payload, "DWORD WINAPI runHeadsetBridge(")
    for terminal_state in (
        "VR_RUNTIME_STATE_EXITING",
        "VR_RUNTIME_STATE_LOSS_PENDING",
        "VR_RUNTIME_STATE_ERROR",
        "VR_RUNTIME_STATE_STOPPED",
    ):
        if terminal_state not in bridge:
            raise AssertionError(
                f"headset bridge must exit for terminal state {terminal_state}"
            )
    if "return finishHeadsetBridge(" not in bridge:
        raise AssertionError("terminal bridge exits must request and flush evidence")
    raw_terminal_return = re.search(r"\breturn\s+[0-9]+\s*;", bridge)
    if raw_terminal_return is not None:
        raise AssertionError("bridge has a terminal return that bypasses evidence flush")

    for packed_status in ("headsetStartStatus", "headsetFrameStatus"):
        if f"MecchaEvidenceResultState {packed_status}" not in payload:
            raise AssertionError(f"launcher status pair is not coherent: {packed_status}")
    for removed_split_atomic in (
        "headsetStartResult",
        "headsetStartAttempted",
        "headsetFrameResult",
        "headsetFrameAttempted",
    ):
        if removed_split_atomic in payload:
            raise AssertionError(
                f"split launcher-sensitive atomic remains: {removed_split_atomic}"
            )
    for dispatcher_token in (
        "MecchaEvidenceDispatcher",
        "WindowsEvidenceDispatchOperations",
        "kEvidenceRetryMaximumMs",
        "ensureEvidenceDispatcherUntilReady",
        "kEvidenceDispatcherStartTimeoutMs",
        "flushEvidenceGeneration",
        "kEvidenceTerminalFlushMs",
    ):
        if dispatcher_token not in payload:
            raise AssertionError(f"durable evidence dispatcher missing {dispatcher_token}")
    if "TrySubmitThreadpoolCallback" in payload:
        raise AssertionError("evidence must not depend on one-shot callback submission")
    request_writer = function_body(payload, "std::uint64_t requestEvidenceWrite(")
    for io_token in ("writeEvidence", "CreateFileW", "WriteFile", "MoveFileExW"):
        if io_token in request_writer:
            raise AssertionError(f"evidence producer performs I/O through {io_token}")
    if (
        "MecchaEvidencePersistResult writeEvidence()" not in payload
        or "MecchaEvidencePersistResult::WriteFailed" not in payload
        or "MecchaEvidencePersistResult::FlushFailed" not in payload
        or "MecchaEvidencePersistResult::RenameFailed" not in payload
    ):
        raise AssertionError("evidence I/O failures must be observable by the dispatcher")
    for fault_coverage in (
        "testResultAttemptedPairIsCoherent",
        "testProductionStateMachineRetriesFirstNFailures",
        "testRealWorkerPreservesNewerGeneration",
        "testTerminalFlushIsBounded",
        "testInitializationFailureBecomesExplicitTerminalState",
    ):
        if fault_coverage not in evidence_test:
            raise AssertionError(f"evidence dispatcher regression missing {fault_coverage}")
    for injected_operation in (
        "request_create_failures",
        "acknowledged_create_failures",
        "worker_create_failures",
        "request_signal_failures",
        "waitForAcknowledgedSignalAttempt",
        "write_failures",
        "flush_failures",
        "rename_failures",
        "shutdownForTest",
    ):
        if injected_operation not in evidence_test:
            raise AssertionError(
                f"real dispatcher fault injection missing {injected_operation}"
            )
    bootstrap_guard = "if (!ensureEvidenceDispatcherUntilReady())"
    if bootstrap_guard not in bridge or bridge.find(bootstrap_guard) > bridge.find("for ("):
        raise AssertionError(
            "headset bridge must establish a writer before its long-running loop"
        )
    if "snapshotAfterLoad" not in evidence_state:
        raise AssertionError("coherent result regression needs a deterministic load barrier")
    if "LocalEvidence" not in launcher or "LocalApplicationData" not in launcher:
        raise AssertionError("LocalEvidence launcher behavior must be preserved")
    steam_prompt = "STEAM: approve the launch-options prompt on the desktop"
    steam_launch = "Start-Process $steamLaunchUri"
    if steam_prompt not in launcher:
        raise AssertionError(
            "Stereo launcher must tell the operator to approve Steam's desktop prompt"
        )
    if launcher.find(steam_prompt) > launcher.find(steam_launch):
        raise AssertionError(
            "Steam prompt guidance must appear before the launch process wait begins"
        )
    if "steam://run/4704690//-emulatestereo/" not in launcher:
        raise AssertionError("Stereo launch must still request Unreal fake stereo")
    for ignored_override in (
        "FOVForFakeStereoRenderingDevice",
        "EyeOffsetForFakeStereoRenderingDevice",
        "TopFOVRatioForFakeStereoRenderingDevice",
        "DifferenceBetweenEyesForFakeStereoRenderingDevice",
        "r.StereoEmulationFOV",
        "-ini:Game",
        "-ini:Engine",
    ):
        if ignored_override in launcher:
            raise AssertionError(
                f"proven-ignored startup override must not remain: {ignored_override}"
            )
    identity_gate = orientation_service.find("camera_identity_may_write")
    projection_normalize = orientation_service.find(
        "normalizeMeccha24517175StereoProjection"
    )
    if identity_gate < 0 or projection_normalize < 0 or identity_gate > projection_normalize:
        raise AssertionError(
            "exact-build projection normalization must run only after eye identity readiness"
        )
    projection_blocker = """if ($Stereo -and
                $evidence.orientation_stable_camera_id -ne 0 -and
                $evidence.orientation_secondary_stable_camera_id -ne 0 -and"""
    if projection_blocker not in launcher:
        raise AssertionError(
            "launcher must not judge projection before both eye identities stabilize"
        )
    if (
        "stereo_source_eye_aspect_mode" not in payload
        or "configured_projection" not in payload
    ):
        raise AssertionError(
            "stereo relay must preserve the engine projection aspect"
        )
    if "half_backbuffer" in payload:
        raise AssertionError(
            "packed texture dimensions must not overwrite angular projection aspect"
        )

    destroy = function_body(runtime, "void OpenXrRuntime::destroyOpenXrObjects(")
    for reset in (
        "session_begun_ = false",
        "headset_state_.session_active = 0",
        "current_frame_.head_pose = {}",
        "eye.pose = {}",
    ):
        if reset not in destroy:
            raise AssertionError(f"forced OpenXR teardown missing reset: {reset}")

    if "identityPose" in runtime:
        raise AssertionError("unobserved OpenXR poses must not be marked valid")
    update_frame = function_body(runtime, "VrRuntimeResult OpenXrRuntime::updateFrameData(")
    if not re.search(
        r"if \(xrOk\(result\)\)[\s\S]*toRuntimePose\(head_location\.pose,"
        r" head_location\.locationFlags\)[\s\S]*else \{\s*"
        r"current_frame_\.head_pose = \{\}",
        update_frame,
    ):
        raise AssertionError("failed head-space location must clear pose validity")

    render_frame = function_body(runtime, "VrRuntimeResult OpenXrRuntime::renderFrame(")
    if render_frame.count("const VrRuntimeResult end_result") < 3:
        raise AssertionError(
            "every empty-frame recovery must give xrEndFrame failure precedence"
        )
    if render_frame.count("if (end_result != VR_RUNTIME_OK)") < 3:
        raise AssertionError("empty-frame cleanup results must all be checked")
    for state_array in ("image_acquired", "image_waited"):
        if f"std::array<bool, 2> {state_array}" not in render_frame:
            raise AssertionError(
                f"swapchain cleanup must track {state_array} independently"
            )
    wait_start = render_frame.find("xrWaitSwapchainImage(")
    render_target_start = render_frame.find("render_targets[eye].backend", wait_start)
    wait_path = render_frame[wait_start:render_target_start]
    if "result = VR_RUNTIME_ERROR_VR_API" not in wait_path:
        raise AssertionError("a failed swapchain wait must be a hard runtime error")
    if wait_path.find("image_waited[eye] = true") < wait_path.find("if (!xrOk(xr_result))"):
        raise AssertionError("swapchain image must be marked waited only after wait succeeds")
    release_start = render_frame.find("xrReleaseSwapchainImage(")
    recovery_start = render_frame.find("if (result != VR_RUNTIME_OK)", release_start)
    release_path = render_frame[release_start:recovery_start]
    if "!image_acquired[eye] || !image_waited[eye]" not in render_frame:
        raise AssertionError("only acquired-and-waited images may be released")
    if "&& result == VR_RUNTIME_OK" in release_path:
        raise AssertionError("an earlier frame result must not mask release failure")
    if "result = VR_RUNTIME_ERROR_VR_API" not in release_path:
        raise AssertionError("swapchain release failure must take error precedence")

    poll_events = function_body(runtime, "VrRuntimeResult OpenXrRuntime::pollEvents(")
    for token in (
        "XrResult poll_result",
        "XR_EVENT_UNAVAILABLE",
        "fail(VR_RUNTIME_ERROR_VR_API, poll_result)",
    ):
        if token not in poll_events:
            raise AssertionError(f"xrPollEvent failure handling missing {token}")

    print("OpenXR independent pose sampling static checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

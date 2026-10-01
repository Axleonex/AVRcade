from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
PAYLOAD_CPP = ROOT / "src" / "native" / "injector" / "proof_payload" / "proof_payload_main.cpp"
PAYLOAD_DEF = ROOT / "src" / "native" / "injector" / "proof_payload" / "proof_payload.def"
RUNTIME_PAYLOAD_CPP = (
    ROOT
    / "src"
    / "native"
    / "injector"
    / "runtime_smoke_payload"
    / "runtime_smoke_payload_main.cpp"
)
RUNTIME_PAYLOAD_DEF = (
    ROOT
    / "src"
    / "native"
    / "injector"
    / "runtime_smoke_payload"
    / "runtime_smoke_payload.def"
)
RUNTIME_DRIVER_CPP = (
    ROOT
    / "tests"
    / "native"
    / "injector"
    / "detours_runtime_smoke_main.cpp"
)
CAPTURE_PS1 = ROOT / "tests" / "native" / "injector" / "detours_smoke_capture.ps1"
RUNTIME_CAPTURE_PS1 = (
    ROOT / "tests" / "native" / "injector" / "detours_runtime_smoke_capture.ps1"
)
CMAKE = ROOT / "CMakeLists.txt"
CI = ROOT / "scripts" / "ci" / "build-and-test.ps1"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def main() -> int:
    for path in [
        PAYLOAD_CPP,
        PAYLOAD_DEF,
        RUNTIME_PAYLOAD_CPP,
        RUNTIME_PAYLOAD_DEF,
        RUNTIME_DRIVER_CPP,
        CAPTURE_PS1,
        RUNTIME_CAPTURE_PS1,
        CMAKE,
        CI,
    ]:
        assert path.exists(), f"missing Detours smoke artifact: {path.relative_to(ROOT)}"

    payload = read(PAYLOAD_CPP)
    payload_def = read(PAYLOAD_DEF)
    runtime_payload = read(RUNTIME_PAYLOAD_CPP)
    runtime_payload_def = read(RUNTIME_PAYLOAD_DEF)
    runtime_driver = read(RUNTIME_DRIVER_CPP)
    capture = read(CAPTURE_PS1)
    runtime_capture = read(RUNTIME_CAPTURE_PS1)
    cmake = read(CMAKE)
    ci = read(CI)
    corpus = "\n".join([
        payload,
        runtime_payload,
        runtime_driver,
        capture,
        runtime_capture,
        cmake,
        ci,
    ])
    optional_code = "\n".join([
        payload,
        runtime_payload,
        runtime_driver,
        capture,
        runtime_capture,
    ])

    assert "VRCLIENT_PROOF_SENTINEL" in payload
    assert "GetModuleFileNameW" in payload
    assert "CreateFileW" in payload and "WriteFile" in payload
    assert "pose_timing_api" in payload
    assert "DetourFinishHelperProcess @1" in payload_def
    assert "withdll.exe" in capture
    assert "vrclient_smoke_host.exe" in capture
    assert "token=$token" in capture
    assert "vr_runtime_create" in runtime_payload
    assert "vr_runtime_get_state" in runtime_payload
    assert "vr_runtime_get_headset_state" in runtime_payload
    assert "vr_runtime_get_frame_data" in runtime_payload
    assert "vr_runtime_start" not in runtime_payload
    assert "DetourFinishHelperProcess @1" in runtime_payload_def
    assert "vrclient_runtime_smoke_probe" in runtime_payload_def
    assert "runBootstrapSmoke" in runtime_driver
    assert "evaluateSafetyVerdict" in runtime_driver
    assert "BootstrapRuntimeLoader" in runtime_driver
    assert "CreateProcessW" in runtime_driver
    assert "withdll.exe" in runtime_capture
    assert "blocked_loader_invoked=false" in runtime_capture
    assert "diagnostics log missing build id" in runtime_capture
    assert "VRCLIENT_BUILD_OPENXR_RUNTIME" in cmake
    assert "vrclient_detours_runtime_smoke_capture" in cmake
    assert "VRCLIENT_ENABLE_DETOURS_SMOKE" in cmake
    assert "VRCLIENT_DETOURS_WITHDLL" in cmake
    assert "VRCLIENT_ENABLE_DETOURS_SMOKE=ON" in ci
    assert "DetoursSmoke" in ci

    forbidden_tokens = [
        "OpenProcess",
        "VirtualAllocEx",
        "WriteProcessMemory",
        "CreateRemoteThread",
        "NtCreateThreadEx",
        "QueueUserAPC",
        "SetWindowsHookEx",
        "VirtualProtectEx",
        "RtlCreateUserThread",
        "ThreadHideFromDebugger",
        "NtSetInformationThread",
        "DetourAttach",
        "DetourTransactionBegin",
        "WinHttp",
        "InternetOpen",
        "socket(",
    ]
    hits = [token for token in forbidden_tokens if token in optional_code]
    assert not hits, f"Detours smoke path must not implement raw injection/evasion/network APIs: {hits}"

    assert 'option(VRCLIENT_ENABLE_DETOURS_SMOKE' in cmake
    assert '"Enable optional user-supplied Detours controlled smoke test. OFF by default."' in cmake
    assert "if(VRCLIENT_ENABLE_DETOURS_SMOKE)" in cmake
    assert "if(VRCLIENT_ENABLE_DETOURS_SMOKE)" in cmake
    assert "vrclient_runtime_smoke_payload" in cmake
    assert "OpenXR::openxr_loader" in cmake

    print("detours smoke static checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

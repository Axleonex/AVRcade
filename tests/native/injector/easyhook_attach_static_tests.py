from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
DRIVER_CPP = ROOT / "tests" / "native" / "injector" / "easyhook_attach_smoke_main.cpp"
CAPTURE_PS1 = ROOT / "tests" / "native" / "injector" / "easyhook_attach_smoke_capture.ps1"
PROOF_PAYLOAD_CPP = (
    ROOT / "src" / "native" / "injector" / "proof_payload" / "proof_payload_main.cpp"
)
CMAKE = ROOT / "CMakeLists.txt"
CI = ROOT / "scripts" / "ci" / "build-and-test.ps1"
RUNBOOK = ROOT / "docs" / "injector" / "easyhook-attach-runbook.md"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def main() -> int:
    for path in [DRIVER_CPP, CAPTURE_PS1, PROOF_PAYLOAD_CPP, CMAKE, CI, RUNBOOK]:
        assert path.exists(), f"missing EasyHook attach artifact: {path.relative_to(ROOT)}"

    driver = read(DRIVER_CPP)
    capture = read(CAPTURE_PS1)
    payload = read(PROOF_PAYLOAD_CPP)
    cmake = read(CMAKE)
    ci = read(CI)
    runbook = read(RUNBOOK)

    assert "DiscoveryFlow::AttachRunning" in driver
    assert "discoverTargets" in driver
    assert "runBootstrapSmoke" in driver
    assert "evaluateSafetyVerdict" in driver
    assert "--pid" in driver and "--dll" in driver
    assert "blocked_loader_invoked=false" in driver
    assert "EasyHook-compatible attach loader" in capture
    assert "AttachLoader.exe --pid <pid> --dll <payload.dll>" in capture
    assert "VRCLIENT_PROOF_SENTINEL" in payload
    assert "VRCLIENT_ENABLE_EASYHOOK_ATTACH_SMOKE" in cmake
    assert "VRCLIENT_EASYHOOK_ATTACH_LOADER" in cmake
    assert "vrclient_easyhook_attach_smoke_capture" in cmake
    assert "EasyHookAttachSmoke" in ci
    assert "EasyHookAttachLoader" in ci
    assert "EasyHookAttachSmoke" in runbook
    assert "AttachLoader.exe --pid <pid> --dll <payload.dll>" in runbook
    assert "not vendored by VRClient" in runbook

    optional_code = "\n".join([driver, capture])
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
        "RemoteHooking.Inject",
        "EasyHook.RemoteHooking",
        "ManualMap",
        "manual map",
        "stealth",
        "bypass",
        "WinHttp",
        "InternetOpen",
        "socket(",
    ]
    hits = [token for token in forbidden_tokens if token in optional_code]
    assert not hits, f"EasyHook attach path must stay external/no-evasion: {hits}"

    print("easyhook attach static checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

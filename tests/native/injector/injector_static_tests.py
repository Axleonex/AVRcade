from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
PROCESS_H = ROOT / "src" / "native" / "injector" / "process" / "process_discovery.h"
PROCESS_CPP = ROOT / "src" / "native" / "injector" / "process" / "process_discovery.cpp"
SAFETY_CPP = ROOT / "src" / "native" / "injector" / "safety" / "safety_preflight.cpp"
BOOTSTRAP_CPP = ROOT / "src" / "native" / "injector" / "bootstrap" / "bootstrap_smoke.cpp"
RUNBOOK = ROOT / "docs" / "injector" / "runbook.md"


REASON_CODES = [
    "missing_executable",
    "missing_process",
    "multiple_install_candidates",
    "process_restarted",
    "running_process_mismatch",
    "privilege_mismatch",
    "architecture_mismatch",
    "runtime_architecture_mismatch",
    "unknown_build",
    "ambiguous_build",
    "unsupported_sample_build",
    "identity_not_supported",
    "unsafe_target_source",
    "unknown_anti_cheat_risk",
    "unsafe_anti_cheat_risk",
    "unknown_online_risk",
    "online_mode_risk",
    "public_matchmaking_risk",
    "private_multiplayer_not_confirmed",
    "multiplayer_mod_compatibility_unconfirmed",
    "offline_policy_multiplayer_scope",
    "runtime_binary_missing",
    "runtime_hash_missing",
    "runtime_hash_mismatch",
    "runtime_already_loaded",
    "interrupted_before_load",
    "runtime_loader_missing",
    "runtime_load_failed",
    "pose_timing_unavailable",
]


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def main() -> int:
    for path in [PROCESS_H, PROCESS_CPP, SAFETY_CPP, BOOTSTRAP_CPP, RUNBOOK]:
        assert path.exists(), f"missing Phase 3 artifact: {path.relative_to(ROOT)}"

    process_h = read(PROCESS_H)
    process_cpp = read(PROCESS_CPP)
    safety_cpp = read(SAFETY_CPP)
    bootstrap_cpp = read(BOOTSTRAP_CPP)
    runbook = read(RUNBOOK)
    all_source = "\n".join([process_h, process_cpp, safety_cpp, bootstrap_cpp])

    assert "TargetDescriptor" in process_h
    assert "command_line_plan" in process_h
    assert "diagnostics_context" in process_h
    assert "identity_evidence_trusted" in process_h
    assert "executable_exists" in process_h
    assert "process_discovery_result" in process_cpp
    assert "MultipleCandidates" in process_cpp
    assert "discoverAttach" in process_cpp and "discoverDirect" in process_cpp

    for code in REASON_CODES:
        assert code in runbook, f"runbook does not document {code}"

    for code in [
        "unknown_anti_cheat_risk",
        "online_mode_risk",
        "runtime_hash_mismatch",
        "runtime_already_loaded",
        "unsafe_target_source",
    ]:
        assert code in safety_cpp, f"safety preflight missing {code}"

    preflight_index = bootstrap_cpp.find("runSafetyPreflight")
    load_attempt_index = bootstrap_cpp.find("runtime_load_attempted = true")
    assert preflight_index != -1 and load_attempt_index != -1
    assert preflight_index < load_attempt_index, "bootstrap can attempt load before preflight"
    assert "RefusedIdentity" in bootstrap_cpp
    assert "RefusedPreflight" in bootstrap_cpp
    assert "BootstrapRuntimeLoader" in read(ROOT / "src" / "native" / "injector" / "bootstrap" / "bootstrap_smoke.h")
    assert "runtime_loader_missing" in bootstrap_cpp
    assert "cleanup_attempted" in bootstrap_cpp
    assert "runtime_load_result" in bootstrap_cpp

    dangerous_api = re.compile(
        r"\b(WriteProcessMemory|CreateRemoteThread|NtCreateThreadEx|"
        r"SetWindowsHookEx|VirtualAllocEx|QueueUserAPC)\b"
    )
    hits = dangerous_api.findall(all_source)
    assert not hits, f"Phase 3 contains process-patching/stealth API usage: {hits}"

    required_diagnostics = [
        "game_id",
        "build_id",
        "reason_code",
        "preflight_verdict",
        "bootstrap_result",
        "runtime_load_result",
        "adapter_id",
        "target_scope",
        "openxr_state",
    ]
    for field in required_diagnostics:
        assert field in all_source or field in runbook, f"missing diagnostic field {field}"

    print("injector static and refusal coverage checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
FILES = [
    ROOT / "src" / "native" / "injector" / "process" / "process_discovery.cpp",
    ROOT / "src" / "native" / "injector" / "safety" / "safety_preflight.cpp",
    ROOT / "src" / "native" / "injector" / "bootstrap" / "bootstrap_smoke.cpp",
    ROOT / "src" / "native" / "versioning" / "game_fingerprint.cpp",
    ROOT / "tests" / "native" / "injector" / "injector_tests.cpp",
    ROOT / "tests" / "native" / "versioning" / "versioning_tests.cpp",
    ROOT / "docs" / "injector" / "runbook.md",
]


CASE_TO_REASON = {
    "unknown build": "unknown_build",
    "ambiguous build": "ambiguous_build",
    "unsupported build": "unsupported_sample_build",
    "missing executable": "missing_executable",
    "multiple installs": "multiple_install_candidates",
    "PID reuse": "process_restarted",
    "process restart between discovery and attach": "process_restarted",
    "executable mutation after discovery": "running_process_mismatch",
    "privilege mismatch": "privilege_mismatch",
    "architecture mismatch": "architecture_mismatch",
    "running process mismatch": "running_process_mismatch",
    "untrusted identity evidence": "untrusted_identity_evidence",
    "wrong executable name": "executable_name_mismatch",
    "already-loaded runtime": "runtime_already_loaded",
    "runtime DLL hash missing": "runtime_hash_missing",
    "runtime DLL hash mismatch": "runtime_hash_mismatch",
    "missing runtime loader": "runtime_loader_missing",
    "failed runtime load": "runtime_load_failed",
    "cleanup after partial load": "cleanup_attempted",
    "unsafe anti-cheat risk": "unsafe_anti_cheat_risk",
    "unsafe online risk": "online_mode_risk",
    "public matchmaking risk": "public_matchmaking_risk",
    "private multiplayer not confirmed": "private_multiplayer_not_confirmed",
    "mod compatibility unconfirmed": "multiplayer_mod_compatibility_unconfirmed",
    "offline policy multiplayer scope": "offline_policy_multiplayer_scope",
    "interrupted launch": "interrupted_before_load",
}


def main() -> int:
    corpus = "\n".join(path.read_text(encoding="utf-8") for path in FILES)
    missing: list[str] = []
    for case, reason in CASE_TO_REASON.items():
        if reason not in corpus:
            missing.append(f"{case} -> {reason}")
    if missing:
        raise AssertionError("missing refusal coverage: " + ", ".join(missing))

    assert "runtime_load_attempted" in corpus
    assert "missing loader must not fake a load" in corpus
    assert "cleanup_succeeded" in corpus
    assert "selected commercial targets" in corpus
    assert "private_modded_coop known_safe selected_game" in corpus
    print("injector refusal matrix coverage passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

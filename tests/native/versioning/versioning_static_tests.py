from __future__ import annotations

import json
import re
from copy import deepcopy
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SAMPLE = ROOT / "config" / "games" / "sample-game.json"
VERSIONING_CPP = ROOT / "src" / "native" / "versioning" / "game_fingerprint.cpp"
VERSIONING_H = ROOT / "src" / "native" / "versioning" / "game_fingerprint.h"
PROCESS_H = ROOT / "src" / "native" / "injector" / "process" / "process_discovery.h"
SMOKE_HOST = ROOT / "src" / "native" / "injector" / "bootstrap" / "smoke_host_main.cpp"


def detect(config: dict, target: dict) -> tuple[str, str, int]:
    executable_names = {name.lower() for name in config["executable_names"]}
    if not target.get("trusted", False):
        return ("unknown", "untrusted_identity_evidence", 0)
    if target.get("executable_name", "").lower() not in executable_names:
        return ("unknown", "executable_name_mismatch", 0)

    matches: list[tuple[dict, int]] = []
    for build in config["builds"]:
        confidence = 0
        hashes = {item["value"].lower() for item in build["file_hashes"]}
        if target.get("sha256", "").lower() in hashes:
            confidence = max(confidence, 100)
        if target.get("product_version") in build["product_versions"]:
            confidence = max(confidence, 65)
        signature_tags = set(target.get("signature_tags", []))
        for signature in build["signatures"]:
            if signature["id"] in signature_tags or signature["pattern"] in signature_tags:
                confidence = max(confidence, signature["confidence"])
        if confidence >= build["confidence_threshold"]:
            matches.append((build, confidence))

    if not matches:
        return ("unknown", "unknown_build", 0)

    matches.sort(key=lambda item: item[1], reverse=True)
    best = matches[0][1]
    tied = [item for item in matches if item[1] == best]
    if len(tied) > 1:
        return ("ambiguous", "ambiguous_build", best)

    build = matches[0][0]
    if build["support"] == "supported":
        return ("known_supported", "known_supported_build", best)
    return ("known_unsupported", build.get("refusal_reason", "known_unsupported_build"), best)


def main() -> int:
    config = json.loads(SAMPLE.read_text(encoding="utf-8"))
    cpp = VERSIONING_CPP.read_text(encoding="utf-8")
    header = VERSIONING_H.read_text(encoding="utf-8")
    process_header = PROCESS_H.read_text(encoding="utf-8")
    smoke_host = SMOKE_HOST.read_text(encoding="utf-8")

    assert "bootstrap" not in cpp.lower(), "versioning must not depend on bootstrap"
    assert "safety_preflight" not in cpp, "versioning must not depend on safety preflight"
    assert "DetectionStatus::KnownSupported" in cpp
    assert "ambiguous_build" in cpp
    assert "unknown_build" in cpp
    assert "phase3_identity_only" in cpp
    assert "RuntimeBinaryExpectation" in header
    assert "identity_evidence_trusted" in process_header
    assert "untrusted_identity_evidence" in cpp
    assert "executable_name_mismatch" in cpp
    assert "VRCLIENT_SMOKE_HOST" in smoke_host

    supported = detect(config, {
        "trusted": True,
        "executable_name": "vrclient_smoke_host.exe",
        "sha256": "a4a4893f8f2befed4daa6785f736cf32aa588af41553b3730b683b13fdc4efd7",
        "product_version": "0.1.0-smoke",
    })
    assert supported == ("known_supported", "known_supported_build", 100)

    unsupported = detect(config, {
        "trusted": True,
        "executable_name": "vrclient_smoke_host.exe",
        "sha256": "c4455457d2081f1df5f3b6850f849664125cc89a0e58fcfd6ff86ef6d1ad1315",
        "product_version": "0.0.9-smoke",
    })
    assert unsupported == ("known_unsupported", "unsupported_sample_build", 100)

    unknown = detect(config, {
        "trusted": True,
        "executable_name": "vrclient_smoke_host.exe",
        "sha256": "57e39ef45d24ba6d9eaf0f4a06f087d342a1f3c6dc8840e3a8b2afd7b902cb9f",
        "product_version": "9.9.9",
    })
    assert unknown == ("unknown", "unknown_build", 0)

    signature = detect(config, {
        "trusted": True,
        "executable_name": "vrclient_smoke_host.exe",
        "signature_tags": ["smoke-main-banner"],
    })
    assert signature == ("known_supported", "known_supported_build", 85)

    untrusted = detect(config, {
        "executable_name": "vrclient_smoke_host.exe",
        "sha256": "a4a4893f8f2befed4daa6785f736cf32aa588af41553b3730b683b13fdc4efd7",
    })
    assert untrusted == ("unknown", "untrusted_identity_evidence", 0)

    wrong_name = detect(config, {
        "trusted": True,
        "executable_name": "not_the_smoke_host.exe",
        "sha256": "a4a4893f8f2befed4daa6785f736cf32aa588af41553b3730b683b13fdc4efd7",
    })
    assert wrong_name == ("unknown", "executable_name_mismatch", 0)

    ambiguous_config = deepcopy(config)
    duplicate = deepcopy(config["builds"][0])
    duplicate["build_id"] = "smoke-duplicate"
    ambiguous_config["builds"].append(duplicate)
    ambiguous = detect(ambiguous_config, {
        "trusted": True,
        "executable_name": "vrclient_smoke_host.exe",
        "sha256": "a4a4893f8f2befed4daa6785f736cf32aa588af41553b3730b683b13fdc4efd7",
    })
    assert ambiguous == ("ambiguous", "ambiguous_build", 100)

    assert not re.search(r"\bWriteProcessMemory\b|\bCreateRemoteThread\b", cpp)
    print("versioning static and matrix tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

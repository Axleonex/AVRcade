from __future__ import annotations

import json
from copy import deepcopy
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
MANIFEST = ROOT / "adapters" / "_template" / "adapter.json"


def validate(manifest: dict, game_id: str, build_id: str) -> tuple[bool, list[str]]:
    errors: list[str] = []
    required_exports = {
        "vrclient_get_adapter_abi",
        "vrclient_get_adapter_metadata",
        "vrclient_create_adapter",
        "vrclient_destroy_adapter",
    }
    exports = set(manifest.get("required_exports", []))
    if not required_exports.issubset(exports):
        errors.append("missing_exported_symbol")
    if not manifest.get("adapter_id"):
        errors.append("missing_metadata")
    host = manifest.get("host_api", {})
    if host.get("min") != "1.0.0" or host.get("max") != "1.0.0":
        errors.append("unsupported_api_version")
    if game_id not in set(manifest.get("supported_games", [])):
        errors.append("wrong_game_id")
    builds = {
        (item.get("game_id"), item.get("build_id"))
        for item in manifest.get("supported_builds", [])
    }
    if (game_id, build_id) not in builds:
        errors.append("wrong_build_id")
    return (not errors, errors)


def main() -> int:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    ok, errors = validate(manifest, "vrclient-smoke-host", "smoke-2026-06-11")
    assert ok, errors

    missing_export = deepcopy(manifest)
    missing_export["required_exports"] = ["vrclient_get_adapter_abi"]
    ok, errors = validate(missing_export, "vrclient-smoke-host", "smoke-2026-06-11")
    assert not ok and "missing_exported_symbol" in errors

    unsupported_api = deepcopy(manifest)
    unsupported_api["host_api"]["min"] = "2.0.0"
    ok, errors = validate(unsupported_api, "vrclient-smoke-host", "smoke-2026-06-11")
    assert not ok and "unsupported_api_version" in errors

    ok, errors = validate(manifest, "wrong-game", "smoke-2026-06-11")
    assert not ok and "wrong_game_id" in errors

    ok, errors = validate(manifest, "vrclient-smoke-host", "wrong-build")
    assert not ok and "wrong_build_id" in errors

    print("plugin validation matrix checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

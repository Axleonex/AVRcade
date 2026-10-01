from __future__ import annotations

import json
import sys
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[3]
SCHEMA = ROOT / "config" / "schemas" / "game-fingerprint.schema.json"
ORCHESTRATED_SCHEMA = ROOT / "config" / "schemas" / "orchestrated-game.schema.json"
GAMES = ROOT / "config" / "games"

SAFE_ANTI_CHEAT = {"none", "known_safe"}
SAFE_ONLINE = {"none", "offline_only"}
VALID_ANTI_CHEAT = {"none", "known_safe", "known_risky", "unknown"}
VALID_ONLINE = {"none", "offline_only", "private_modded_coop", "known_online", "unknown"}


def require_real_sha256(value: str, label: str) -> None:
    assert len(value) == 64, f"{label} must be a SHA-256 hex digest"
    assert len(set(value.lower())) > 1, f"{label} must not be a repeated-character sentinel"


def validate_config(path: Path, schema: dict) -> None:
    config = json.loads(path.read_text(encoding="utf-8"))
    jsonschema.validate(config, schema)

    policy = config["support_policy"]
    assert policy["anti_cheat_risk"] in VALID_ANTI_CHEAT
    assert policy["online_risk"] in VALID_ONLINE
    require_real_sha256(config["runtime"]["sha256"], f"{path.name}.runtime.sha256")

    for build in config["builds"]:
        for item in build["file_hashes"]:
            require_real_sha256(item["value"], f"{build['build_id']}.file_hash")
        for offset in build["offsets"]:
            assert offset["phase3_identity_only"] is True

    if config["game_id"] == "vrclient-smoke-host":
        assert config["controlled_smoke_target"] is True
        assert policy["anti_cheat_risk"] in SAFE_ANTI_CHEAT
        assert policy["online_risk"] in SAFE_ONLINE

    if config["game_id"] == "repo":
        assert config["controlled_smoke_target"] is False
        assert config["executable_names"] == ["REPO.exe"]
        assert "steam" in policy["allowed_sources"]
        assert "manual" in policy["allowed_sources"]
        assert policy["anti_cheat_risk"] == "known_safe"
        assert policy["online_risk"] == "private_modded_coop"
        assert config["builds"][0]["build_id"] == "steam-3241660-build-23363152"
        assert config["builds"][0]["file_hashes"][0]["value"].lower() == (
            "412f7cf79cf16888999e22905ca3bb11a6074857efdf8c754073a47ef872317c"
        )


def main() -> int:
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    orchestrated_schema = json.loads(ORCHESTRATED_SCHEMA.read_text(encoding="utf-8"))
    if len(sys.argv) > 1:
        paths = [Path(arg) if Path(arg).is_absolute() else ROOT / arg for arg in sys.argv[1:]]
    else:
        paths = sorted(GAMES.glob("*.json"))

    for path in paths:
        config = json.loads(path.read_text(encoding="utf-8"))
        if "vr_mod" in config:
            jsonschema.validate(config, orchestrated_schema)
            assert config["builds"] == [], f"{path.name}: unverified route cannot declare builds"
        else:
            validate_config(path, schema)

    relative = ", ".join(str(path.relative_to(ROOT)) for path in paths)
    print(f"validated game routes: {relative}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

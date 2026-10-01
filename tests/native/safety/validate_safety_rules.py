from __future__ import annotations

import json
import sys
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[3]
SCHEMA = ROOT / "config" / "schemas" / "safety-rules.schema.json"
RULES = ROOT / "config" / "safety"
RULE_PATTERN = "*rules.json"

# The five modding postures the plan fixes (T01-AC3). The schema constrains the
# enum; this validator asserts the default rule set never relies on a posture to
# weaken default-block, and that the enum stays complete.
MODDING_POSTURES = {
    "official",
    "community-supported",
    "unsupported",
    "unknown",
    "conflicting-with-integrity-policy",
}


def validate_config(path: Path, schema: dict) -> None:
    config = json.loads(path.read_text(encoding="utf-8"))
    jsonschema.validate(config, schema)

    # SAFE-03: the rule set is independently versioned and deliverable.
    assert config["version"] == 1, f"{path.name} version must be 1"
    assert config["rule_set_version"], f"{path.name} must carry a rule_set_version"

    # Anti-cheat indicators must be CONFIG data, not empty placeholders, and the
    # known-AC example block below relies on at least one being present.
    indicators = config["anti_cheat_indicators"]
    assert isinstance(indicators, list) and indicators, (
        f"{path.name} must list anti-cheat indicators as config data"
    )

    game_ids = [rule["game_id"] for rule in config["rules"]]
    assert len(game_ids) == len(set(game_ids)), (
        f"{path.name} has duplicate rule game_id entries"
    )

    for rule in config["rules"]:
        # Default-block / least-permissive: an EMPTY allow-list must never be
        # shippable, because the engine treats an empty allowed_launch_modes as
        # permitting NO launch mode (not all) and an empty
        # acceptable_modding_postures as accepting NO posture (forcing Warn). The
        # schema pins minItems>=1; this mirrors that invariant as an explicit
        # semantic guard so the fail-open seam cannot reappear via edited data.
        assert rule["allowed_launch_modes"], (
            f"{rule['game_id']} must list at least one allowed launch mode; an "
            "empty list permits none, not all"
        )
        assert rule["acceptable_modding_postures"], (
            f"{rule['game_id']} must list at least one acceptable modding posture; "
            "an empty list accepts none and forces Warn"
        )
        for posture in rule["acceptable_modding_postures"]:
            assert posture in MODDING_POSTURES, (
                f"{rule['game_id']} uses unknown modding posture {posture}"
            )
        # Modding posture is a favorable-only signal: an entry may NEVER list
        # conflicting-with-integrity-policy as acceptable.
        assert "conflicting-with-integrity-policy" not in rule[
            "acceptable_modding_postures"
        ], (
            f"{rule['game_id']} must not accept the conflicting posture"
        )
        # Autonomy floor (mirrors the B2 hook-discovery safety gate and the C++
        # loader): only the controlled smoke target may auto-approve. A non-smoke
        # entry must NEVER set allow_offline:true, or this engine would reach a
        # fully autonomous Allow that the B2 gate would refuse. The schema encodes
        # this with if/then; this is the explicit semantic mirror.
        if rule["allow_offline"]:
            assert rule["controlled_smoke_target"], (
                f"{rule['game_id']} sets allow_offline without "
                "controlled_smoke_target; only the controlled smoke target may "
                "auto-approve (autonomy floor)"
            )

    if path.name == "default-rules.json":
        rules_by_id = {rule["game_id"]: rule for rule in config["rules"]}

        # The controlled smoke target must be present and be the allow-able one.
        smoke = rules_by_id.get("vrclient-smoke-host")
        assert smoke is not None, "default rules must cover vrclient-smoke-host"
        assert smoke["controlled_smoke_target"] is True
        assert smoke["allow_offline"] is True

        # A known-anti-cheat example entry must exist so the block path is
        # demonstrably exercised by the rule set + detection inputs.
        blocked = rules_by_id.get("example-anti-cheat-protected")
        assert blocked is not None, (
            "default rules must include a known-anti-cheat example entry"
        )
        assert blocked["controlled_smoke_target"] is False
        assert blocked["allow_offline"] is False

        # The R.E.P.O. commercial target must NOT be auto-allowable.
        repo = rules_by_id.get("repo")
        assert repo is not None, "default rules must cover repo"
        assert repo["controlled_smoke_target"] is False
        assert repo["allow_offline"] is False


def main() -> int:
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator.check_schema(schema)
    if len(sys.argv) > 1:
        paths = [Path(arg) if Path(arg).is_absolute() else ROOT / arg for arg in sys.argv[1:]]
    else:
        paths = sorted(RULES.glob(RULE_PATTERN))

    assert paths, "no safety rule files found to validate"
    for path in paths:
        validate_config(path, schema)

    relative = ", ".join(str(path.relative_to(ROOT)) for path in paths)
    print(f"validated {relative} against {SCHEMA.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

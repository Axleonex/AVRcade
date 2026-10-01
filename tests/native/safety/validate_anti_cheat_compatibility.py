from __future__ import annotations

import json
import sys
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[3]
SCHEMA = ROOT / "config" / "schemas" / "anti-cheat-compatibility.schema.json"
DEFAULT = ROOT / "config" / "safety" / "anti-cheat-compatibility.json"


def validate_config(path: Path, schema: dict) -> None:
    config = json.loads(path.read_text(encoding="utf-8"))
    jsonschema.validate(config, schema)

    assert config["version"] == 1, f"{path.name} schema version must be 1"
    assert config["compatibility_set_version"], (
        f"{path.name} must carry compatibility_set_version"
    )

    variants = config["variants"]
    variant_ids = [variant["variant_id"] for variant in variants]
    assert len(variant_ids) == len(set(variant_ids)), (
        f"{path.name} has duplicate variant_id entries"
    )

    ac_launch_only = [
        variant
        for variant in variants
        if variant["anti_cheat_policy"] == "launch-only-when-detected"
    ]
    assert ac_launch_only, (
        f"{path.name} must include at least one AC launch-only fixture"
    )

    blocked = [
        variant
        for variant in variants
        if variant["anti_cheat_policy"] == "block-when-detected"
    ]
    assert blocked, f"{path.name} must include an AC blocked fixture"

    injector = [variant for variant in variants if variant["launch_mode"] == "injector"]
    assert injector, f"{path.name} must include a clear-signal injector fixture"

    for variant in variants:
        if variant["launch_mode"] == "launch_only":
            assert variant["legitimate_non_injection_path"] is True, (
                f"{variant['variant_id']} launch_only must be a legitimate "
                "non-injection path"
            )
            assert variant["injection_capable"] is False, (
                f"{variant['variant_id']} launch_only must not be injection capable"
            )
        if variant["anti_cheat_policy"] == "launch-only-when-detected":
            assert variant["variant_type"] == "external-vr-mod-launch-only", (
                f"{variant['variant_id']} AC launch-only variant must use the "
                "Phase 8.5 external-vr-mod-launch-only type"
            )
            assert (
                variant["reason_codes"]["launch_only"]
                == "anti_cheat_launch_only_injection_disabled"
            ), (
                f"{variant['variant_id']} must expose the AC launch-only reason code"
            )
        if variant["anti_cheat_policy"] in {
            "launch-only-when-detected",
            "block-when-detected",
        }:
            assert variant["injection_capable"] is False, (
                f"{variant['variant_id']} AC variant must never be injection capable"
            )


def main() -> int:
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator.check_schema(schema)
    if len(sys.argv) > 1:
        paths = [Path(arg) if Path(arg).is_absolute() else ROOT / arg for arg in sys.argv[1:]]
    else:
        paths = [DEFAULT]

    for path in paths:
        validate_config(path, schema)

    relative = ", ".join(str(path.relative_to(ROOT)) for path in paths)
    print(f"validated {relative} against {SCHEMA.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

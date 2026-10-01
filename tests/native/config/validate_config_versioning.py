from __future__ import annotations

# Bolt-on Phase B5 schema validator (CFGV-02): validates the synthetic demo-config
# fixtures against their v1 / v2 schemas and re-derives the v1->v2 migration in
# pure Python, asserting each v1 input migrates to its expected v2 doc AND that the
# expected v2 doc validates against the v2 schema. This is the python-side mirror of
# the C++ migration test; the two must agree on the transform.
#
# Auto-discovered and run by scripts/ci/build-and-test.ps1 step 7, and registered
# as the CTest `config_versioning_schema`.

import json
import sys
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[3]
FIXTURES = ROOT / "tests" / "native" / "config" / "fixtures"
V1_SCHEMA = FIXTURES / "demo-config.v1.schema.json"
V2_SCHEMA = FIXTURES / "demo-config.v2.schema.json"


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def migrate_v1_to_v2(doc: dict) -> dict:
    """Pure-Python mirror of makeDemoConfigV1ToV2 in config_versioning.cpp:
    bump version, rename display_name -> title, add defaulted comfort_mode."""
    out = dict(doc)
    out["version"] = 2
    if "display_name" not in out:
        raise AssertionError("v1->v2 migration: source field display_name is absent")
    out["title"] = out.pop("display_name")
    out.setdefault("comfort_mode", "standard")
    # Preserve a stable key order matching the expected fixtures
    # (version, profile_id, title, comfort_mode).
    ordered = {}
    for key in ("version", "profile_id", "title", "comfort_mode"):
        if key in out:
            ordered[key] = out[key]
    for key, value in out.items():
        if key not in ordered:
            ordered[key] = value
    return ordered


# (v1 input fixture, expected v2 fixture) pairs.
MIGRATION_PAIRS = [
    ("demo-config.v1.basic.input.json", "demo-config.v1.basic.expected-v2.json"),
    ("demo-config.v1.repo.input.json", "demo-config.v1.repo.expected-v2.json"),
]


def main() -> int:
    v1_schema = load(V1_SCHEMA)
    v2_schema = load(V2_SCHEMA)

    # The schemas themselves must be valid Draft 2020-12 schemas.
    jsonschema.Draft202012Validator.check_schema(v1_schema)
    jsonschema.Draft202012Validator.check_schema(v2_schema)

    checked = 0
    for input_name, expected_name in MIGRATION_PAIRS:
        v1_doc = load(FIXTURES / input_name)
        expected_v2 = load(FIXTURES / expected_name)

        # v1 input validates against the v1 schema.
        jsonschema.validate(v1_doc, v1_schema)
        # expected v2 validates against the v2 schema.
        jsonschema.validate(expected_v2, v2_schema)

        # The migration transform reproduces the expected v2 exactly.
        migrated = migrate_v1_to_v2(v1_doc)
        assert migrated == expected_v2, (
            f"{input_name}: migrated doc != {expected_name}\n"
            f"  got: {migrated}\n  exp: {expected_v2}"
        )
        # And the migrated doc validates against the v2 schema.
        jsonschema.validate(migrated, v2_schema)
        checked += 1

    # The current-version v2 fixture and the too-new v3 fixture: v2 validates,
    # v3 must FAIL the v2 schema (it is from a newer client).
    jsonschema.validate(load(FIXTURES / "demo-config.v2.current.json"), v2_schema)
    try:
        jsonschema.validate(load(FIXTURES / "demo-config.v3.too-new.json"), v2_schema)
    except jsonschema.ValidationError:
        pass
    else:
        raise AssertionError("demo-config v3 should NOT validate against the v2 schema")

    print(
        f"validated {checked} demo-config migration pairs against "
        f"{V1_SCHEMA.name} / {V2_SCHEMA.name}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

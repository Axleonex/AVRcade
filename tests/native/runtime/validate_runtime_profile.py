from __future__ import annotations

import json
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[3]
SCHEMA = ROOT / "config" / "schemas" / "runtime-profile.schema.json"
DEFAULT_PROFILE = ROOT / "config" / "defaults" / "runtime-profile.json"


def main() -> int:
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    profile = json.loads(DEFAULT_PROFILE.read_text(encoding="utf-8"))
    jsonschema.validate(profile, schema)

    dynamic = profile["dynamic_resolution"]
    if not dynamic["min_scale"] <= dynamic["initial_scale"] <= dynamic["max_scale"]:
        raise AssertionError("dynamic resolution scale range is invalid")

    foveation = profile["foveation"]
    if foveation["inner_radius"] > foveation["outer_radius"]:
        raise AssertionError("foveation radii are invalid")

    print(f"validated {DEFAULT_PROFILE.relative_to(ROOT)} against {SCHEMA.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

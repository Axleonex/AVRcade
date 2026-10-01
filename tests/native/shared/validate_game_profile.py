from __future__ import annotations

import json
import sys
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[3]
SCHEMA = ROOT / "config" / "schemas" / "game-profile.schema.json"
DEFAULT = ROOT / "config" / "defaults" / "game-profile.json"
PROFILES = ROOT / "config" / "profiles"

REQUIRED_ACTIONS = {
    "move_x",
    "move_y",
    "turn_x",
    "interact",
    "menu",
    "recenter",
    "comfort_snap_turn",
    "comfort_vignette_toggle",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def validate_profile(path: Path, schema: dict) -> None:
    profile = json.loads(path.read_text(encoding="utf-8"))
    jsonschema.validate(profile, schema)

    actions = {item["id"]: item for item in profile["input"]["actions"]}
    missing = REQUIRED_ACTIONS - actions.keys()
    require(not missing, f"missing required input actions: {sorted(missing)}")
    require(actions["move_x"]["gamepad_fallback"], "move_x must support gamepad fallback")
    require(actions["interact"]["gamepad_fallback"], "interact must support gamepad fallback")

    comfort = profile["comfort"]
    require(comfort["snap_turn"]["enabled"], "snap turn should default enabled")
    require(comfort["vignette"]["enabled"], "vignette should default enabled")
    require(comfort["world_scale"] == 1.0, "world scale should be data-driven default 1.0")

    anchors = profile["hud"]["anchors"]
    require(any(anchor["template_data"] for anchor in anchors), "default HUD anchors need template data")
    for anchor in anchors:
        require(anchor["element_id"], "HUD anchor needs element_id")
        require(anchor["depth_m"] > 0, "HUD anchor depth must be positive")
        require("offset" in anchor, "HUD anchor requires offset object")


def main() -> int:
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    if len(sys.argv) > 1:
        paths = [Path(arg) if Path(arg).is_absolute() else ROOT / arg for arg in sys.argv[1:]]
    else:
        paths = [DEFAULT, *sorted(PROFILES.glob("*-game-profile.json"))]

    require(bool(paths), "no game profiles found to validate")
    for path in paths:
        validate_profile(path, schema)

    relative = ", ".join(str(path.relative_to(ROOT)) for path in paths)
    print(f"game profile schema check passed for {relative}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

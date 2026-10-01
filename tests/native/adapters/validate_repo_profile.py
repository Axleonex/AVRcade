from __future__ import annotations

import json
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[3]
SCHEMA = ROOT / "config" / "schemas" / "game-profile.schema.json"
PROFILE = ROOT / "config" / "profiles" / "repo-game-profile.json"

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

EXPECTED_ANCHORS = {
    "HUDCanvas",
    "HealthUI",
    "SemiUI",
    "ChatUI",
    "MapToolController",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    profile = json.loads(PROFILE.read_text(encoding="utf-8"))
    jsonschema.validate(profile, schema)

    require(
        profile["profile_id"] == "repo-steam-3241660-build-23363152",
        "profile id should be pinned to the selected R.E.P.O. build",
    )
    actions = {item["id"]: item for item in profile["input"]["actions"]}
    missing = REQUIRED_ACTIONS - actions.keys()
    require(not missing, f"missing required input actions: {sorted(missing)}")
    require(actions["interact"]["binding"] == "right_trigger", "interact should map to grab")
    require(actions["interact"]["gamepad_fallback"], "interact needs gamepad fallback")

    comfort = profile["comfort"]
    require(comfort["snap_turn"]["enabled"], "snap turn should default enabled")
    require(comfort["vignette"]["enabled"], "vignette should default enabled")
    require(not comfort["smooth_turn"]["enabled"], "smooth turn should default disabled")
    require(comfort["world_scale"] == 1.0, "world scale should default to 1.0")

    anchors = {item["element_id"]: item for item in profile["hud"]["anchors"]}
    require(EXPECTED_ANCHORS <= anchors.keys(), "missing expected R.E.P.O. HUD anchors")
    for anchor in anchors.values():
        require(anchor["template_data"], "R.E.P.O. HUD anchors stay template until validated")
        require(anchor["depth_m"] > 0, "HUD anchor depth must be positive")
        require("offset" in anchor, "HUD anchor requires an offset object")

    print(f"validated {PROFILE.relative_to(ROOT)} against {SCHEMA.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

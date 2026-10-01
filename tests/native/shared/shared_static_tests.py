from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SHARED = ROOT / "src" / "native" / "shared"
SDK_SHARED = ROOT / "src" / "native" / "plugins" / "sdk" / "shared_services.h"
DEFAULT = ROOT / "config" / "defaults" / "game-profile.json"
SCHEMA = ROOT / "config" / "schemas" / "game-profile.schema.json"
CMAKE = ROOT / "CMakeLists.txt"
DOCS = ROOT / "docs" / "sdk" / "adapter-authoring.md"

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


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    expected = [
        SHARED / "game_profile.h",
        SHARED / "game_profile.cpp",
        SHARED / "input" / "input_system.h",
        SHARED / "input" / "input_system.cpp",
        SHARED / "comfort" / "comfort_system.h",
        SHARED / "comfort" / "comfort_system.cpp",
        SHARED / "hud" / "hud_system.h",
        SHARED / "hud" / "hud_system.cpp",
        SDK_SHARED,
        DEFAULT,
        SCHEMA,
    ]
    for path in expected:
        require(path.exists(), f"missing shared-system artifact: {path.relative_to(ROOT)}")

    profile = json.loads(DEFAULT.read_text(encoding="utf-8"))
    actions = {item["id"] for item in profile["input"]["actions"]}
    require(REQUIRED_ACTIONS.issubset(actions), "default profile missing required actions")
    require(profile["comfort"]["snap_turn"]["enabled"], "snap turn must default enabled")
    require(profile["comfort"]["vignette"]["enabled"], "vignette must default enabled")
    require(profile["hud"]["anchors"][0]["template_data"], "default HUD anchor must be template data")

    sdk_text = read(SDK_SHARED)
    for token in [
        "uint32_t size",
        "uint32_t version",
        "VrClientInputService",
        "VrClientComfortService",
        "VrClientHudService",
        "VRCLIENT_INPUT_ACTION_INTERACT",
        "VRCLIENT_HUD_ANCHOR_HEAD_LOCKED",
    ]:
        require(token in sdk_text, f"shared SDK missing token: {token}")
    for forbidden in ["std::", "openxr", "manager", "std::vector", "std::string"]:
        require(forbidden not in sdk_text.lower(), f"shared SDK leaks forbidden token: {forbidden}")

    cmake = read(CMAKE)
    require("add_library(vr_shared_systems STATIC" in cmake, "shared systems must be one library")
    require("vr_shared_unit_tests" in cmake, "shared unit tests must be wired")
    require("game_profile_schema" in cmake, "game profile schema test must be wired")
    require("vr_shared_input" not in cmake, "input must not be a separate top-level library")
    require("vr_shared_comfort" not in cmake, "comfort must not be a separate top-level library")
    require("vr_shared_hud" not in cmake, "HUD must not be a separate top-level library")

    docs = read(DOCS)
    require("Shared Systems" in docs, "SDK docs must describe shared systems")
    require("row-major" in docs, "SDK docs must document HUD transform layout")
    require("per-game data" in docs, "SDK docs must say where per-game data belongs")

    print("shared system static checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
PATCH = ROOT / "patches" / "cyberpunk-vr-port" / "f5e59d7-controller-navigation.patch"
GAME_PROFILE = ROOT / "config" / "profiles" / "cyberpunk-2077-game-profile.json"
INSTALL_MANIFEST = ROOT / "config" / "redengine" / "native" / "cyberpunk-2077-installed-files.json"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def action(profile: dict, action_id: str) -> dict:
    return next(item for item in profile["input"]["actions"] if item["id"] == action_id)


def main() -> int:
    patch = PATCH.read_text(encoding="utf-8")
    profile = json.loads(GAME_PROFILE.read_text(encoding="utf-8"))
    manifest = json.loads(INSTALL_MANIFEST.read_text(encoding="utf-8"))

    require(
        "ctrl.buttons |= 0x0020; // XINPUT_GAMEPAD_BACK" in patch,
        "the OpenXR menu action must emit Cyberpunk's pause binding (Back/Select)",
    )
    require("rightStickClicked = sclick" in patch, "right-stick click must be deferred for chord resolution")
    require(
        "if (leftStickClicked && rightStickClicked)" in patch,
        "both-stick fallback must be explicit and controller-profile independent",
    )
    require(
        "ctrl.buttons |= 0x0010; // XINPUT_GAMEPAD_START" in patch,
        "the both-stick chord must retain access to Cyberpunk's Hub/Map binding",
    )
    require(
        "dpadUsedThisFrame = true" in patch,
        "the both-stick chord must suppress the deferred L3 click",
    )

    pause = action(profile, "menu")
    interact = action(profile, "interact")
    require(interact["binding"] == "left_primary_button", "interact must identify the left-hand primary face button")
    require(interact["gamepad_binding"] == "gamepad_x", "interact metadata must match Cyberpunk's X binding")
    require(pause["binding"] == "left_menu", "pause should use the standard left menu action")
    require(pause["gamepad_binding"] == "gamepad_back", "pause metadata must match the game binding")
    hub = action(profile, "hub_menu")
    require(hub["binding"] == "left_thumbstick_click+right_thumbstick_click", "hub fallback chord must be documented")
    require(hub["gamepad_binding"] == "gamepad_start", "hub metadata must match the game binding")

    stereo = next(package for package in manifest["packages"] if package["name"].startswith("CyberpunkVR Port Stereo"))
    require(".controls1" in stereo["version"], "installed package must identify the controller patch")

    print("Cyberpunk controller navigation patch checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SOURCE = (
    ROOT
    / "external"
    / "cyberpunk-vr-port-reference-f5e59d7e81a3-complete"
    / "cyberpunk-vr-port-f5e59d7e81a35ccf71da1e75b34483335e908174"
)
MANIFEST = ROOT / "config/redengine/native/cyberpunk-2077-installed-files.json"
PROFILE = ROOT / "config/redengine/native/cyberpunk-2077.json"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    overlay_h = (SOURCE / "src/vr/overlay/imgui_overlay.h").read_text(encoding="utf-8")
    overlay_cpp = (SOURCE / "src/vr/overlay/imgui_overlay.cpp").read_text(encoding="utf-8")
    frame_loop = (SOURCE / "src/vr/openxr/openxr_frameloop.cpp").read_text(encoding="utf-8")
    core = (SOURCE / "src/vr/core/vr_core.cpp").read_text(encoding="utf-8")
    no_anims = (
        SOURCE / "mods/redscript/CyberpunkVRPort_NoAnims/vrport_no_anims.reds"
    ).read_text(encoding="utf-8")
    hud = (SOURCE / "mods/cet/CyberpunkVRPort_HUD/init.lua").read_text(encoding="utf-8")
    hud_preset = SOURCE / "mods/cet/CyberpunkVRPort_HUD/hud_layout.ini"
    plugin = (SOURCE / "src/red4ext_stereo/plugin_main.cpp").read_text(encoding="utf-8")
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    profile = json.loads(PROFILE.read_text(encoding="utf-8"))

    require("OverlayMenuTab" in overlay_h, "overlay must expose a typed direct-tab request")
    require("OverlayRequestToggle" in overlay_h, "OpenXR must be able to toggle the VR settings overlay")
    require("kVrMenuHoldMs" in frame_loop, "both-stick hold must have an explicit long-press threshold")
    require("OverlayMenuTab::Vrik" in frame_loop, "the controller shortcut must return directly to VRIK")
    require(
        "ImGuiConfigFlags_NavEnableGamepad" in overlay_cpp,
        "the in-headset settings overlay must enable gamepad navigation",
    )
    require(
        "AddKeyEvent(ImGuiKey_GamepadFaceDown" in overlay_cpp,
        "the OpenXR controller snapshot must feed ImGui activation input",
    )
    require(
        "OverlayIsVisible()" in core and "XINPUT_GAMEPAD{}" in core,
        "controller input must be withheld from gameplay while VR settings are open",
    )

    require("s_autoCameraBakePending" in frame_loop, "first-run camera alignment must be remembered as pending")
    require("BakeCameraOffset()" in frame_loop, "a stable first-run head/camera measurement must be persisted")

    require("VRPortPublishWeaponSprintBlock" in no_anims, "sprint comfort handling must remain present")
    require(
        "VRPortPublishWeaponSprintBlock(scriptInterface, false)" in no_anims,
        "sprint exit must release the forced weapon-up stance instead of latching it forever",
    )

    require("local function readVrActivationState()" in hud, "HUD edits must obey the per-process VR gate")
    require("if not hudLive.vrActive then" in hud, "flat launches must leave the HUD untouched")
    require(hud_preset.is_file(), "VRClient must ship a centered HUD safe-area preset")
    preset = hud_preset.read_text(encoding="utf-8")
    require("xr_hud_top_left_alerts=360.0000" in preset, "tutorial/alert UI must use the compact left inset")
    require("xr_hud_top_right=-360.0000" in preset, "right-edge UI must use the compact right inset")
    require("xr_hud_visible=1" in preset, "the shipped compact HUD must remain visible by default")
    require("hud_user.ini" in hud, "the HUD module must support an untracked per-user mode override")
    require("layout.visible == false" in hud, "immersive mode must collapse gameplay HUD regions")
    require("CyberpunkVRPort_HUD" in plugin, "native activation state must also be published to the HUD module")

    hud_package = next((p for p in manifest["packages"] if "HUD Safe Area" in p["name"]), None)
    require(hud_package is not None, "install manifest must own the HUD safe-area files")
    require(
        any(item["path"].endswith("CyberpunkVRPort_HUD\\init.lua") for item in hud_package["files"]),
        "manifest must pin the gated HUD script",
    )
    require(
        any(dep["path"].endswith("CyberpunkVRPort_HUD\\init.lua") for dep in profile["dependencies"]),
        "preflight must fail closed when the HUD safe-area module is missing",
    )
    require(profile["conversion"]["capabilities"]["hud"] == "game_observed", "HUD capability must leave planned state")

    print("Cyberpunk VR usability patch checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

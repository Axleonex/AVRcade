from __future__ import annotations

import json
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[3]
NATIVE_SCHEMA = ROOT / "config" / "schemas" / "redengine-native-profile.schema.json"
NATIVE_PROFILE = ROOT / "config" / "redengine" / "native" / "cyberpunk-2077.json"
GAME_SCHEMA = ROOT / "config" / "schemas" / "game-profile.schema.json"
GAME_PROFILE = ROOT / "config" / "profiles" / "cyberpunk-2077-game-profile.json"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    native_schema = json.loads(NATIVE_SCHEMA.read_text(encoding="utf-8"))
    native = json.loads(NATIVE_PROFILE.read_text(encoding="utf-8"))
    jsonschema.validate(native, native_schema)

    require(native["engine"]["family"] == "redengine", "wrong engine family")
    require(native["engine"]["major"] == 4, "Cyberpunk requires REDengine 4")
    require(native["loading"]["mode"] == "red4ext_plugin", "RED4ext must load the bridge")
    require(native["loading"]["proxy_dll_forbidden"], "proxy DLL loading must be refused")
    require(native["conversion"]["stereo_method"] == "engine_second_view", "AER is not the product path")
    require(native["game"]["steam_buildid_observed"] == "20383525", "Steam build must be pinned")
    require(
        native["game"]["executable_sha256_observed"] == "a7de82945c03e041fc7339fcf9066224d98db2f5d80fea50f7947bb350a60991",
        "shipping executable hash must be pinned",
    )
    require(native["game"]["adapter_build_id"] == "steam-1091500-build-20383525", "adapter build must match profile")
    require(native["verification"]["status"] == "headset_verified", "successful headset evidence must be recorded")
    require(native["loading"]["state"] == "game_verified", "RED4ext loading must be live-verified")
    require(native["conversion"]["capabilities"]["red4ext_bridge"] == "game_verified", "bridge capability must be live-verified")
    require(native["renderer"]["observation_state"] == "game_verified", "D3D12 observation must be live game-verified")
    require(native["conversion"]["capabilities"]["graphics_bridge"] == "game_verified", "graphics bridge must be live game-verified")
    require(
        native["conversion"]["capabilities"]["stereo_views"] == "game_verified",
        "engine second-view capability must record the successful OpenXR stereo run",
    )
    require(
        native["conversion"]["capabilities"]["head_tracking"] == "game_verified",
        "HMD tracking must record the successful live run",
    )
    require(
        native["conversion"]["capabilities"]["motion_controls"] == "game_observed",
        "controller actions must retain the observed-but-not-yet-fully-verified state",
    )
    dependency_paths = {item["path"].replace("\\", "/").lower() for item in native["dependencies"]}
    require("red4ext/red4ext.dll" in dependency_paths, "RED4ext loader dependency missing")
    require(
        "red4ext/plugins/cyberpunkvr_stereo/cyberpunkvr_stereo.dll" in dependency_paths,
        "VRClient-managed engine-aware stereo backend dependency missing",
    )
    require(
        "red4ext/plugins/cyberpunkvr_hands/cyberpunkvr_hands.dll" in dependency_paths,
        "VRIK arm-bone hook dependency missing",
    )
    require("red4ext/plugins/archivexl/archivexl.dll" in dependency_paths, "ArchiveXL dependency missing")
    require("red4ext/plugins/tweakxl/tweakxl.dll" in dependency_paths, "TweakXL dependency missing")
    require("engine/tools/scc.exe" in dependency_paths, "redscript dependency missing")

    conflict_paths = {item["path"].replace("\\", "/").lower() for item in native["conflicts"]}
    require("bin/x64/dxgi.dll" in conflict_paths, "DXGI proxy conflict missing")
    require("red4ext/plugins/vrclient.redengine" in conflict_paths, "retired bridge conflict missing")
    require(
        "red4ext/plugins/cyberpunkvr_hands" not in conflict_paths,
        "the supported VRIK arm-bone hook must not be blocked as a conflict",
    )

    game_schema = json.loads(GAME_SCHEMA.read_text(encoding="utf-8"))
    game = json.loads(GAME_PROFILE.read_text(encoding="utf-8"))
    jsonschema.validate(game, game_schema)
    require(game["comfort"]["snap_turn"]["enabled"], "snap turn must default on")
    require(game["comfort"]["vignette"]["enabled"], "vignette must default on")
    require(not game["comfort"]["smooth_turn"]["enabled"], "smooth turn must default off")
    require(
        all(anchor["template_data"] for anchor in game["hud"]["anchors"]),
        "unverified Cyberpunk HUD anchors must remain template data",
    )

    print("Cyberpunk REDengine and shared game profiles validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

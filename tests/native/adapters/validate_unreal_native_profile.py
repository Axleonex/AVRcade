from __future__ import annotations

import json
import sys
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[3]
SCHEMA = ROOT / "config" / "schemas" / "unreal-native-profile.schema.json"
PROFILE = ROOT / "config" / "unreal" / "native" / "meccha-chameleon.json"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    profile_path = (
        Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else PROFILE
    )
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    profile_text = profile_path.read_text(encoding="utf-8")
    profile = json.loads(profile_text)
    jsonschema.validate(profile, schema)

    require(
        profile["profile_id"]
        == "meccha-chameleon-steam-4704690-build-24517175-native",
        "native profile must remain pinned to the observed Meccha build",
    )
    require(
        profile["game"]["shipping_binary"]
        == "Chameleon\\Binaries\\Win64\\PenguinHotel-Win64-Shipping.exe",
        "native route must target the real shipping process, not the launcher stub",
    )
    require(
        profile["game"]["executable_sha256_observed"]
        == "2192bea467070e9ac051d587a5b2d3afd9fd525580692c361db67a371f0883ce",
        "native profile must remain pinned to the observed shipping binary",
    )
    require(
        profile["renderer"]["observed_api"] == "d3d12"
        and profile["renderer"]["observation_state"] == "game_observed",
        "native profile must retain the live D3D12 renderer observation",
    )
    require(
        profile["conversion"]["runtime_backend"] == "d3d12",
        "Meccha native route must use VRClient's D3D12 backend",
    )
    require(
        profile["conversion"]["loading"]["state"] == "game_observed",
        "native profile must retain the live observation-only payload result",
    )

    owners = profile["conversion"]["ownership"]
    require(
        set(owners.values()) == {"vrclient"},
        "VRClient must own stereo, camera, input, and submission",
    )
    require(
        profile["verification"]["external_converter_absent_required"],
        "native verification must run with external converters absent",
    )
    require(
        profile["verification"]["private_room_only"],
        "Meccha verification must preserve the private-room safety posture",
    )
    require(
        "uevr" not in profile_text.lower(),
        "native profile may not encode an external converter dependency",
    )

    capabilities = profile["conversion"]["capabilities"]
    require(
        capabilities["graphics_bridge"] == "planned",
        "the game/OpenXR graphics bridge must remain planned until observed",
    )
    for name in ("stereo_views", "head_tracking", "gamepad_fallback", "motion_controls"):
        require(
            capabilities[name] == "planned",
            f"{name} must remain planned until game/headset verification",
        )

    print(f"validated {profile_path} against {SCHEMA.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

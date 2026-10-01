from __future__ import annotations

import hashlib
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
GAME_ROOT = Path(r"X:\SteamLibrary\steamapps\common\Cyberpunk 2077")
CALIBRATION = GAME_ROOT / "red4ext/plugins/CyberpunkVR_Stereo/vrik_calibration.ini"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main() -> int:
    stereo = (SOURCE / "src/vr/stereo/sync_stereo.cpp").read_text(encoding="utf-8")
    core = (SOURCE / "src/vr/core/vr_core.cpp").read_text(encoding="utf-8")
    arms = (SOURCE / "src/red4ext_plugin/vrik/vrik_hook.h").read_text(encoding="utf-8")

    require(
        "CyberpunkVR_ViewDataDispatchFill = 0" in stereo,
        "unsafe generic node-dispatch view-data access must fail closed by default",
    )
    require(
        "CyberpunkVR_ViewDataDispatchFill &&" in stereo,
        "node dispatch must gate the unsafe virtual view-data accessor",
    )
    require(
        "g_locateCameraHits % 2" not in core,
        "stereo eye identity must not depend on camera-hook call parity",
    )
    require(
        "CyberpunkVR_IsVrcamViewActive" in core
        and "CyberpunkVR_IsMainViewActive" in core
        and "viewEyeSign" in core,
        "late IPD placement must derive its sign from MAIN/VRCAM view identity",
    )
    require(
        "ANATOMICAL REACH CLAMP" in arms,
        "arm solver must clamp unreachable controller targets to anatomical reach",
    )
    require(
        "foreL += ratio * diff" not in arms,
        "arm solver must not lengthen bones to reach an out-of-range controller",
    )
    require(
        "targetModel = reachableTarget" in arms,
        "wrist pin must use the reachable target rather than stretching to the raw controller",
    )
    require(
        'CP2077\'s vehicle "look behind" action may move only one of the two engine' in core
        and "vehicleSharedBase" in core
        and "s_mainPosEpoch" in core,
        "vehicle rear-look must rebase VRCAM on MAIN before applying the normal eye offset",
    )

    calibration = CALIBRATION.read_text(encoding="utf-8")
    require("scaleR=1.0000" in calibration, "right-hand reach must default to true 1:1 scale")
    require("scaleL=1.0000" in calibration, "left-hand reach must default to true 1:1 scale")

    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    for package_name, relative_path in (
        ("CyberpunkVR Port Stereo (VRClient-managed backend)", "red4ext\\plugins\\CyberpunkVR_Stereo\\CyberpunkVR_Stereo.dll"),
        ("CyberpunkVR Port Hands (VRIK arm-bone hook)", "red4ext\\plugins\\CyberpunkVR_Hands\\CyberpunkVR_Hands.dll"),
    ):
        package = next(p for p in manifest["packages"] if p["name"] == package_name)
        entry = next(item for item in package["files"] if item["path"] == relative_path)
        installed = GAME_ROOT / Path(relative_path.replace("\\", "/"))
        require(installed.is_file(), f"{relative_path} is not installed")
        require(entry["sha256"] == sha256(installed), f"manifest hash is stale for {relative_path}")

    print("Cyberpunk stereo lifetime, eye identity, and anatomical arm-reach checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

from __future__ import annotations

import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
MANIFEST = ROOT / "config" / "redengine" / "native" / "cyberpunk-2077-installed-files.json"
PROFILE = ROOT / "config" / "redengine" / "native" / "cyberpunk-2077.json"
RELATIVE_PATH = Path("red4ext/plugins/CyberpunkVR_Hands/CyberpunkVR_Hands.dll")
EXPECTED_SHA256 = "7D5B502F50708404B059ADC7CE872FA107856590B9BFEDAD3A94384282D09144"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main() -> int:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    profile = json.loads(PROFILE.read_text(encoding="utf-8"))

    package = next(
        package
        for package in manifest["packages"]
        if package["name"] == "CyberpunkVR Port Hands (VRIK arm-bone hook)"
    )
    entry = next(
        item
        for item in package["files"]
        if Path(item["path"].replace("\\", "/")) == RELATIVE_PATH
    )
    require(entry["sha256"] == EXPECTED_SHA256, "manifest must pin the VRIK arm-hook DLL hash")

    dependency_paths = {
        item["path"].replace("\\", "/").casefold()
        for item in profile["dependencies"]
    }
    conflict_paths = {
        item["path"].replace("\\", "/").casefold()
        for item in profile["conflicts"]
    }
    expected_path = RELATIVE_PATH.as_posix().casefold()
    require(expected_path in dependency_paths, "preflight must require the VRIK arm-bone hook")
    require(
        "red4ext/plugins/cyberpunkvr_hands" not in conflict_paths,
        "preflight must not reject the supported VRIK arm-bone hook",
    )

    installed = Path(manifest["game_root"]) / RELATIVE_PATH
    require(installed.is_file(), "VRIK arm-bone hook DLL is not installed")
    require(sha256(installed) == EXPECTED_SHA256, "installed VRIK arm-bone hook DLL differs from the pinned build")

    print("Cyberpunk VRIK arm-bone hook install checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

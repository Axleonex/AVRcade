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
    / "mods"
    / "redscript"
    / "CyberpunkVRPort_NoAnims"
    / "vrport_no_anims.reds"
)
MANIFEST = ROOT / "config" / "redengine" / "native" / "cyberpunk-2077-installed-files.json"
RELATIVE_PATH = Path("r6/scripts/CyberpunkVRPort_NoAnims/vrport_no_anims.reds")
EXPECTED_SHA256 = "8C7D407A7EA3D83EB2021908438E9CF1E0C5AAD9277B12DB03536724C8277C0E"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main() -> int:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    script = SOURCE.read_text(encoding="utf-8")

    require(sha256(SOURCE) == EXPECTED_SHA256, "pinned NoAnims source hash changed")
    require("wrappedMethod(stateContext, scriptInterface, true)" in script, "camera bob must be forced off")
    require(
        'SetInputFloat(this, n"melee_camera_shake_weight", 0.0)' in script,
        "melee camera additive must be disabled",
    )
    require("VRPortPublishWeaponSprintBlock" in script, "sprint weapon-lower animation must be blocked")

    package = next(package for package in manifest["packages"] if package["name"].startswith("CyberpunkVR Port NoAnims"))
    entry = next(item for item in package["files"] if Path(item["path"].replace("\\", "/")) == RELATIVE_PATH)
    require(entry["sha256"] == EXPECTED_SHA256, "manifest must pin the NoAnims script hash")

    installed = Path(manifest["game_root"]) / RELATIVE_PATH
    require(installed.is_file(), "NoAnims script is not installed")
    require(sha256(installed) == EXPECTED_SHA256, "installed NoAnims script does not match pinned source")

    print("Cyberpunk NoAnims install checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
PATCH = ROOT / "patches" / "cyberpunk-vr-port" / "f5e59d7-openxr-hmd-readiness.patch"
INSTALL_MANIFEST = ROOT / "config" / "redengine" / "native" / "cyberpunk-2077-installed-files.json"
EXPECTED_DLL_SHA256 = "171C853D899BAF0CE4634B650EE02EE79E1185DC5E8748575CB1DC21B6F1A3C6"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    patch = PATCH.read_text(encoding="utf-8")
    manifest = json.loads(INSTALL_MANIFEST.read_text(encoding="utf-8"))

    require("XR_ERROR_FORM_FACTOR_UNAVAILABLE" in patch, "retry must target transient HMD unavailability")
    require("kSystemReadyAttempts = 60" in patch, "retry attempt count must remain bounded")
    require("kSystemReadyDelayMs = 500" in patch, "retry delay must remain explicit")
    require(
        "XR_SUCCEEDED(res) || res != XR_ERROR_FORM_FACTOR_UNAVAILABLE" in patch,
        "non-transient OpenXR errors must fail immediately",
    )
    require("systemAttempt < kSystemReadyAttempts" in patch, "retry loop must have a hard stop")
    require("HMD became ready on attempt" in patch, "successful recovery must be logged")
    require("Failed to get XrSystemId after" in patch, "retry exhaustion must be logged")

    stereo = next(package for package in manifest["packages"] if package["name"].startswith("CyberpunkVR Port Stereo"))
    dll = next(item for item in stereo["files"] if item["path"].endswith("CyberpunkVR_Stereo.dll"))
    require(dll["sha256"] == EXPECTED_DLL_SHA256, "installed manifest must pin the patched DLL hash")
    require("vrclient.hmdready1" in stereo["version"], "package version must identify the readiness patch")

    print("Cyberpunk OpenXR HMD-readiness patch checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

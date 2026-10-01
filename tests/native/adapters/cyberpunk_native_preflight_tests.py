from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "tools" / "cyberpunk_native_preflight.py"
PROFILE = ROOT / "config" / "redengine" / "native" / "cyberpunk-2077.json"


def run(profile: Path, game_root: Path, manifest: Path) -> tuple[int, dict[str, object]]:
    completed = subprocess.run(
        [
            sys.executable,
            str(SCRIPT),
            "--profile",
            str(profile),
            "--game-root",
            str(game_root),
            "--steam-manifest",
            str(manifest),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    return completed.returncode, json.loads(completed.stdout)


def write_profile(path: Path, profile: dict[str, object]) -> None:
    path.write_text(json.dumps(profile), encoding="utf-8")


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="vrclient-cyberpunk-preflight-") as temp:
        root = Path(temp)
        game_root = root / "Cyberpunk 2077"
        exe = game_root / "bin" / "x64" / "Cyberpunk2077.exe"
        exe.parent.mkdir(parents=True)
        exe.write_bytes(b"controlled-cyberpunk-shipping-fixture")
        manifest = root / "appmanifest_1091500.acf"
        manifest.write_text(
            '"AppState"\n{\n  "appid" "1091500"\n  "buildid" "123456"\n}\n',
            encoding="utf-8",
        )
        profile = json.loads(PROFILE.read_text(encoding="utf-8"))
        profile["game"]["steam_buildid_observed"] = None
        profile["game"]["executable_sha256_observed"] = None
        profile["game"]["adapter_build_id"] = None
        profile_path = root / "profile.json"
        write_profile(profile_path, profile)

        code, evidence = run(profile_path, game_root, manifest)
        assert code == 5
        assert evidence["status"] == "BLOCKED_DEPENDENCIES_REQUIRED"

        red4ext = game_root / "red4ext" / "red4ext.dll"
        red4ext.parent.mkdir(parents=True)
        red4ext.write_bytes(b"controlled-red4ext-fixture")
        for dependency in profile["dependencies"]:
            dependency_path = game_root.joinpath(
                *dependency["path"].replace("\\", "/").split("/")
            )
            if dependency_path.exists():
                continue
            dependency_path.parent.mkdir(parents=True, exist_ok=True)
            dependency_path.write_bytes(b"controlled-native-dependency-fixture")
        code, evidence = run(profile_path, game_root, manifest)
        assert code == 6
        assert evidence["status"] == "BLOCKED_FINGERPRINT_UNPINNED"
        actual_hash = hashlib.sha256(exe.read_bytes()).hexdigest()
        assert evidence["actual_sha256"] == actual_hash
        assert evidence["actual_steam_buildid"] == "123456"

        dxgi = game_root / "bin" / "x64" / "dxgi.dll"
        dxgi.write_bytes(b"controlled-conflict")
        code, evidence = run(profile_path, game_root, manifest)
        assert code == 4
        assert evidence["status"] == "BLOCKED_CONFLICTING_RENDER_HOOKS"
        dxgi.unlink()

        profile["game"]["steam_buildid_observed"] = "123456"
        profile["game"]["executable_sha256_observed"] = actual_hash
        profile["game"]["adapter_build_id"] = "steam-1091500-build-123456"
        write_profile(profile_path, profile)
        code, evidence = run(profile_path, game_root, manifest)
        assert code == 0
        assert evidence["status"] == "READY_FOR_HEADSET_VALIDATION"
        assert evidence["next_gate"] == (
            "launch the engine-aware backend through VRClient and preserve OpenXR stereo evidence"
        )

        manifest.write_text(
            '"AppState"\n{\n  "appid" "1091500"\n  "buildid" "999"\n}\n',
            encoding="utf-8",
        )
        code, evidence = run(profile_path, game_root, manifest)
        assert code == 8
        assert evidence["status"] == "BLOCKED_BUILD_MISMATCH"

    print("Cyberpunk native preflight fail-closed tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

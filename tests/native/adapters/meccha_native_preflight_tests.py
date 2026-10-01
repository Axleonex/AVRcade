from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "tools" / "meccha_native_preflight.py"
PROFILE = ROOT / "config" / "unreal" / "native" / "meccha-chameleon.json"


def run(profile: Path, exe: Path, manifest: Path) -> tuple[int, dict[str, object]]:
    completed = subprocess.run(
        [
            sys.executable,
            str(SCRIPT),
            "--profile",
            str(profile),
            "--shipping-exe",
            str(exe),
            "--steam-manifest",
            str(manifest),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    return completed.returncode, json.loads(completed.stdout)


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="vrclient-meccha-preflight-") as temp:
        root = Path(temp)
        exe = root / "PenguinHotel-Win64-Shipping.exe"
        exe.write_bytes(b"controlled-meccha-shipping-fixture")
        profile = json.loads(PROFILE.read_text(encoding="utf-8"))
        pinned_buildid = profile["game"]["steam_buildid_observed"]
        manifest = root / "appmanifest_4704690.acf"
        manifest.write_text(
            '"AppState"\n{\n  "appid" "4704690"\n'
            f'  "buildid" "{pinned_buildid}"\n}}\n',
            encoding="utf-8",
        )
        profile["game"]["executable_sha256_observed"] = None
        profile_path = root / "profile.json"
        profile_path.write_text(json.dumps(profile), encoding="utf-8")

        code, evidence = run(profile_path, exe, manifest)
        assert code == 5
        assert evidence["status"] == "BLOCKED_FINGERPRINT_UNPINNED"
        actual_hash = hashlib.sha256(exe.read_bytes()).hexdigest()
        assert evidence["actual_sha256"] == actual_hash

        profile["game"]["executable_sha256_observed"] = "0" * 64
        profile_path.write_text(json.dumps(profile), encoding="utf-8")
        code, evidence = run(profile_path, exe, manifest)
        assert code == 6
        assert evidence["status"] == "BLOCKED_HASH_MISMATCH"

        profile["game"]["executable_sha256_observed"] = actual_hash
        profile_path.write_text(json.dumps(profile), encoding="utf-8")
        code, evidence = run(profile_path, exe, manifest)
        assert code == 0
        assert evidence["status"] == "READY_FOR_IN_PROCESS_OBSERVATION"

        manifest.write_text(
            '"AppState"\n{\n  "appid" "4704690"\n  "buildid" "999"\n}\n',
            encoding="utf-8",
        )
        code, evidence = run(profile_path, exe, manifest)
        assert code == 4
        assert evidence["status"] == "BLOCKED_BUILD_MISMATCH"

    print("Meccha native preflight fail-closed tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

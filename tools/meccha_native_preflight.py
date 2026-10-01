"""Read-only, fail-closed preflight for the native MECCHA CHAMELEON route."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def steam_build_id(manifest: Path) -> str | None:
    if not manifest.is_file():
        return None
    match = re.search(
        r'"buildid"\s+"(?P<buildid>[0-9]+)"',
        manifest.read_text(encoding="utf-8", errors="replace"),
    )
    return match.group("buildid") if match else None


def emit(evidence: dict[str, object], evidence_out: Path | None) -> int:
    text = json.dumps(evidence, indent=2, sort_keys=True)
    print(text)
    if evidence_out is not None:
        evidence_out.parent.mkdir(parents=True, exist_ok=True)
        evidence_out.write_text(text + "\n", encoding="utf-8")
    return int(evidence["exit_code"])


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Verify the installed Meccha shipping binary against the pinned native "
            "profile without launching or modifying the game."
        )
    )
    parser.add_argument(
        "--profile",
        type=Path,
        default=Path("config/unreal/native/meccha-chameleon.json"),
    )
    parser.add_argument("--shipping-exe", type=Path)
    parser.add_argument("--steam-manifest", type=Path)
    parser.add_argument("--evidence-out", type=Path)
    args = parser.parse_args()

    profile_path = args.profile.resolve()
    profile = json.loads(profile_path.read_text(encoding="utf-8"))
    expected_build = profile["game"]["steam_buildid_observed"]
    expected_hash = profile["game"]["executable_sha256_observed"]

    if args.shipping_exe is None:
        evidence = {
            "status": "BLOCKED_GAME_PATH_REQUIRED",
            "exit_code": 2,
            "profile": str(profile_path),
            "shipping_binary_relative": profile["game"]["shipping_binary"],
            "message": "Pass --shipping-exe with the installed shipping binary.",
        }
        return emit(evidence, args.evidence_out)

    shipping_exe = args.shipping_exe.resolve()
    if not shipping_exe.is_file():
        evidence = {
            "status": "BLOCKED_GAME_NOT_FOUND",
            "exit_code": 2,
            "profile": str(profile_path),
            "shipping_exe": str(shipping_exe),
        }
        return emit(evidence, args.evidence_out)

    actual_hash = sha256_file(shipping_exe)
    actual_build = (
        steam_build_id(args.steam_manifest.resolve())
        if args.steam_manifest is not None
        else None
    )
    base = {
        "profile": str(profile_path),
        "shipping_exe": str(shipping_exe),
        "shipping_executable_name": shipping_exe.name,
        "actual_sha256": actual_hash,
        "expected_sha256": expected_hash,
        "actual_steam_buildid": actual_build,
        "expected_steam_buildid": expected_build,
        "observed_at_utc": datetime.now(timezone.utc).isoformat(),
        "read_only": True,
    }

    expected_name = Path(
        profile["game"]["shipping_binary"].replace("\\", "/")
    ).name
    if shipping_exe.name.casefold() != expected_name.casefold():
        return emit(
            {
                **base,
                "status": "BLOCKED_WRONG_EXECUTABLE",
                "exit_code": 3,
                "expected_executable_name": expected_name,
            },
            args.evidence_out,
        )
    if actual_build is not None and actual_build != expected_build:
        return emit(
            {
                **base,
                "status": "BLOCKED_BUILD_MISMATCH",
                "exit_code": 4,
            },
            args.evidence_out,
        )
    if expected_hash is None:
        return emit(
            {
                **base,
                "status": "BLOCKED_FINGERPRINT_UNPINNED",
                "exit_code": 5,
                "message": (
                    "Review actual_sha256, then pin it in "
                    "game.executable_sha256_observed before native loading."
                ),
            },
            args.evidence_out,
        )
    if actual_hash.casefold() != str(expected_hash).casefold():
        return emit(
            {
                **base,
                "status": "BLOCKED_HASH_MISMATCH",
                "exit_code": 6,
            },
            args.evidence_out,
        )

    return emit(
        {
            **base,
            "status": "READY_FOR_IN_PROCESS_OBSERVATION",
            "exit_code": 0,
            "next_gate": "capture D3D12 device, direct queue, and swapchain",
        },
        args.evidence_out,
    )


if __name__ == "__main__":
    raise SystemExit(main())

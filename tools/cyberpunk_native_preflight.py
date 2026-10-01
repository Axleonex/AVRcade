"""Read-only, fail-closed preflight for the Cyberpunk 2077 REDengine route."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from datetime import datetime, timezone
from pathlib import Path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def steam_build_id(manifest: Path | None) -> str | None:
    if manifest is None or not manifest.is_file():
        return None
    match = re.search(
        r'"buildid"\s+"(?P<buildid>[0-9]+)"',
        manifest.read_text(encoding="utf-8", errors="replace"),
    )
    return match.group("buildid") if match else None


def resolve_profile_path(game_root: Path, relative: str) -> Path:
    parts = relative.replace("\\", "/").split("/")
    return game_root.joinpath(*parts)


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
            "Inspect a Cyberpunk 2077 installation for exact build identity, "
            "RED4ext readiness, and conflicting native render hooks without "
            "launching or modifying the game."
        )
    )
    parser.add_argument(
        "--profile",
        type=Path,
        default=Path("config/redengine/native/cyberpunk-2077.json"),
    )
    parser.add_argument("--game-root", type=Path)
    parser.add_argument("--steam-manifest", type=Path)
    parser.add_argument("--evidence-out", type=Path)
    args = parser.parse_args()

    profile_path = args.profile.resolve()
    profile = json.loads(profile_path.read_text(encoding="utf-8"))

    if args.game_root is None:
        return emit(
            {
                "status": "BLOCKED_GAME_PATH_REQUIRED",
                "exit_code": 2,
                "profile": str(profile_path),
                "message": "Pass --game-root with the Cyberpunk 2077 install root.",
                "read_only": True,
            },
            args.evidence_out,
        )

    game_root = args.game_root.resolve()
    if not game_root.is_dir():
        return emit(
            {
                "status": "BLOCKED_GAME_NOT_FOUND",
                "exit_code": 2,
                "profile": str(profile_path),
                "game_root": str(game_root),
                "read_only": True,
            },
            args.evidence_out,
        )

    shipping_exe = resolve_profile_path(
        game_root,
        profile["game"]["shipping_binary"],
    )
    if not shipping_exe.is_file():
        return emit(
            {
                "status": "BLOCKED_SHIPPING_BINARY_NOT_FOUND",
                "exit_code": 3,
                "profile": str(profile_path),
                "game_root": str(game_root),
                "shipping_exe": str(shipping_exe),
                "read_only": True,
            },
            args.evidence_out,
        )

    actual_hash = sha256_file(shipping_exe)
    manifest = args.steam_manifest.resolve() if args.steam_manifest else None
    actual_build = steam_build_id(manifest)
    expected_hash = profile["game"]["executable_sha256_observed"]
    expected_build = profile["game"]["steam_buildid_observed"]

    missing_dependencies = []
    for dependency in profile["dependencies"]:
        path = resolve_profile_path(game_root, dependency["path"])
        if not path.exists():
            missing_dependencies.append(
                {
                    "path": dependency["path"],
                    "reason": dependency["reason"],
                }
            )

    conflicts = []
    for conflict in profile["conflicts"]:
        path = resolve_profile_path(game_root, conflict["path"])
        if path.exists():
            conflicts.append(
                {
                    "path": conflict["path"],
                    "reason": conflict["reason"],
                }
            )

    base = {
        "profile": str(profile_path),
        "game_root": str(game_root),
        "shipping_exe": str(shipping_exe),
        "actual_sha256": actual_hash,
        "expected_sha256": expected_hash,
        "actual_steam_buildid": actual_build,
        "expected_steam_buildid": expected_build,
        "adapter_build_id": profile["game"]["adapter_build_id"],
        "missing_dependencies": missing_dependencies,
        "conflicts": conflicts,
        "observed_at_utc": datetime.now(timezone.utc).isoformat(),
        "read_only": True,
    }

    if conflicts:
        return emit(
            {
                **base,
                "status": "BLOCKED_CONFLICTING_RENDER_HOOKS",
                "exit_code": 4,
                "message": "Remove or disable the listed conflicts before VRClient loads.",
            },
            args.evidence_out,
        )

    if missing_dependencies:
        return emit(
            {
                **base,
                "status": "BLOCKED_DEPENDENCIES_REQUIRED",
                "exit_code": 5,
                "message": "Install every pinned native dependency before bridge loading.",
            },
            args.evidence_out,
        )

    if expected_hash is None or expected_build is None:
        return emit(
            {
                **base,
                "status": "BLOCKED_FINGERPRINT_UNPINNED",
                "exit_code": 6,
                "message": (
                    "Review actual_sha256 and actual_steam_buildid, then pin both "
                    "with a matching adapter_build_id before native loading."
                ),
            },
            args.evidence_out,
        )

    if actual_build is None:
        return emit(
            {
                **base,
                "status": "BLOCKED_STEAM_BUILD_UNKNOWN",
                "exit_code": 7,
                "message": "Pass --steam-manifest for the installed Steam copy.",
            },
            args.evidence_out,
        )

    if actual_build != str(expected_build):
        return emit(
            {
                **base,
                "status": "BLOCKED_BUILD_MISMATCH",
                "exit_code": 8,
            },
            args.evidence_out,
        )

    if actual_hash.casefold() != str(expected_hash).casefold():
        return emit(
            {
                **base,
                "status": "BLOCKED_HASH_MISMATCH",
                "exit_code": 9,
            },
            args.evidence_out,
        )

    if not profile["game"]["adapter_build_id"]:
        return emit(
            {
                **base,
                "status": "BLOCKED_ADAPTER_BUILD_ID_UNPINNED",
                "exit_code": 10,
            },
            args.evidence_out,
        )

    graphics_state = (
        profile.get("conversion", {})
        .get("capabilities", {})
        .get("graphics_bridge", "planned")
    )
    observer_ready = graphics_state in {
        "controlled_verified",
        "game_observed",
        "game_verified",
    }
    observer_game_verified = graphics_state == "game_verified"
    stereo_state = (
        profile.get("conversion", {})
        .get("capabilities", {})
        .get("stereo_views", "planned")
    )
    stereo_observed = stereo_state in {"game_observed", "game_verified"}
    engine_aware_backend = any(
        dependency["path"].replace("\\", "/").casefold()
        == "red4ext/plugins/cyberpunkvr_stereo/cyberpunkvr_stereo.dll"
        for dependency in profile["dependencies"]
    )
    return emit(
        {
            **base,
            "status": (
                "READY_FOR_HEADSET_VALIDATION"
                if engine_aware_backend
                else
                (
                    "READY_FOR_OPENXR_INTEGRATION"
                    if stereo_observed
                    else "READY_TO_RUN_VRCAM_PROBE"
                )
                if observer_game_verified
                else (
                    "READY_FOR_GAME_OBSERVATION"
                    if observer_ready
                    else "READY_FOR_RED4EXT_BRIDGE"
                )
            ),
            "exit_code": 0,
            "next_gate": (
                "launch the engine-aware backend through VRClient and preserve OpenXR stereo evidence"
                if engine_aware_backend
                else
                (
                    "connect the observed VRCAM resource to VRClient OpenXR submission"
                    if stereo_observed
                    else "launch into gameplay once and preserve VRCAM resource evidence"
                )
                if observer_game_verified
                else (
                    "launch once and preserve RED4ext graphics-observer evidence"
                    if observer_ready
                    else "load no-op bridge and observe D3D12 device/queue/Present"
                )
            ),
        },
        args.evidence_out,
    )


if __name__ == "__main__":
    raise SystemExit(main())

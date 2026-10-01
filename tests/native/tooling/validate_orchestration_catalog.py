from __future__ import annotations

import json
from datetime import datetime
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[3]
CONFIG = ROOT / "config"


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def validate(path: Path, schema_name: str) -> dict:
    document = load(path)
    schema = load(CONFIG / "schemas" / schema_name)
    jsonschema.validate(document, schema)
    return document


def main() -> int:
    rules = validate(CONFIG / "safety" / "default-rules.json", "safety-rules.schema.json")
    rules_by_game = {item["game_id"]: item for item in rules["rules"]}
    modpack_paths = sorted((CONFIG / "modpacks").glob("*.modpack.json"))
    require(bool(modpack_paths), "no production modpacks found")

    checked: list[str] = []
    for modpack_path in modpack_paths:
        slug = modpack_path.name.removesuffix(".modpack.json")
        modpack = validate(modpack_path, "modpack-spec.schema.json")
        require(modpack["game_slug"] == slug, f"{slug}: modpack game_slug mismatch")

        comfort_path = CONFIG / "modpacks" / f"{slug}.comfort-map.json"
        lock_path = CONFIG / "modpacks" / f"{slug}.lock.json"
        game_path = CONFIG / "games" / f"{slug}.json"
        profile_path = CONFIG / "profiles" / f"{slug}-game-profile.json"
        for path in (comfort_path, lock_path, game_path, profile_path):
            require(path.is_file(), f"{slug}: missing {path.relative_to(ROOT)}")

        comfort = validate(comfort_path, "comfort-map.schema.json")
        lockfile = validate(lock_path, "modpack-lock.schema.json")
        game_raw = load(game_path)
        game = validate(game_path, "orchestrated-game.schema.json" if "vr_mod" in game_raw
                        else "game-fingerprint.schema.json")
        validate(profile_path, "orchestrated-profile.schema.json" if "vr_mod" in game
                 else "game-profile.schema.json")

        require(comfort["game_slug"] == slug, f"{slug}: comfort-map game_slug mismatch")
        if "vr_mod" not in game:
            require(comfort["verified_against_cfg"] is True,
                    f"{slug}: comfort map has not been verified against a real mod cfg/source")
        else:
            require(comfort["verified_against_cfg"] or comfort["map"] == {},
                    f"{slug}: unverified comfort map must not claim settings")
        require(lockfile["game_slug"] == slug, f"{slug}: lockfile game_slug mismatch")
        require(lockfile["community"] == modpack["community"],
                f"{slug}: lockfile community mismatch")
        require(game["game_id"] == slug, f"{slug}: game config id mismatch")
        require(slug in rules_by_game, f"{slug}: no safety rule")

        resolved = lockfile["resolved_at_utc"]
        require(resolved.endswith("Z"), f"{slug}: lockfile time must be UTC/Z")
        datetime.fromisoformat(resolved.removesuffix("Z") + "+00:00")

        locked = {(item["namespace"], item["name"]) for item in lockfile["packages"]}
        declared = {(item["namespace"], item["name"]) for item in modpack["mods"]}
        missing_declared = declared - locked
        require(not missing_declared,
                f"{slug}: declared packages absent from lock: {sorted(missing_declared)}")
        require(len(locked) == len(lockfile["packages"]),
                f"{slug}: duplicate package in lockfile")

        checked.append(slug)

    validate(CONFIG / "unreal" / "uevr-release.json", "uevr-release.schema.json")
    validate(CONFIG / "unreal" / "dotnet-desktop-runtime.json", "dotnet-desktop-tool.schema.json")
    unreal_paths = sorted((CONFIG / "unreal").glob("*.uevr.json"))
    require(bool(unreal_paths), "no UEVR catalog entries found")
    unreal_checked: list[str] = []
    for path in unreal_paths:
        game = validate(path, "uevr-game.schema.json")
        slug = path.name.removesuffix(".uevr.json")
        require(game["game_slug"] == slug and game["game_id"] == slug,
                f"{slug}: UEVR slug/id mismatch")
        require(slug not in checked, f"{slug}: duplicated in Unity and Unreal catalogs")
        require(slug in rules_by_game, f"{slug}: no safety rule")
        verification = game["verification"]
        profile = game["uevr_profile"]
        if "source" in profile:
            require(profile.get("required_uevr_channel") == game["uevr_channel"],
                    f"{slug}: profile/runtime UEVR channels disagree")
            require("supported_builds" in profile and "capabilities" in profile,
                    f"{slug}: sourced profile requires build and capability fields")
            imported = [item for item in profile["source"]["files"]
                        if item["disposition"] == "import"]
            require(bool(imported), f"{slug}: profile source has no importable files")
            require(len({item["path"].lower() for item in imported}) == len(imported),
                    f"{slug}: duplicate profile source paths")
        capabilities = profile.get("capabilities", {})
        if capabilities.get("motion_controls") == "verified":
            require(verification["profile_status"] == "verified",
                    f"{slug}: motion controls cannot be verified on an unverified profile")
        if verification["profile_status"] == "verified":
            profile_dir = CONFIG / "unreal" / "profiles" / slug
            require(profile_dir.is_dir(), f"{slug}: missing UEVR profile directory")
            require(any(item.is_file() for item in profile_dir.rglob("*")),
                    f"{slug}: UEVR profile directory is empty")
            require(verification["injection_stable"],
                    f"{slug}: verified profile requires stable injection")
            require(verification["headset_t1"],
                    f"{slug}: verified profile requires headset T1")
            if "source" in profile:
                require(bool(profile["supported_builds"]),
                        f"{slug}: verified profile requires at least one supported build")
                require(capabilities["headset_stereo"] == "verified",
                        f"{slug}: verified profile requires verified stereo")
                require(capabilities["head_tracking"] == "verified",
                        f"{slug}: verified profile requires verified head tracking")
            else:
                require(bool(verification.get("head_bone")),
                        f"{slug}: legacy verified profile requires a locked head bone")
                require(bool(verification.get("paint_mode_property")),
                        f"{slug}: legacy verified profile requires a paint-mode property")
            require(bool(verification["evidence"]),
                    f"{slug}: verified profile requires an evidence path")
            require((ROOT / verification["evidence"]).is_file(),
                    f"{slug}: verification evidence file does not exist")
        unreal_checked.append(slug)

    print(
        "orchestration catalog schema/cross-file check passed for "
        f"Unity={', '.join(checked)}; Unreal={', '.join(unreal_checked)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

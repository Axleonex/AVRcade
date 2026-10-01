from __future__ import annotations

import json
import sys
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[3]
SCHEMA = ROOT / "config" / "schemas" / "hook-surface.schema.json"
HOOKS = ROOT / "config" / "hooks"

VALID_AREAS = {
    "camera",
    "projection",
    "hud",
    "input",
    "interaction",
    "mode_state",
    "network_block_marker",
}
VALID_STATUS = {"candidate", "validated", "blocked"}
VALID_CONFIDENCE = {"low", "medium", "high"}
VALID_METHODS = {"hardware_breakpoint", "vtable_observation", "pass_through_detour"}
VALID_THREAD_CONTEXTS = {"render", "main", "worker", "unknown"}

REPO_BUILD_ID = "steam-3241660-build-23363152"
REPO_BLOCK_MARKERS = {
    "MainMenuIsMultiplayer",
    "MenuActionRandomMatchmaking",
    "PhotonNetwork",
    "PhotonView",
    "PhotonVoice",
    "LeavePhotonRoom",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def validate_hook(label: str, hook: dict) -> None:
    require(bool(hook.get("name")), f"{label}: hook needs a non-empty name")
    require(hook["area"] in VALID_AREAS, f"{label}: invalid area {hook.get('area')!r}")
    require(bool(hook.get("module")), f"{label}: hook needs a non-empty module")
    require(hook["status"] in VALID_STATUS, f"{label}: invalid status {hook.get('status')!r}")
    require(
        hook["confidence"] in VALID_CONFIDENCE,
        f"{label}: invalid confidence {hook.get('confidence')!r}",
    )

    locators = [hook.get("symbol"), hook.get("signature"), hook.get("offset")]
    require(
        any(isinstance(value, str) and value for value in locators),
        f"{label}: hook needs at least one of symbol, signature, or offset",
    )

    if hook["status"] == "validated":
        validation = hook.get("validation")
        require(
            isinstance(validation, dict),
            f"{label}: status=validated requires a validation evidence object",
        )
        require(
            validation.get("method") in VALID_METHODS,
            f"{label}: validated hook needs a recognized validation method",
        )
        require(
            validation.get("thread_context") in VALID_THREAD_CONTEXTS,
            f"{label}: validated hook needs a thread_context",
        )
        require(
            isinstance(validation.get("hit_count"), int) and validation["hit_count"] >= 1,
            f"{label}: validated hook needs hit_count >= 1",
        )
        require(
            bool(validation.get("captured_at")),
            f"{label}: validated hook needs a captured_at timestamp",
        )
        cadence = validation.get("cadence_hz")
        per_frame = validation.get("per_frame")
        require(
            (isinstance(cadence, (int, float)) and not isinstance(cadence, bool) and cadence > 0)
            or isinstance(per_frame, bool),
            f"{label}: validated hook needs observed cadence_hz or a per_frame flag",
        )

    if hook["status"] == "blocked":
        blocked_reason = hook.get("blocked_reason")
        require(
            isinstance(blocked_reason, str) and bool(blocked_reason),
            f"{label}: status=blocked requires a non-empty blocked_reason",
        )


def validate_surface(path: Path, schema: dict) -> None:
    surface = json.loads(path.read_text(encoding="utf-8"))
    jsonschema.validate(surface, schema)

    require(surface["version"] == 1, f"{path.name}: version must be 1")
    require(bool(surface["game_id"]), f"{path.name}: game_id must be non-empty")
    require(bool(surface["build_id"]), f"{path.name}: build_id must be non-empty")
    require(bool(surface["generated_by"]), f"{path.name}: generated_by must be non-empty")
    require(len(surface["hooks"]) >= 1, f"{path.name}: hooks must not be empty")

    names = [hook["name"] for hook in surface["hooks"]]
    require(
        len(names) == len(set(names)),
        f"{path.name}: hook names must be unique",
    )

    for hook in surface["hooks"]:
        validate_hook(f"{path.name}.{hook.get('name', '<unnamed>')}", hook)

    if surface["game_id"] == "repo":
        require(
            surface["build_id"] == REPO_BUILD_ID,
            f"{path.name}: repo hook surface must pin build {REPO_BUILD_ID}",
        )
        by_name = {hook["name"]: hook for hook in surface["hooks"]}
        missing = REPO_BLOCK_MARKERS - by_name.keys()
        require(not missing, f"{path.name}: missing repo block markers: {sorted(missing)}")
        for marker in REPO_BLOCK_MARKERS:
            require(
                by_name[marker]["area"] == "network_block_marker",
                f"{path.name}: {marker} must keep area network_block_marker",
            )


def main() -> int:
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator.check_schema(schema)

    if len(sys.argv) > 1:
        paths = [Path(arg) if Path(arg).is_absolute() else ROOT / arg for arg in sys.argv[1:]]
    else:
        paths = sorted(HOOKS.glob("*.json"))

    require(len(paths) >= 1, "no hook-surface documents found to validate")
    for path in paths:
        validate_surface(path, schema)

    def display(path: Path) -> str:
        return str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path)

    relative = ", ".join(display(path) for path in paths)
    print(f"validated {relative} against {SCHEMA.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

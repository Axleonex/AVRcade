from __future__ import annotations

import json
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[3]
SCHEMA = ROOT / "config" / "schemas" / "diagnostics-profile.schema.json"
DEFAULT = ROOT / "config" / "defaults" / "diagnostics-profile.json"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    profile = json.loads(DEFAULT.read_text(encoding="utf-8"))
    jsonschema.validate(profile, schema)

    required = set(schema["required"])
    require(required.issubset(profile.keys()), f"missing required keys: {required - profile.keys()}")
    require(profile["version"] == 1, "diagnostics profile version must be 1")
    require(profile["enabled"] is True, "diagnostics should default enabled for Phase 2")

    logging = profile["logging"]
    require(logging["enabled"] is True, "logging defaults enabled")
    require(logging["max_buffer_records"] >= 1, "logging buffer must be bounded and positive")
    require(logging["max_retained_logs"] >= 1, "retained log count must be positive")
    require(logging["flush_on_shutdown"] is True, "shutdown flush should be enabled")

    crash = profile["crash_capture"]
    require(crash["enabled"] is True, "crash capture defaults enabled")
    require(crash["max_recent_records"] >= 1, "crash artifact must include recent records")

    export = profile["export"]
    require(export["automatic_upload"] is False, "diagnostics export must not auto-upload")
    require(export["directory"], "export directory must be configured")

    telemetry = profile["telemetry_aggregation"]
    require(telemetry["enabled"] is False, "telemetry aggregation must default off")
    require(telemetry["local_only"] is True, "telemetry aggregation must be local-only")
    require(
        telemetry["automatic_upload"] is False,
        "telemetry aggregation must not auto-upload",
    )

    print(f"diagnostics profile schema check passed for {DEFAULT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

"""DIAG-01 closure: all native runtime layers log through ONE structured pipeline.

The single pipeline is `vrclient::diagnostics::AsyncLogger` (diagnostic_logger.h).
This validator proves two things, and is able to FAIL:

  1. No native runtime LIBRARY source performs ad-hoc/raw logging
     (std::cout / std::cerr / printf / fprintf / OutputDebugString). Command-line
     TOOL entrypoints (`*_main.cpp`) and the standalone hook-discovery CLI are
     console programs by design and are excluded; `std::snprintf` used purely for
     numeric formatting (not logging) is excluded by matching whole tokens only.

  2. Every native runtime layer that emits diagnostics does so against the single
     AsyncLogger type — either by including diagnostic_logger.h and taking an
     `AsyncLogger*`, or (for leaf SDK-facing layers) by routing through the
     host-provided diagnostics callback. We assert the cross-layer logging
     surfaces (runtime, injector, versioning, plugin host) reference AsyncLogger.

If a future change introduces a second logging path (e.g. a module that prints
directly instead of going through AsyncLogger), check 1 fails loudly.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
NATIVE = ROOT / "src" / "native"

# The single pipeline.
PIPELINE_HEADER = NATIVE / "diagnostics" / "logging" / "diagnostic_logger.h"

# Raw-logging tokens that would constitute a SECOND, unstructured pipeline.
RAW_LOGGING = re.compile(r"\b(std::cout|std::cerr|printf|fprintf|OutputDebugString)\b")

# Console TOOL entrypoints are allowed to print directly — they are not runtime
# layers and do not participate in the structured session log.
EXCLUDED_FILES = {
    "src/native/tooling/hookdisc/hookdisc_cli.cpp",
    "src/native/tooling/hookdisc/obs_host_main.cpp",
    "src/native/plugins/validation/validator_main.cpp",
    "src/native/injector/bootstrap/smoke_host_main.cpp",
    "src/native/runtime/harness/main.cpp",
    # M5 injection-free CLIs: console tools that print one machine-readable line
    # to stdout for the .NET client to parse (RULECHECK / SIGN); not runtime layers.
    "src/native/safety/cli/vrclient_safety_cli.cpp",
    "src/native/supply_chain/cli/vrclient_sign_cli.cpp",
}

# Lower-level layers whose logging surface MUST take the single AsyncLogger
# directly (they are below the DiagnosticsSystem facade).
PIPELINE_CONSUMERS = [
    "src/native/injector/process/process_discovery.h",
    "src/native/injector/bootstrap/bootstrap_smoke.h",
    "src/native/versioning/game_fingerprint.h",
    "src/native/plugins/host/plugin_host.h",
]

# The runtime layer logs through the DiagnosticsSystem facade (which owns exactly
# one AsyncLogger — asserted below), not by naming AsyncLogger directly.
FACADE_CONSUMERS = [
    "src/native/runtime/openxr/openxr_runtime.cpp",
]


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    require(PIPELINE_HEADER.exists(), "the single logging pipeline header must exist")
    pipeline = read(PIPELINE_HEADER)
    require("class AsyncLogger" in pipeline, "pipeline must define AsyncLogger")
    require("struct SessionMetadata" in pipeline, "pipeline must carry session metadata")

    # 1) No raw logging in any non-excluded native source (the FAIL-able check).
    offenders: list[str] = []
    for path in sorted(NATIVE.rglob("*.cpp")) + sorted(NATIVE.rglob("*.h")):
        rel = path.relative_to(ROOT).as_posix()
        if rel in EXCLUDED_FILES:
            continue
        if RAW_LOGGING.search(read(path)):
            offenders.append(rel)
    require(
        not offenders,
        f"native runtime layers must log through AsyncLogger, not raw stdio: {offenders}",
    )

    # 2) The cross-layer logging surfaces route through the single pipeline.
    for rel in PIPELINE_CONSUMERS:
        path = ROOT / rel
        require(path.exists(), f"expected pipeline consumer missing: {rel}")
        text = read(path)
        require(
            "AsyncLogger" in text,
            f"{rel} must log through the single AsyncLogger pipeline",
        )

    # 3) The diagnostics facade unifies logger + crash + overlay on one logger.
    facade = read(NATIVE / "diagnostics" / "diagnostics_system.h")
    require("AsyncLogger logger_" in facade, "diagnostics system must own one logger")
    require("CrashCapture crash_capture_" in facade, "crash capture rides the same system")
    require("DiagnosticsOverlay overlay_" in facade, "overlay rides the same system")

    # 4) The runtime layer routes through that single-logger facade.
    for rel in FACADE_CONSUMERS:
        path = ROOT / rel
        require(path.exists(), f"expected facade consumer missing: {rel}")
        text = read(path)
        require(
            "diagnostics_." in text,
            f"{rel} must log through the DiagnosticsSystem facade (single logger)",
        )

    print("DIAG-01 single-pipeline checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

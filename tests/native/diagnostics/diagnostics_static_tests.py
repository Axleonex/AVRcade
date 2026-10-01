from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    expected_files = [
        "src/native/diagnostics/logging/diagnostic_logger.h",
        "src/native/diagnostics/logging/diagnostic_logger.cpp",
        "src/native/diagnostics/crash/crash_capture.h",
        "src/native/diagnostics/crash/crash_capture.cpp",
        "src/native/diagnostics/overlay/diagnostics_overlay.h",
        "src/native/diagnostics/overlay/diagnostics_overlay.cpp",
        "src/native/diagnostics/export/diagnostics_exporter.h",
        "src/native/diagnostics/export/diagnostics_exporter.cpp",
        "src/native/diagnostics/diagnostics_system.h",
        "src/native/diagnostics/diagnostics_system.cpp",
    ]
    for rel in expected_files:
        require((ROOT / rel).exists(), f"missing diagnostics artifact: {rel}")

    logger_cpp = read(ROOT / "src/native/diagnostics/logging/diagnostic_logger.cpp")
    require("std::try_to_lock" in logger_cpp, "logger enqueue must avoid blocking callers")
    require("max_buffer_records" in logger_cpp, "logger must enforce bounded queue")
    require("workerLoop" in logger_cpp and "std::ofstream" in logger_cpp, "logger must flush on worker path")
    require("dropped_records_" in logger_cpp, "logger must track overflow policy")

    crash_cpp = read(ROOT / "src/native/diagnostics/crash/crash_capture.cpp")
    require("fallback_crash_report" in crash_cpp, "crash capture must write fallback artifact")
    require("recent_log_records" in crash_cpp, "crash artifact must include recent log records")
    require("crash_artifact_write_failed" in crash_cpp, "crash write failures must be logged")

    overlay_cpp = read(ROOT / "src/native/diagnostics/overlay/diagnostics_overlay.cpp")
    require("std::try_to_lock" in overlay_cpp, "overlay updates must not block the frame path")
    require("renderTextLines" in overlay_cpp, "overlay must expose displayable state lines")

    export_cpp = read(ROOT / "src/native/diagnostics/export/diagnostics_exporter.cpp")
    require("automatic_upload" in export_cpp and "false" in export_cpp, "export must be local and opt-in")
    require("[REDACTED]" in export_cpp, "export must redact sensitive fields")
    export_h = read(ROOT / "src/native/diagnostics/export/diagnostics_exporter.h")
    require("include_telemetry_summary" in export_h,
            "telemetry aggregation must be explicit opt-in on the export request")
    require("network_upload" in export_cpp and "false" in export_cpp,
            "telemetry aggregation summary must record no network upload")
    forbidden_network_tokens = [
        "#include <winsock",
        "#include <winhttp",
        "#include <wininet",
        "WinHttp",
        "InternetOpen",
        "HttpSendRequest",
        "WSAStartup",
        "URLDownloadToFile",
        "curl_easy",
    ]
    for token in forbidden_network_tokens:
        require(token not in export_cpp, f"diagnostics export must not add network/upload code: {token}")

    runtime_files = list((ROOT / "src/native/runtime").rglob("*.cpp"))
    direct_logging_pattern = re.compile(r"\b(std::cout|std::cerr|printf|fprintf|OutputDebugString)\b")
    offenders: list[str] = []
    for path in runtime_files:
      text = read(path)
      if direct_logging_pattern.search(text):
          offenders.append(path.relative_to(ROOT).as_posix())
    require(not offenders, f"direct runtime logging remains: {offenders}")

    openxr = read(ROOT / "src/native/runtime/openxr/openxr_runtime.cpp")
    require("diagnostics_.logStateTransition" in openxr, "runtime state transitions must log")
    require("diagnostics_.logRuntimeError" in openxr, "runtime failures must log")
    require("diagnostics_.updateFrameSnapshot" in openxr, "runtime must publish overlay snapshots")

    print("diagnostics static checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

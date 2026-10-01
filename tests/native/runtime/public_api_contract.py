from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
HEADER = ROOT / "src" / "native" / "runtime" / "public" / "vr_runtime_api.h"
SOURCE = ROOT / "src" / "native" / "runtime" / "public" / "vr_runtime_api.cpp"


def enum_value(text: str, symbol: str) -> int:
    match = re.search(rf"\b{re.escape(symbol)}\s*=\s*(\d+)", text)
    if match is None:
        raise AssertionError(f"public API missing enum value for {symbol}")
    return int(match.group(1))


def main() -> int:
    text = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
    forbidden_includes = ["openxr", "oculus", "steamvr", "wmr"]
    lowered = text.lower()
    for include in forbidden_includes:
        if include in lowered:
            raise AssertionError(f"public API leaks forbidden dependency: {include}")

    leaked_xr_types = sorted(set(re.findall(r"\bXr[A-Z][A-Za-z0-9_]*", text)))
    if leaked_xr_types:
        raise AssertionError(f"public API leaks OpenXR type names: {leaked_xr_types}")

    required_symbols = [
        "vr_runtime_create",
        "vr_runtime_start",
        "vr_runtime_run_frame",
        "vr_runtime_get_frame_data",
        "vr_runtime_sample_head_pose",
        "VrRuntimeRenderCallback",
        "VrRuntimeFrameTiming",
        "VrRuntimeHeadsetState",
        "VR_RUNTIME_GRAPHICS_BACKEND_D3D12",
    ]
    missing = [symbol for symbol in required_symbols if symbol not in text]
    if missing:
        raise AssertionError(f"public API missing required symbols: {missing}")

    if not re.search(
        r"vr_runtime_sample_head_pose[\s\S]*safe to call concurrently with\s+vr_runtime_run_frame",
        text,
    ):
        raise AssertionError(
            "public API must document concurrent pose sampling without allowing concurrent lifecycle calls"
        )

    backend_values = {
        "VR_RUNTIME_GRAPHICS_BACKEND_D3D11": 1,
        "VR_RUNTIME_GRAPHICS_BACKEND_VULKAN": 2,
        "VR_RUNTIME_GRAPHICS_BACKEND_D3D12": 3,
    }
    for symbol, expected in backend_values.items():
        actual = enum_value(text, symbol)
        if actual != expected:
            raise AssertionError(
                f"{symbol} ABI value drifted: expected {expected}, got {actual}"
            )

    for accepted in ("VR_RUNTIME_GRAPHICS_BACKEND_VULKAN", "VR_RUNTIME_GRAPHICS_BACKEND_D3D12"):
        if accepted not in source:
            raise AssertionError(f"runtime create path does not accept {accepted}")
    if "VR_RUNTIME_GRAPHICS_BACKEND_D3D11" in source:
        raise AssertionError("legacy D3D11 must not be accepted by the runtime create path")

    create_start = source.find("VrRuntimeResult vr_runtime_create(")
    create_end = source.find("void vr_runtime_destroy(", create_start)
    create_body = source[create_start:create_end]
    if not re.search(
        r"if \(runtime == nullptr\)[\s\S]*\*runtime = nullptr;",
        create_body,
    ):
        raise AssertionError("runtime creation must clear valid output pointers first")
    if "try {" not in create_body or "catch (...)" not in create_body:
        raise AssertionError("runtime construction must not throw through the C ABI")
    if create_body.count("VR_RUNTIME_ERROR_RUNTIME_UNAVAILABLE") < 3:
        raise AssertionError("runtime construction failures need a stable C result")

    print(f"public API contract passed for {HEADER.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

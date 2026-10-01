from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
HOOKDISC = ROOT / "src" / "native" / "tooling" / "hookdisc"
CMAKE = ROOT / "CMakeLists.txt"

SOURCES = [
    "hook_surface_doc.h",
    "hook_surface_doc.cpp",
    "inspection.h",
    "inspection.cpp",
    "observe.h",
    "observe.cpp",
    "safety_gate.h",
    "safety_gate.cpp",
    "byte_patch_detour.h",
    "hookdisc_cli.cpp",
]

# APIs that perform code/memory tampering, remote injection, or anti-cheat/
# anti-debug evasion. NONE may appear anywhere in the v1 hookdisc sources.
# (Legitimate read-only observation APIs used here -- OpenThread, SuspendThread,
# GetThreadContext, SetThreadContext for debug registers, VirtualQuery,
# CreateToolhelp32Snapshot, AddVectoredExceptionHandler -- are deliberately NOT
# on this list; debug registers do not modify target code bytes.)
FORBIDDEN_APIS = [
    "WriteProcessMemory",
    "NtWriteVirtualMemory",
    "CreateRemoteThread",
    "NtCreateThreadEx",
    "RtlCreateUserThread",
    "QueueUserAPC",
    "SetWindowsHookEx",
    "VirtualAllocEx",
    "VirtualProtectEx",
    "NtSetInformationThread",     # ThreadHideFromDebugger lives here
    "ThreadHideFromDebugger",
]

BYTE_PATCH_MACRO = "VRCLIENT_HOOKDISC_ENABLE_BYTE_PATCH_DETOUR"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def main() -> int:
    for name in SOURCES:
        path = HOOKDISC / name
        require(path.exists(), f"missing hookdisc source: {path.relative_to(ROOT)}")

    combined = "\n".join(read(HOOKDISC / name) for name in SOURCES)

    # 1. No evasion / tampering / remote-injection APIs anywhere in v1 sources.
    pattern = re.compile(r"\b(" + "|".join(re.escape(api) for api in FORBIDDEN_APIS) + r")\b")
    hits = sorted(set(pattern.findall(combined)))
    require(
        not hits,
        f"hookdisc sources contain evasion/tampering/injection APIs: {hits}",
    )

    # 2. The byte-patching pass-through detour must stay compile-time disabled.
    detour = read(HOOKDISC / "byte_patch_detour.h")
    require(BYTE_PATCH_MACRO in detour, "byte-patch detour guard macro missing")
    require(
        f"#if defined({BYTE_PATCH_MACRO})" in detour,
        "byte-patch detour must be wrapped in an #if defined guard",
    )
    require(
        "HUMAN SIGN-OFF" in detour,
        "byte-patch detour must carry the human sign-off requirement comment",
    )
    cmake = read(CMAKE)
    # Ignore CMake comment lines: the macro may be NAMED in a comment explaining
    # that it stays disabled; what must never appear is an actual definition.
    cmake_code = "\n".join(
        line for line in cmake.splitlines() if not line.lstrip().startswith("#")
    )
    require(
        BYTE_PATCH_MACRO not in cmake_code,
        "CMake must NOT define the byte-patch detour macro (keep it disabled)",
    )

    # 3. Read-only observation mechanism is hardware-breakpoint + VEH based.
    observe = read(HOOKDISC / "observe.cpp")
    for token in ["AddVectoredExceptionHandler", "Dr7", "EXCEPTION_SINGLE_STEP",
                  "CONTEXT_DEBUG_REGISTERS"]:
        require(token in observe, f"observe.cpp missing read-only HW-breakpoint token {token}")

    # 4. Safety gate honors the existing preflight (RE-05).
    gate = read(HOOKDISC / "safety_gate.cpp")
    require("runSafetyPreflight" in gate, "safety_gate.cpp must call runSafetyPreflight")
    require("makePreflightRequest" in gate, "safety_gate.cpp must build a preflight request")

    # 5. Inspection harness is read-only PE/RTTI inspection.
    inspection = read(HOOKDISC / "inspection.cpp")
    for token in ["CreateToolhelp32Snapshot", "IMAGE_EXPORT_DIRECTORY", ".?AV", "VirtualQuery"]:
        require(token in inspection, f"inspection.cpp missing read-only token {token}")

    # 6. CMake wires the new (additive) targets and tests.
    for token in ["vr_tooling_hookdisc", "vrclient_hookdisc", "vr_hookdisc_unit_tests",
                  "hook_surface_schema", "hookdisc_static_checks"]:
        require(token in cmake, f"CMakeLists.txt missing hookdisc wiring {token}")

    print("hookdisc static safety checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

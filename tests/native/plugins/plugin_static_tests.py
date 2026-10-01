from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / "src" / "native" / "plugins" / "sdk"
HOST = ROOT / "src" / "native" / "plugins" / "host"
VALIDATION = ROOT / "src" / "native" / "plugins" / "validation"
TEMPLATE = ROOT / "adapters" / "_template"
DOCS = ROOT / "docs" / "sdk" / "adapter-authoring.md"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    expected = [
        SDK / "igame_adapter.h",
        SDK / "adapter_context.h",
        SDK / "adapter_metadata.h",
        SDK / "shared_services.h",
        HOST / "plugin_host.h",
        HOST / "plugin_host.cpp",
        HOST / "plugin_library.h",
        HOST / "plugin_library.cpp",
        VALIDATION / "adapter_validator.h",
        VALIDATION / "adapter_validator.cpp",
        VALIDATION / "validator_main.cpp",
        TEMPLATE / "template_adapter.cpp",
        TEMPLATE / "adapter.json",
        DOCS,
    ]
    for path in expected:
        require(path.exists(), f"missing Phase 4 artifact: {path.relative_to(ROOT)}")

    sdk_text = "\n".join(read(path) for path in SDK.glob("*.h"))
    require("VRCLIENT_ADAPTER_ABI_VERSION" in sdk_text, "SDK must define ABI version")
    require("VRCLIENT_ADAPTER_CALL" in sdk_text, "SDK must define calling convention")
    require("vrclient_get_adapter_abi" in sdk_text, "SDK must name ABI export")
    require("vrclient_create_adapter" in sdk_text, "SDK must name factory export")
    require("uint32_t size" in sdk_text, "ABI structs must carry size fields")
    require("VrAdapterHostServices" in sdk_text, "SDK must expose host-owned services")
    require("VRCLIENT_ADAPTER_SERVICE_INPUT" in sdk_text, "SDK must reserve Phase 5 input service")
    require("VRCLIENT_ADAPTER_SERVICE_COMFORT" in sdk_text, "SDK must reserve Phase 5 comfort service")
    require("VRCLIENT_ADAPTER_SERVICE_HUD" in sdk_text, "SDK must reserve Phase 5 HUD service")
    require("VrClientInputService" in sdk_text, "SDK must expose input service struct")
    require("VrClientComfortService" in sdk_text, "SDK must expose comfort service struct")
    require("VrClientHudService" in sdk_text, "SDK must expose HUD service struct")
    require("VRCLIENT_SHARED_SERVICE_VERSION" in sdk_text, "shared services must be versioned")

    forbidden_public = [
        "openxr",
        "injector/",
        "manager",
        "std::vector",
        "std::string",
        "std::filesystem",
    ]
    lowered_sdk = sdk_text.lower()
    for token in forbidden_public:
        require(token.lower() not in lowered_sdk, f"public SDK leaks forbidden token: {token}")

    host_cpp = read(HOST / "plugin_host.cpp") + "\n" + read(HOST / "plugin_library.cpp")
    for token in [
        "missing_exported_symbol",
        "abi_version_mismatch",
        "wrong_game_or_build",
        "unsafe_plugin_path",
        "LoadLibraryExW",
        "LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR",
        "plugin_file_token",
        "callback_overrun_threshold",
        "adapter_exception",
        "hot_reload_unsafe_state",
        "fast_restart_preserves_diagnostics",
    ]:
        require(token in host_cpp, f"host missing reason code: {token}")

    require("duration > adapter.frame_callback_budget_us" in host_cpp, "host must enforce timing budget")
    require("active_callbacks" in host_cpp, "host must track active callbacks")
    require("catch (...)" in host_cpp, "host must guard adapter exceptions")
    require("std::ofstream" not in host_cpp, "host lifecycle path must not perform direct logging I/O")

    validator_cpp = read(VALIDATION / "adapter_validator.cpp")
    for token in [
        "load-compatible",
        "target-compatible",
        "runtime-safe",
        "missing_exported_symbol",
        "unsupported_api_version",
        "manifest_build_mismatch",
    ]:
        require(token in validator_cpp, f"validator missing coverage token: {token}")

    template_cpp = read(TEMPLATE / "template_adapter.cpp")
    for token in [
        "extern \"C\"",
        "vrclient_get_adapter_abi",
        "vrclient_get_adapter_metadata",
        "vrclient_create_adapter",
        "vrclient_destroy_adapter",
        "smoke-2026-06-11",
    ]:
        require(token in template_cpp, f"template missing {token}")

    hot_path_body = re.search(
        r"VrAdapterResult VRCLIENT_ADAPTER_CALL tick\((.*?)\n\}",
        template_cpp,
        re.S,
    )
    require(hot_path_body is not None, "template tick callback not found")
    blocked = re.compile(r"\b(std::ofstream|std::ifstream|printf|std::cout|std::cerr|Sleep|WaitForSingleObject|new|delete|malloc|free)\b")
    require(not blocked.search(hot_path_body.group(1)), "template tick callback contains hot-path-blocked operation")

    docs = read(DOCS)
    for phrase in [
        "No blocking file",
        "No synchronous logging",
        "No avoidable allocation",
        "Fast restart is the baseline",
        "same game/build identity namespace",
    ]:
        require(phrase in docs, f"docs missing required guidance: {phrase}")

    print("plugin static checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

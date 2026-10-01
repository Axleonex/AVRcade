from __future__ import annotations

import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
ADAPTER = ROOT / "adapters" / "cyberpunk_2077" / "native_adapter.cpp"
MANIFEST = ROOT / "adapters" / "cyberpunk_2077" / "adapter.json"
SERVICES = ROOT / "src" / "native" / "plugins" / "sdk" / "redengine_services.h"
CONTEXT = ROOT / "src" / "native" / "plugins" / "sdk" / "adapter_context.h"
CMAKE = ROOT / "CMakeLists.txt"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    adapter = ADAPTER.read_text(encoding="utf-8")
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    services = SERVICES.read_text(encoding="utf-8")
    context = CONTEXT.read_text(encoding="utf-8")
    cmake = CMAKE.read_text(encoding="utf-8")

    require(manifest["activation"] == "blocked_until_red4ext_bridge_services", "manifest must remain bridge-gated")
    require(manifest["supported_builds"][0]["build_id"] == "steam-1091500-build-20383525", "manifest build must be pinned")
    require("steam-1091500-build-20383525" in adapter, "adapter build must match manifest")
    require("VR_ADAPTER_ERROR_UNSUPPORTED_TARGET" in adapter, "adapter must refuse unknown builds")
    require("VR_ADAPTER_ERROR_UNSUPPORTED_API" in adapter, "adapter must refuse a missing REDengine bridge")
    for export in manifest["required_exports"]:
        require(export in adapter, f"adapter missing ABI export: {export}")
    for service in ["REDENGINE_GRAPHICS", "REDENGINE_VIEW", "REDENGINE_GAME_STATE"]:
        require(f"VRCLIENT_ADAPTER_SERVICE_{service}" in context, f"context missing {service} service ID")
        require(service in services, f"service header missing {service} contract")

    tick = re.search(
        r"VrAdapterResult VRCLIENT_ADAPTER_CALL tick\((.*?)\n\}",
        adapter,
        re.S,
    )
    require(tick is not None, "Cyberpunk adapter tick not found")
    blocked = re.compile(r"\b(std::ofstream|std::ifstream|printf|std::cout|std::cerr|Sleep|WaitForSingleObject|new|delete|malloc|free)\b")
    require(not blocked.search(tick.group(1)), "Cyberpunk adapter tick contains hot-path-blocked work")
    require("vrclient_cyberpunk_2077_adapter" in cmake, "CMake missing Cyberpunk adapter target")
    require("cyberpunk_native_preflight" in cmake, "CMake missing Cyberpunk preflight test")

    print("Cyberpunk adapter static checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

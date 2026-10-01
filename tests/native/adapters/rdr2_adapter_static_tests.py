from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
ADAPTER = ROOT / "adapters" / "rdr2" / "native_adapter.cpp"
BRIDGE = ROOT / "src" / "native" / "adapters" / "rdr2" / "rdr2_bridge.cpp"
MANIFEST = ROOT / "adapters" / "rdr2" / "adapter.json"
SERVICES = ROOT / "src" / "native" / "plugins" / "sdk" / "rage_services.h"
CONTEXT = ROOT / "src" / "native" / "plugins" / "sdk" / "adapter_context.h"
CMAKE = ROOT / "CMakeLists.txt"
CLI = ROOT / "client" / "VrClient.Cli" / "Program.cs"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    adapter = ADAPTER.read_text(encoding="utf-8")
    bridge = BRIDGE.read_text(encoding="utf-8")
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    services = SERVICES.read_text(encoding="utf-8")
    context = CONTEXT.read_text(encoding="utf-8")
    cmake = CMAKE.read_text(encoding="utf-8")
    cli = CLI.read_text(encoding="utf-8")

    require(manifest["adapter_id"] == "vrclient-rdr2-adapter", "adapter id mismatch")
    require(manifest["supported_builds"][0]["build_id"] == "steam-1174180-build-13773296", "RDR2 build must be pinned")
    require("steam-1174180-build-13773296" in adapter, "adapter build must match manifest")
    require("VR_ADAPTER_ERROR_UNSUPPORTED_TARGET" in adapter, "unknown builds must refuse")
    require("VR_ADAPTER_ERROR_UNSUPPORTED_API" in adapter, "missing bridge services must refuse")
    for service in ["RAGE_GRAPHICS", "RAGE_VIEW", "RAGE_GAME_STATE", "RAGE_INPUT"]:
        require(f"VRCLIENT_ADAPTER_SERVICE_{service}" in context, f"context missing {service}")
        require(service in services, f"RAGE service header missing {service}")
    tick = re.search(r"VrAdapterResult VRCLIENT_ADAPTER_CALL tick\((.*?)\n\}", adapter, re.S)
    require(tick is not None, "RDR2 adapter tick not found")
    blocked = re.compile(r"\b(std::ofstream|std::ifstream|printf|std::cout|std::cerr|Sleep|WaitForSingleObject|new|delete|malloc|free)\b")
    require(not blocked.search(tick.group(1)), "RDR2 adapter tick contains hot-path-blocked work")
    require("vrclient_rdr2_adapter" in cmake, "CMake missing RDR2 adapter target")
    require("VRCLIENT_BUILD_RDR2_BRIDGE" in cmake, "CMake missing optional RDR2 bridge target")
    require("ScriptHookRDR2.dll" in bridge, "bridge must bind ScriptHookRDR2")
    require("CommandLineToArgvW" in bridge and "-vrclient-rdr2-story" in bridge,
            "bridge must validate the Steam-forwarded Story Mode argument")
    require("Rdr2LaunchPolicy.SteamArguments" in cli,
            "RDR2 CLI must use the shared Steam-forwarded launch policy")
    require("story_mode_marker_required" in bridge,
            "bridge must fail closed without the Story Mode marker")
    require("vr_runtime_run_frame" in bridge and "D3D12SceneRelayRenderer" in bridge,
            "bridge must submit frames through the shared OpenXR relay")
    require("camera_ready" in bridge and "stereo_submit" in bridge,
            "bridge must emit live camera/stereo evidence markers")
    require("GetCurrentProcessId" in bridge and "rdr2-bridge-" in bridge,
            "bridge evidence log must be scoped to the current RDR2 process")
    require("resetLogFile" in bridge and "L\"wb\"" in bridge,
            "bridge must truncate a reused PID log before recording this process")
    require("VR_RUNTIME_SKIPPED" in bridge,
            "bridge must tolerate loading/menu adapter recoverable frames")
    require("GetAsyncKeyState" in bridge, "bridge must expose a keyboard fallback for minimum controls")
    require("Online" not in bridge and "online" not in bridge,
            "RDR2 bridge must not contain an Online launch path")
    print("RDR2 adapter static checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
ADAPTER = ROOT / "adapters" / "repo" / "repo_adapter.cpp"
MANIFEST = ROOT / "adapters" / "repo" / "adapter.json"
CMAKE = ROOT / "CMakeLists.txt"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    source = ADAPTER.read_text(encoding="utf-8")
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    cmake = CMAKE.read_text(encoding="utf-8")

    require(manifest["adapter_id"] == "vrclient-repo-adapter", "adapter id mismatch")
    require(manifest["supported_games"] == ["repo"], "manifest should support only repo")
    require(
        manifest["supported_builds"] == [
            {"game_id": "repo", "build_id": "steam-3241660-build-23363152"}
        ],
        "manifest should support only the pinned R.E.P.O. build",
    )

    for token in [
        "vrclient-repo-adapter",
        "steam-3241660-build-23363152",
        "VRCLIENT_ADAPTER_SERVICE_INPUT",
        "VRCLIENT_ADAPTER_SERVICE_COMFORT",
        "VRCLIENT_ADAPTER_SERVICE_HUD",
        "repo_adapter_hook_surface_unvalidated",
    ]:
        require(token in source, f"adapter source missing {token}")

    for forbidden in [
        "vrclient-smoke-host",
        "MenuActionRandomMatchmaking",
        "PhotonNetwork",
        "JoinRandomRoom",
        "CreateRoom",
        "online_coop_enabled",
    ]:
        require(forbidden not in source, f"adapter source must not enable {forbidden}")

    require("vrclient_repo_adapter" in cmake, "CMake missing R.E.P.O. adapter target")
    require("vr_repo_adapter_tests" in cmake, "CMake missing R.E.P.O. adapter tests")
    print("R.E.P.O. adapter static checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SOURCE = (
    ROOT
    / "external"
    / "cyberpunk-vr-port-reference-f5e59d7e81a3-complete"
    / "cyberpunk-vr-port-f5e59d7e81a35ccf71da1e75b34483335e908174"
)
PATCH = ROOT / "patches" / "cyberpunk-vr-port" / "f5e59d7-activation-gate.patch"
MANIFEST = ROOT / "config" / "redengine" / "native" / "cyberpunk-2077-installed-files.json"
PROFILE = ROOT / "config" / "redengine" / "native" / "cyberpunk-2077.json"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    plugin = (SOURCE / "src/red4ext_stereo/plugin_main.cpp").read_text(encoding="utf-8")
    stereo_init = (SOURCE / "mods/cet/CyberpunkVRPort_Stereo/init.lua").read_text(encoding="utf-8")
    selector = (SOURCE / "mods/cet/CyberpunkVRPort_Stereo/modules/vrcam_select.lua").read_text(encoding="utf-8")
    vrik = (SOURCE / "mods/cet/CyberpunkVRPort_VRIK/init.lua").read_text(encoding="utf-8")
    no_anims = (SOURCE / "mods/redscript/CyberpunkVRPort_NoAnims/vrport_no_anims.reds").read_text(encoding="utf-8")
    patch = PATCH.read_text(encoding="utf-8")
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    profile = json.loads(PROFILE.read_text(encoding="utf-8"))

    require('GetEnvironmentVariableA("VRCLIENT_VR_ACTIVE"' in plugin, "native gate must read the child environment")
    require('strcmp(value, "1") == 0' in plugin, "only the exact value 1 may activate VR")
    require(
        'CGlobalFunction::Create("CyberpunkVRPort.NoAnims.IsVRClientVrActive"' in plugin,
        "native registration must match the redscript module-qualified name",
    )
    require("WriteVrActivationBridgeState(vrActive)" in plugin, "native load must overwrite CET activation state every process")
    require("CyberpunkVRPort_VRIK" in plugin, "native gate must publish activation state for the VRIK camera/body pump")
    require("if (!vrActive)" in plugin, "passive load path must be explicit")
    load_path = plugin[plugin.index("const bool vrActive"):]
    passive_block = load_path[load_path.index("if (!vrActive)"):load_path.index("CyberpunkVRPort_InitStereo();")]
    require("return true" in passive_block, "passive mode must return before stereo initialization")

    require('io.open("bridge/vrclient_vr_active.txt", "r")' in selector, "CET must consume native activation state")
    require("VrcamSelect.want = VrcamSelect.vrActive" in selector, "flat CET mode must request VRCAM off")
    require('io.open("bridge/vrcam_active.txt", "w")' in selector, "flat mode must publish an empty active camera")
    require("type(registerHotkey) == 'function'" in stereo_init, "optional CET hotkey API must not abort VRCAM initialization")

    require("local function readVrActivationState()" in vrik, "VRIK pump must consume per-process activation state")
    require("isReady = readVrActivationState()" in vrik, "VRIK camera/body pump must remain inactive for flat launches")
    require('io.open("vrclient_vr_active.txt", "r")' in vrik, "VRIK must read its native-written local gate")
    require(vrik.count("if type(registerHotkey) == 'function' then") >= 2, "optional CET hotkey API must not abort the VRIK transform pump")

    require("native func IsVRClientVrActive() -> Bool" in no_anims, "NoAnims must use the native per-process gate")
    require(no_anims.count("if !IsVRClientVrActive()") >= 10, "every behavior-changing NoAnims wrapper must preserve vanilla flat behavior")
    require("VR-only installs" not in no_anims, "the obsolete unconditional-install assumption must be removed")

    stereo = next(package for package in manifest["packages"] if package["name"].startswith("CyberpunkVR Port Stereo"))
    comfort = next(package for package in manifest["packages"] if package["name"].startswith("CyberpunkVR Port NoAnims"))
    require(".activation2" in stereo["version"], "stereo package version must identify the camera/body activation gate")
    require(".activation1" in comfort["version"], "NoAnims package version must identify the activation gate")
    require("VRCLIENT_VR_ACTIVE" in patch, "the pinned-source patch must preserve the activation contract")
    require(profile["loading"]["activation_mode"] == "per_process_opt_in", "profile must declare flat-by-default loading")
    require(profile["loading"]["activation_environment"] == "VRCLIENT_VR_ACTIVE=1", "profile must pin the exact opt-in value")
    require(
        any(dep["path"].endswith("CyberpunkVRPort_VRIK\\init.lua") for dep in profile["dependencies"]),
        "preflight must fail closed when the VRIK camera/body transform pump is missing",
    )

    print("Cyberpunk per-process activation gate checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

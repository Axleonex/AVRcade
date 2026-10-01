"""Static contract checks for the classic GTA SA x86 bridge."""

from pathlib import Path
import hashlib
import json


ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "adapters" / "gta_sa" / "vrclient_gtasa_theater.cpp"
CMAKE = ROOT / "adapters" / "gta_sa" / "CMakeLists.txt"
DOC = ROOT / "docs" / "adapters" / "gta-san-andreas.md"
CLI = ROOT / "client" / "VrClient.Cli" / "Program.cs"
PROFILE = ROOT / "config" / "legacy" / "gta-san-andreas.json"
NATIVE_CONFIG = ROOT / "config" / "legacy" / "gta-san-andreas-native.json"
PROFILE_VALIDATOR = ROOT / "tests" / "native" / "adapters" / "validate_gtasa_native_profile.py"
BUILD_SCRIPTS = [
    ROOT / "tools" / "build-gtasa-bridge.ps1",
    ROOT / "tools" / "build-gtasa-openxr-loader.ps1",
    ROOT / "tools" / "build-fake-openxr-runtime.ps1",
]


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    cmake = CMAKE.read_text(encoding="utf-8")
    doc = DOC.read_text(encoding="utf-8")
    cli = CLI.read_text(encoding="utf-8")
    profile = json.loads(PROFILE.read_text(encoding="utf-8"))
    native_config = json.loads(NATIVE_CONFIG.read_text(encoding="utf-8"))
    native_config_sha256 = hashlib.sha256(NATIVE_CONFIG.read_bytes()).hexdigest()

    required_source = [
        "IMAGE_FILE_MACHINE_I386",
        "HookD3D9Import",
        "IDirect3DDevice9",
        "xrCreateInstance",
        "XR_KHR_D3D11_ENABLE_EXTENSION_NAME",
        "XR_TYPE_COMPOSITION_LAYER_QUAD",
        "D3D11CreateDevice",
        "D3DFMT_A2R10G10B10",
        "A2R10G10B10",
        "gta_sa.exe",
        "g_bridgeEnabled",
        "VRCLIENT_GTASA_STEREO",
        "VRCLIENT_GTASA_INPUT",
        "xrLocateViews",
        "xrCreateActionSet",
        "xrSyncActions",
        "SendInput",
        "leftMoveAction_",
        "rightLookAction_",
        "BeginStereoFrame",
        "HookedRenderTail",
        "CallOriginalRenderTail",
        "RenderCompleteFrame",
        "CaptureRenderedEye",
        "ApplyRwProjection",
        "gtasa_vr::EyePose",
        "XR_VIEW_STATE_POSITION_VALID_BIT",
        "ReleasePressedInputs",
        "frameState_.shouldRender",
        "EnsureInitialized",
        "EndStereoFrame",
        "RenderProjection",
        "RemoveInlineHook",
        "GtaSaNativeConfig",
        "LoadGtaSaNativeConfig",
        "VRCLIENT_GTASA_HOOK_CONFIG",
        "ParseHexBytes",
        "g_config",
        "XR_TYPE_COMPOSITION_LAYER_PROJECTION",
        "imageArrayIndex",
        "IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT",
        "DelayImportDescriptor",
        "g_createDeviceImplementationTrampoline",
        "OpenXR initialization started",
        "ReleaseOpenXrResources",
        "CaptureFirstPersonBasis",
        "constexpr unsigned int kPlayerHeadBone = 5;",
        "gtasa_vr::HeadAnchoredBasis",
        "VRCLIENT_GTASA_FIRST_PERSON",
        "findPlayerPedExpected",
        "getBonePositionExpected",
        "findPlayerVehicleExpected",
        "pedPreRenderExpected",
        "leftHandPoseAction_",
        "rightHandPoseAction_",
        "xrCreateActionSpace",
        "xrLocateSpace",
        "XR_SPACE_LOCATION_POSITION_VALID_BIT",
        "SolveTwoBone",
        "HookedPedPreRender",
        "RestoreArmOverride",
        "InstallVrHudHooks",
        "HookedFontPrintString",
        "HookedDrawPrimitiveUp",
        "VrHudPoint",
        "cutsceneRunning",
        "cutsceneProcessing",
        "viewWindowX",
        "viewOffsetX",
        "RestoreGameState(g_d3d9Device)",
        "retrying in",
        "XR_ERROR_RUNTIME_UNAVAILABLE",
        "OpenXR x86 loader override=",
        "OpenXR runtime manifest override=",
        "XR_MAKE_VERSION(1, 0, 0)",
        "XR_ERROR_API_VERSION_UNSUPPORTED",
        "XR_ERROR_FORM_FACTOR_UNAVAILABLE",
        "fovAddress",
        "aspectRatioAddress",
        "baseFov_",
        "baseAspectRatio_",
        "CalculateEyeFov",
        "CalculateEyeAspectRatio",
        "RestoreGameState",
        "resolveSurface_",
        "CreateRenderTarget",
        "D3DMULTISAMPLE_NONE",
        "StretchRect",
        "sourceForReadback",
        "XR_SESSION_STATE_STOPPING",
        "XR_SESSION_STATE_LOSS_PENDING",
        "HandleSessionLoss",
        "XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING",
        "XR_EVENT_UNAVAILABLE",
        "XR_ERROR_INSTANCE_LOST",
        "HandleInstanceLoss",
        "skipSessionTermination",
    ]
    for marker in required_source:
        assert marker in source, marker

    assert "CMAKE_SIZEOF_VOID_P EQUAL 8" in cmake
    assert "SUFFIX \".asi\"" in cmake
    assert "MSVC_RUNTIME_LIBRARY" in cmake
    assert "openxr_loader.dll" in source
    assert "gta_sa.exe" in doc
    assert "stereo scene path" in doc.lower()
    assert "monoscopic theater" in doc.lower()
    assert "game directory" in doc.lower()
    assert "--update-bridge" in cli
    assert "--from-bridge-sha256" in cli
    assert "--from-native-config-sha256" in cli
    assert "GtaSanAndreasNativeConfigFileName" in cli
    assert "nativeConfigSource" in cli
    assert "VRCLIENT_GTASA_DISABLE" in source
    assert native_config_sha256 in source
    assert "D3D9 Present entered" in source
    assert "BuildGtaMenuPadState" in source
    assert "frontend_pad_update_call" in source
    assert "frontendPadUpdateCall" in source
    assert PROFILE_VALIDATOR.exists()
    assert profile["verification"]["status"] in {
        "game_identity_observed_bridge_missing",
        "game_identity_observed_bridge_pinned_headset_pending",
    }
    for script in BUILD_SCRIPTS:
        script_text = script.read_text(encoding="utf-8")
        assert "Get-Item -LiteralPath" in script_text, script
        assert "Resolve-Path" not in script_text, script
        assert "pushd" in script_text, script

    # The bridge must remain an additive ASI. It may patch the running process,
    # but it must not contain installation or executable-rewrite operations.
    forbidden = ("WriteProcessMemory", "CopyFile", "MoveFile", "DeleteFile", "CreateFileW")
    for marker in forbidden:
        assert marker not in source, marker

    # Patch-sensitive data must be external and match the observed executable;
    # the native profile is the source of truth consumed by the ASI at runtime.
    assert native_config["schema"] == "gta-san-andreas-native/1"
    assert native_config["game_sha256"] == "a559aa772fd136379155efa71f00c47aad34bbfeae6196b0fe1047d0645cbd26"
    assert native_config["image_size"] == 14383616
    for key in (
        "image_base", "camera_address", "camera_matrix_offset",
        "fov_address", "aspect_ratio_address", "copy_camera_matrix_to_rw_cam",
        "rw_camera_offset", "set_rw_view_window", "set_rw_view_offset",
        "screen_dimensions", "derive_camera", "front_end_menu_active", "fade_status",
        "cutscene_running", "cutscene_processing",
        "find_player_ped", "set_player_heading", "find_player_vehicle", "get_bone_position",
        "ped_pre_render", "get_anim_hierarchy_from_clump",
        "rp_hanim_id_get_index", "rp_hanim_get_matrix_array",
        "hud_player_info", "hud_wanted", "hud_radar", "hud_vital_stats",
        "font_print_string",
        "render_tail", "render_tail_return", "camera_size", "d3d9_delay_thunk",
    ):
        assert native_config[key].startswith("0x"), key
    for key in (
        "d3d9_delay_thunk_expected", "create_device_expected",
        "find_player_ped_expected", "set_player_heading_expected", "find_player_vehicle_expected", "get_bone_position_expected",
        "ped_pre_render_expected", "get_anim_hierarchy_from_clump_expected",
        "rp_hanim_id_get_index_expected", "rp_hanim_get_matrix_array_expected",
        "hud_player_info_expected", "hud_wanted_expected", "hud_radar_expected",
        "hud_vital_stats_expected", "font_print_string_expected",
        "render_tail_expected", "render_tail_return_expected", "camera_size_expected",
    ):
        assert len(native_config[key].split()) > 0, key

    # Inline-hook signatures must end on an x86 instruction boundary.  The
    # CPed::PreRender entry starts with a seven-byte CMP instruction; omitting
    # its immediate byte makes the trampoline resume in the middle of that
    # instruction as soon as gameplay begins.
    assert native_config["ped_pre_render_expected"] == "83 B9 30 05 00 00 32"

    for marker in (
        "constexpr uintptr_t kTheCameraAddress",
        "static constexpr uint8_t kRenderSceneBytes",
        "static constexpr uintptr_t kDirect3DCreate9DelayThunk",
        "XR_CURRENT_API_VERSION",
        "NativeAddress(0x7EE410)",
        "NativeAddress(0x7EE1A0)",
        "NativeAddress(0xC17044)",
        "NativeAddress(0x5150E0)",
        "NativeAddress(0xBA67A4)",
        "NativeAddress(0x50AE20)",
        "InstallInlineHook(0x72FC70",
        "InstallInlineHook(0x53E9AC",
    ):
        assert marker not in source, marker

    # DllMain must stay loader-lock safe. Config I/O, hashing, and process patching
    # belong on the worker thread, not inside the DLL attach callback.
    dllmain_start = source.index("BOOL WINAPI DllMain")
    dllmain = source[dllmain_start:]
    for marker in ("LoadGtaSaNativeConfig()", "IsExpectedGameImage()", "HookD3D9Import()"):
        assert marker not in dllmain, marker

    # Guard the real use-after-unload regression, not merely the presence of API names.
    shader_start = source.index("const HRESULT vertexResult")
    shader_end = source.index("D3D11_SAMPLER_DESC", shader_start)
    shader = source[shader_start:shader_end]
    assert shader.index("CreateVertexShader") < shader.index("FreeLibrary(compiler)")
    assert shader.rindex("pixelBytecode->Release()") < shader.rindex("FreeLibrary(compiler)")
    assert "gtasa_bridge_integration_tests" in cmake
    assert "gtasa_vr_behavior_tests" in cmake
    print("gtasa_bridge_static_tests: passed")


if __name__ == "__main__":
    main()

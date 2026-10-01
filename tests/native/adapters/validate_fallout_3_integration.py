#!/usr/bin/env python3
"""CI-safe validation for the free Fallout 3 VR integration."""

from __future__ import annotations

import json
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
OWNED = [
    ROOT / "adapters/fallout_3",
    ROOT / "src/native/gamebryo_dx9_openxr",
    ROOT / "client/VrClient.Core/Fallout3",
    ROOT / "client/VrClient.Fallout3.Cli",
    ROOT / "config/modpacks/fallout-3.dependencies.json",
    ROOT / "config/modpacks/fallout-3.compatibility-rules.json",
    ROOT / "docs/fallout-3",
]


class Fallout3IntegrationValidation(unittest.TestCase):
    def test_backend_labels_are_truthful_and_native_is_not_preverified(self) -> None:
        adapter = json.loads((ROOT / "adapters/fallout_3/adapter.json").read_text(encoding="utf-8"))
        backends = {item["id"]: item for item in adapter["backends"]}
        self.assertEqual("Depth VR - Free", backends["depth-vr"]["label"])
        self.assertEqual("depth-derived", backends["depth-vr"]["stereo_kind"])
        self.assertEqual("Native VR - Experimental", backends["native-vr"]["label"])
        self.assertEqual([], adapter["native"]["verified_build_profiles"])
        self.assertIn("executable_sha256", adapter["native"]["verified_label_requires"])
        self.assertEqual("depth-vr", backends["native-vr"]["fallback"])

    def test_dependency_attribution_is_complete_and_nonredistributing(self) -> None:
        manifest = json.loads((ROOT / "config/modpacks/fallout-3.dependencies.json").read_text(encoding="utf-8"))
        self.assertTrue(manifest["policy"]["free_vr_only"])
        self.assertFalse(manifest["policy"]["automatic_downloads"])
        required = {"name", "author_project", "version", "license", "source_url", "download_url",
                    "sha256", "redistribution", "acquisition", "notes"}
        ids = set()
        for dependency in manifest["dependencies"]:
            ids.add(dependency["id"])
            self.assertTrue(required.issubset(dependency))
            self.assertTrue(dependency["source_url"].startswith("https://"))
            self.assertTrue(dependency["download_url"].startswith("https://"))
        self.assertTrue({"reshade", "superdepth3d", "osiris-vr-viewer", "openxr-sdk", "minhook", "virtualdesktopxr",
                         "steamvr", "fose", "fallout-anniversary-patcher", "mod-organizer-2"}.issubset(ids))
        depth = next(item for item in manifest["dependencies"] if item["id"] == "superdepth3d")
        self.assertIn("prohibited", depth["redistribution"])
        self.assertIn("user", depth["acquisition"])

    def test_no_restricted_binary_is_owned(self) -> None:
        forbidden = {".exe", ".dll", ".esm", ".esp", ".bsa", ".ba2", ".7z", ".zip", ".rar"}
        offenders: list[str] = []
        for item in OWNED:
            paths = item.rglob("*") if item.is_dir() else [item]
            offenders.extend(
                str(path.relative_to(ROOT))
                for path in paths
                if path.is_file()
                and not {"bin", "obj"}.intersection(path.relative_to(ROOT).parts)
                and path.suffix.lower() in forbidden
            )
        self.assertEqual([], offenders)

    def test_paid_converter_name_is_absent_from_owned_output(self) -> None:
        forbidden = "vor" + "px"
        offenders = []
        for item in OWNED:
            paths = item.rglob("*") if item.is_dir() else [item]
            for path in paths:
                if path.is_file() and path.suffix.lower() in {".cs", ".cpp", ".h", ".json", ".md", ".txt"}:
                    if forbidden in path.read_text(encoding="utf-8").casefold():
                        offenders.append(str(path.relative_to(ROOT)))
        self.assertEqual([], offenders)

    def test_shared_renderer_contains_no_title_specific_behavior(self) -> None:
        text = "\n".join(path.read_text(encoding="utf-8") for path in
                         (ROOT / "src/native/gamebryo_dx9_openxr").glob("*.*")).casefold()
        self.assertNotIn("fallout", text)
        self.assertNotIn("new vegas", text)
        self.assertIn("independent", text)
        self.assertIn("devicelost", text.replace(" ", ""))

    def test_native_title_hook_is_implemented_but_profile_gated(self) -> None:
        adapter = (ROOT / "adapters/fallout_3/native_adapter.cpp").read_text(encoding="utf-8")
        camera = (ROOT / "adapters/fallout_3/camera_stereo.h").read_text(encoding="utf-8")
        for token in ("FOSEPlugin_Query", "FOSEPlugin_Load", "MH_CreateHook", "HookRender",
                      "expectedExecutableSha256", "expectedRenderEntry", "CurrentExecutableSha256",
                      "SameRenderedCamera", "submitEye", "desktop rendering restored",
                      "RenderContextPointerRva", "renderContext != mainRenderContext",
                      "using RenderFunction = void (__thiscall*)",
                      "void __fastcall HookRender"):
            self.assertIn(token, adapter + camera)
        self.assertIn("for (std::uint32_t eye = 0; eye < 2; ++eye)", adapter)
        self.assertNotIn("wchar_t path[32768]", adapter)
        dll_main = adapter.split("BOOL WINAPI DllMain", 1)[1].split(
            "// Minimal FOSE ABI surface", 1
        )[0]
        self.assertNotIn("GetModuleFileNameW", dll_main)
        self.assertNotIn("std::filesystem", dll_main)
        self.assertIn("DisableThreadLibraryCalls", dll_main)
        self.assertIn('GetEnvironmentVariableW(L"VRCLIENT_FALLOUT3_NATIVE_VR"', adapter)
        self.assertIn("FOSE loaded in desktop mode; native VR hooks are inactive", adapter)
        example = (ROOT / "adapters/fallout_3/fallout3-native-profile.example.ini").read_text(encoding="utf-8")
        self.assertIn("deliberately non-activating", example)
        self.assertIn("RenderHookRva=0x0", example)

        reviewed = (ROOT / "adapters/fallout_3/fallout3-native-profile-steam-1.7.0.3.ini").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "ExecutableSha256=03CF5ADA02FCF789FCF4639E05127D545FF3F69363B926709B8F0C3F8AD29107",
            reviewed,
        )
        self.assertIn("RenderHookRva=0x002ECBA0", reviewed)
        self.assertIn("RenderContextPointerRva=0x00C7A3C0", reviewed)
        self.assertIn("SceneGraphPointerRva=0x00C7A224", reviewed)
        self.assertIn("RendererPointerRva=0x00C8F048", reviewed)
        self.assertIn("CameraOffset=0x000000AC", reviewed)
        self.assertIn("DeviceOffset=0x00000288", reviewed)
        self.assertIn(
            "RenderEntryBytes=51 A1 24 A2 07 01 8B 80 AC 00 00 00 8B 90 8C 00",
            reviewed,
        )

    def test_cli_and_human_protocol_cover_full_lifecycle(self) -> None:
        cli = (ROOT / "client/VrClient.Fallout3.Cli/Program.cs").read_text(encoding="utf-8").casefold()
        protocol = (ROOT / "docs/fallout-3/HUMAN-VERIFICATION.md").read_text(encoding="utf-8").casefold()
        for verb in ("discover", "check", "plan", "apply", "repair", "restore", "uninstall", "compatibility", "launch"):
            self.assertIn(verb, cli)
        for phrase in ("steamvr", "virtual desktop", "eye order", "pip-boy", "lockpicking", "vats",
                       "save/load", "independent per-eye", "device loss", "fallback"):
            self.assertIn(phrase, protocol)


if __name__ == "__main__":
    unittest.main()

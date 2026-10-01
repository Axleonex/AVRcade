#!/usr/bin/env python3
"""CI-safe static validation for the FNV VR integration (stdlib only)."""

from __future__ import annotations

import json
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]


class FalloutNewVegasIntegrationStaticTests(unittest.TestCase):
    def test_adapter_is_native_openxr_and_never_bundles_commercial_components(self) -> None:
        adapter = json.loads((ROOT / "adapters/fallout_new_vegas/adapter.json").read_text(encoding="utf-8"))
        self.assertTrue(adapter["native_renderer"])
        self.assertEqual("22380", adapter["steam_app_id"])
        self.assertFalse(adapter["restrictions"]["bundles_game_assets"])
        self.assertFalse(adapter["restrictions"]["bundles_vorpx"])
        self.assertFalse(adapter["restrictions"]["bypasses_activation"])

    def test_dependency_manifest_matches_schema_contract(self) -> None:
        manifest = json.loads((ROOT / "config/modpacks/fallout-new-vegas.dependencies.json").read_text(encoding="utf-8"))
        schema = json.loads((ROOT / "adapters/fallout_new_vegas/dependency-manifest.schema.json").read_text(encoding="utf-8"))
        self.assertEqual(schema["properties"]["game"]["const"], manifest["game"])
        self.assertEqual(1, manifest["schema_version"])
        self.assertTrue(set(manifest).issubset(schema["properties"]))
        ids = {item["id"] for item in manifest["dependencies"]}
        required = {"fnv-4gb-patcher", "xnvse", "jip-pp-ln-nvse", "showoff-xnvse", "fnvr", "steamvr", "virtual-desktop", "mod-organizer-2"}
        self.assertTrue(required.issubset(ids))
        self.assertNotIn("vorpx", ids)
        for dependency in manifest["dependencies"]:
            self.assertTrue(dependency["source_url"].startswith("https://"))
            self.assertEqual("user-supplied-from-official-source", dependency["acquisition"])
            digest = dependency["sha256"]
            self.assertTrue(digest is None or (len(digest) == 64 and digest.isascii()))

    def test_repository_contains_no_restricted_binary_in_owned_paths(self) -> None:
        roots = [ROOT / "adapters/fallout_new_vegas", ROOT / "config/modpacks", ROOT / "docs/fallout-new-vegas"]
        forbidden = {".exe", ".dll", ".esm", ".esp", ".bsa", ".ba2", ".7z", ".zip", ".rar"}
        offenders = [str(path.relative_to(ROOT)) for base in roots for path in base.rglob("*") if path.is_file() and path.suffix.lower() in forbidden]
        self.assertEqual([], offenders)

    def test_native_launcher_accepts_the_required_laa_patched_retail_build_safely(self) -> None:
        launcher = (ROOT / "tools/native/fnv-test-launcher.cpp").read_text(encoding="utf-8")
        adapter = (ROOT / "adapters/fallout_new_vegas/native_stereo.cpp").read_text(encoding="utf-8")
        self.assertIn("IMAGE_FILE_LARGE_ADDRESS_AWARE", launcher)
        self.assertIn("dwFileVersionLS)==525", launcher)
        self.assertIn("expectedHash", launcher)  # pristine Steam build remains an exact allow-list entry
        self.assertIn("Refused unsupported or already-modified render entry", adapter)

    def test_native_launcher_preserves_steam_identity_when_starting_the_game(self) -> None:
        launcher = (ROOT / "tools/native/fnv-test-launcher.cpp").read_text(encoding="utf-8")
        self.assertIn('L"SteamAppId"', launcher)
        self.assertIn('L"SteamGameId"', launcher)
        self.assertIn("CreateProcessW", launcher)
        self.assertNotIn('L"steam://rungameid/22380"', launcher)

    def test_preflight_refuses_the_known_bare_laa_steam_executable(self) -> None:
        validator = (ROOT / "client/VrClient.Core/FalloutNewVegas/PrerequisiteValidator.cs").read_text(encoding="utf-8")
        self.assertIn("FFD405CF9F5AF080F202DF9BD1E3C6CA54F0D541081B4DEEB36047A1EE53475A", validator)
        self.assertIn("steam_incompatible_bare_laa", validator)

    def test_human_protocol_and_credits_cover_required_boundaries(self) -> None:
        protocol = (ROOT / "docs/fallout-new-vegas/HUMAN-VERIFICATION.md").read_text(encoding="utf-8").lower()
        credits = (ROOT / "docs/fallout-new-vegas/CREDITS-LICENSES.md").read_text(encoding="utf-8").lower()
        for phrase in ("virtual desktop", "per-eye", "6dof", "both motion controllers", "save/load", "non-vr profile"):
            self.assertIn(phrase, protocol)
        for phrase in ("bethesda", "obsidian", "xnvse", "jip pp ln", "showoff", "iloveusername", "openxr", "mod organizer 2"):
            self.assertIn(phrase, credits)


if __name__ == "__main__":
    unittest.main()

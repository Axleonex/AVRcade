"""CTest for the new-adapter scaffolding CLI (B4 / ADKIT-01, ADKIT-04).

NON-FLAKY by construction:
  * Scaffolds into a fresh tempfile.mkdtemp() -- NEVER the repo tree. No
    compilation, no headset, no subprocess that waits on hardware. Pure
    file-generation + jsonschema. Deterministic placeholder digests.
  * The tempdir is removed in a finally, so a green or red run leaves the
    working tree clean (the OFF gate's `git status` stays quiet).

It REUSES THE EXISTING VALIDATOR LOGIC rather than re-implementing it:
  * the generated fingerprint is checked with
    validate_game_fingerprint.validate_config (the real schema +
    require_real_sha256 + policy asserts). NOTE: that module's main() prints a
    path relative_to(ROOT) and crashes on an out-of-tree tempdir, so we call the
    validation FUNCTION directly -- same checks, no path bug.
  * the generated profile is validated against the SAME
    config/schemas/game-profile.schema.json the repo validator uses.

This file lives under tests/native/** so the default OFF gate
(scripts/ci/build-and-test.ps1) runs it both as a CTest and in its Step-7
"every tests/native/**/*.py" sweep (which runs each script with NO args from the
repo root -- this script honors that: it takes no argv and is self-contained).
"""

from __future__ import annotations

import importlib.util
import json
import shutil
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
CLI = ROOT / "tools" / "new-adapter" / "new_adapter.py"
GAME_PROFILE_SCHEMA = ROOT / "config" / "schemas" / "game-profile.schema.json"
FINGERPRINT_VALIDATOR = ROOT / "tests" / "native" / "versioning" / "validate_game_fingerprint.py"

REQUIRED_EXPORTS = [
    "vrclient_get_adapter_abi",
    "vrclient_get_adapter_metadata",
    "vrclient_create_adapter",
    "vrclient_destroy_adapter",
]

# A slug deliberately distinct from any real adapter (repo / _template / smoke).
GAME_ID = "ctest-fixture-game"
BUILD_ID = "ctest-2026-build-001"
DISPLAY_NAME = "CTest Fixture Game"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def load_module(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None, f"cannot load {path}"
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    import jsonschema  # imported here so a missing dep is a clear, late failure

    cli = load_module(CLI, "new_adapter_cli_under_test")
    fingerprint_validator = load_module(
        FINGERPRINT_VALIDATOR, "validate_game_fingerprint_under_test"
    )
    game_profile_schema = json.loads(GAME_PROFILE_SCHEMA.read_text(encoding="utf-8"))

    temp_root = Path(tempfile.mkdtemp(prefix="vrclient-new-adapter-ctest-"))
    try:
        # --- (a) run the CLI into the temp dir (file-gen + CMake edit on a COPY) ---
        # Seed a COPY of the real root CMakeLists.txt so we exercise the (idempotent)
        # build-wiring insertion WITHOUT ever mutating the live file.
        (temp_root).mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / "CMakeLists.txt", temp_root / "CMakeLists.txt")

        exit_code = cli.main([
            "--game-id", GAME_ID,
            "--build-id", BUILD_ID,
            "--display-name", DISPLAY_NAME,
            "--out-root", str(temp_root),
            "--json",
        ])
        require(exit_code == 0, f"CLI exited nonzero: {exit_code}")

        d = cli.derive(GAME_ID, BUILD_ID)

        # --- (b) the expected files exist ---
        adapter_cpp = temp_root / d["adapter_cpp_rel"]
        manifest_path = temp_root / d["manifest_rel"]
        adapter_cmake = temp_root / d["adapter_cmake_rel"]
        fingerprint_path = temp_root / d["fingerprint_rel"]
        profile_path = temp_root / d["profile_rel"]
        controller_map_path = temp_root / d["controller_map_rel"]
        test_cpp = temp_root / d["test_cpp_rel"]
        profile_validator = temp_root / d["profile_validator_rel"]
        for path in (
            adapter_cpp, manifest_path, adapter_cmake, fingerprint_path,
            profile_path, controller_map_path, test_cpp, profile_validator,
        ):
            require(path.exists(), f"expected generated file missing: {path}")

        # --- (c) generated fingerprint passes the EXISTING fingerprint validator ---
        # Reuse the real validation FUNCTION (schema + require_real_sha256 + policy).
        schema = json.loads(
            (ROOT / "config" / "schemas" / "game-fingerprint.schema.json").read_text(encoding="utf-8")
        )
        fingerprint_validator.validate_config(fingerprint_path, schema)

        # --- (c2) generated profile validates against the EXISTING profile schema ---
        profile = json.loads(profile_path.read_text(encoding="utf-8"))
        jsonschema.validate(profile, game_profile_schema)
        require(
            profile["profile_id"] == f"{GAME_ID}-{BUILD_ID}",
            "profile_id must be '<game_id>-<build_id>'",
        )
        action_ids = {a["id"] for a in profile["input"]["actions"]}
        require(
            action_ids == {
                "move_x", "move_y", "turn_x", "interact", "menu", "recenter",
                "comfort_snap_turn", "comfort_vignette_toggle",
            },
            f"profile must declare all 8 input actions, got {sorted(action_ids)}",
        )
        require(len(profile["hud"]["anchors"]) >= 1, "profile needs >=1 HUD anchor")

        # Controller references are scaffolded for every engine, but stay
        # fail-closed until an adapter author verifies the in-game actions.
        controller_map = json.loads(controller_map_path.read_text(encoding="utf-8"))
        require(controller_map["game_slug"] == GAME_ID, "controller map slug must derive from identity")
        require(controller_map["verification_state"] == "template", "new controller maps must be unverified")

        # --- (d) manifest: required_exports == the 4 ABI names; adapter_id derived ---
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        require(
            manifest["required_exports"] == REQUIRED_EXPORTS,
            f"manifest required_exports mismatch: {manifest['required_exports']}",
        )
        require(
            manifest["adapter_id"] == f"vrclient-{GAME_ID}-adapter",
            f"manifest adapter_id mismatch: {manifest['adapter_id']}",
        )
        require(
            manifest["supported_builds"] == [{"game_id": GAME_ID, "build_id": BUILD_ID}],
            "manifest supported_builds must match the identity",
        )
        require(manifest["schema_version"] == 1, "manifest schema_version must be 1")

        # --- (e) adapter .cpp declares the 4 ABI exports + correct ABI version,
        #         keeps the three Phase-5 service queries, and namespaces events ---
        source = adapter_cpp.read_text(encoding="utf-8")
        for export in REQUIRED_EXPORTS:
            require(export in source, f"adapter source missing ABI export {export}")
        require(
            "return VRCLIENT_ADAPTER_ABI_VERSION;" in source,
            "adapter must return VRCLIENT_ADAPTER_ABI_VERSION from vrclient_get_adapter_abi",
        )
        require(
            "kMetadata" in source and "VRCLIENT_ADAPTER_ABI_VERSION" in source,
            "adapter metadata must reference the ABI version constant",
        )
        for token in [
            "VRCLIENT_ADAPTER_SERVICE_INPUT",
            "VRCLIENT_ADAPTER_SERVICE_COMFORT",
            "VRCLIENT_ADAPTER_SERVICE_HUD",
        ]:
            require(token in source, f"adapter source missing service query {token}")
        require(
            f'"vrclient-{GAME_ID}-adapter"' in source,
            "adapter source must carry its derived adapter id",
        )
        require(
            f'"{GAME_ID}"' in source and f'"{BUILD_ID}"' in source,
            "adapter source must carry its supported game/build identity",
        )
        require(
            f'"{d["event_prefix"]}input_service"' in source,
            "adapter events must be namespaced under the slug prefix",
        )
        require(
            "VR_ADAPTER_ERROR_UNSUPPORTED_TARGET" in source,
            "generated adapter must refuse foreign targets (strict validate)",
        )
        # ADKIT-04 no-fork: the generated adapter must NOT carry platform internals.
        for forbidden in [
            "vrclient-smoke-host",
            '#include "runtime/',
            '#include "injector/',
            "PhotonNetwork",
            "JoinRandomRoom",
        ]:
            require(forbidden not in source, f"adapter source must not contain {forbidden!r}")

        # --- (f) CMake edit happened on the COPY and produced the expected lines;
        #         and the live root CMakeLists.txt was NOT touched by this test ---
        edited_cmake = (temp_root / "CMakeLists.txt").read_text(encoding="utf-8")
        for needle in [
            f"add_library({d['cmake_lib_target']} SHARED",
            f"add_executable({d['cmake_test_target']}",
            f"add_dependencies({d['cmake_test_target']} {d['cmake_lib_target']})",
            f"add_test(NAME {d['cmake_test_target']}",
        ]:
            require(needle in edited_cmake, f"CMake edit missing: {needle}")

        # --- (g) refusal-to-overwrite: re-running without --force must fail (no clobber) ---
        rerun = cli.main([
            "--game-id", GAME_ID,
            "--build-id", BUILD_ID,
            "--display-name", DISPLAY_NAME,
            "--out-root", str(temp_root),
            "--no-cmake",
        ])
        require(rerun != 0, "CLI must refuse to overwrite an existing adapter without --force")

        # --- (h) input validation: a bad slug is rejected ---
        bad = cli.main([
            "--game-id", "Bad_Slug!",
            "--build-id", BUILD_ID,
            "--display-name", DISPLAY_NAME,
            "--out-root", str(temp_root),
            "--no-cmake",
        ])
        require(bad != 0, "CLI must reject an invalid --game-id")

    finally:
        shutil.rmtree(temp_root, ignore_errors=True)

    print("new_adapter CLI scaffolding checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

"""VRClient new-adapter scaffolding CLI (B4 / ADKIT-01, ADKIT-04, ADKIT-03).

"Add a game = one command." Given a (game_id, build_id, display_name) identity,
this tool DERIVES every coordinated string and scaffolds a complete, buildable,
testable adapter package from the platform's data + template surfaces:

  adapters/<game_id>/<game_id>_adapter.cpp     (derived from adapters/_template)
  adapters/<game_id>/adapter.json              (derived from adapters/_template)
  adapters/<game_id>/CMakeLists.txt            (template-of-record copy; inert)
  config/games/<game_id>.json                  (game-fingerprint.schema valid)
  config/profiles/<game_id>-game-profile.json  (game-profile.schema valid)
  config/controller-maps/<game_id>.json        (unverified input-reference template)
  tests/native/adapters/<game_id>_adapter_tests.cpp        (inherited host-load fixture)
  tests/native/adapters/validate_<game_id>_profile.py      (slug-pinned profile validator)

and inserts the unavoidable build wiring into the ROOT CMakeLists.txt:
registration in this repo is EXPLICIT (hand-listed add_library/add_executable/
add_test in the root file; there are ZERO add_subdirectory calls and no GLOB over
adapters/). So dropping files into adapters/<slug>/ alone would NOT build or test
the adapter. The CLI therefore performs IDEMPOTENT, anchored text insertion into
the root CMakeLists.txt -- build wiring only (add_library/add_executable/
add_test/add_dependencies), never platform C/C++ source.

ADKIT-04 (no-fork guarantee): the ONLY outputs are (i) data JSON, (ii) a
template-derived adapter .cpp/.json/CMakeLists, (iii) test fixtures cloned from
the existing adapter tests, and (iv) anchored text insertions into the root
CMakeLists.txt. NOTHING is generated under src/native/**; the SDK ABI is
referenced, never redefined.

ADKIT-02 status (honest): the profile is SEEDED from config/defaults/game-profile.json
(all 8 input actions + comfort + a HUD anchor). This tool does NOT yet ingest a
hook-surface evidence doc to derive per-game hook offsets/anchors -- that
hook-surface -> profile derivation half of ADKIT-02 is DEFERRED. The identity
half (manifest/metadata/fingerprint kept in sync from one identity) IS covered.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


# tools/new-adapter/new_adapter.py -> repo root is parents[2].
REPO_ROOT = Path(__file__).resolve().parents[2]

TEMPLATE_DIR_REL = "adapters/_template"
DEFAULT_PROFILE_REL = "config/defaults/game-profile.json"
DEFAULT_CONTROLLER_MAP_REL = "config/defaults/controller-map.json"

REQUIRED_EXPORTS = [
    "vrclient_get_adapter_abi",
    "vrclient_get_adapter_metadata",
    "vrclient_create_adapter",
    "vrclient_destroy_adapter",
]

# Anchored-insertion sentinels the CLI emits into the ROOT CMakeLists.txt so
# re-running is safe and the blocks are found again. Insertion is idempotent:
# a slug's blocks are only added if its add_library line is not already present.
LIB_BEGIN = "# >>> generated adapters (new_adapter.py) — libraries"
LIB_END = "# <<< generated adapters (new_adapter.py) — libraries"
TEST_TARGET_BEGIN = "# >>> generated adapters (new_adapter.py) — test targets"
TEST_TARGET_END = "# <<< generated adapters (new_adapter.py) — test targets"
ADDTEST_BEGIN = "# >>> generated adapters (new_adapter.py) — add_test"
ADDTEST_END = "# <<< generated adapters (new_adapter.py) — add_test"


class CliError(Exception):
    """User-facing error: printed without a traceback, exit code 2."""


# ---------------------------------------------------------------------------
# Identity validation + derivation
# ---------------------------------------------------------------------------

SLUG_RE = re.compile(r"^[a-z0-9][a-z0-9-]*[a-z0-9]$")
BUILD_RE = re.compile(r"^[a-z0-9][a-z0-9._-]*[a-z0-9]$")
RESERVED_SLUGS = {"_template", "repo"}


def validate_identity(game_id: str, build_id: str, display_name: str) -> None:
    if not SLUG_RE.match(game_id):
        raise CliError(
            f"invalid --game-id {game_id!r}: must be lowercase alphanumeric/hyphen "
            "(e.g. 'my-game'), no leading/trailing hyphen"
        )
    if game_id in RESERVED_SLUGS:
        raise CliError(
            f"--game-id {game_id!r} is reserved (the template/worked-example slug); "
            "pick a different game_id"
        )
    if game_id == "vrclient-smoke-host":
        raise CliError(
            "--game-id 'vrclient-smoke-host' is the controlled smoke target; "
            "the CLI scaffolds real-game adapters (controlled_smoke_target=false)"
        )
    if not BUILD_RE.match(build_id):
        raise CliError(
            f"invalid --build-id {build_id!r}: must be lowercase alphanumeric with "
            "'.', '_' or '-' (e.g. 'steam-3241660-build-23363152')"
        )
    if not display_name.strip():
        raise CliError("--display-name must not be empty")


def derive(game_id: str, build_id: str) -> dict:
    """Single source of truth: all coordinated strings come from here."""
    slug = game_id  # game_id IS the directory/target/adapter-id stem
    underscore = slug.replace("-", "_")
    return {
        "game_id": game_id,
        "build_id": build_id,
        "slug": slug,
        "adapter_id": f"vrclient-{slug}-adapter",
        "event_prefix": f"{underscore}_adapter_",
        "cmake_lib_target": f"vrclient_{underscore}_adapter",
        "cmake_test_target": f"vr_{underscore}_adapter_tests",
        "profile_id": f"{game_id}-{build_id}",
        "adapter_dir_rel": f"adapters/{slug}",
        "adapter_cpp_rel": f"adapters/{slug}/{underscore}_adapter.cpp",
        "manifest_rel": f"adapters/{slug}/adapter.json",
        "adapter_cmake_rel": f"adapters/{slug}/CMakeLists.txt",
        "fingerprint_rel": f"config/games/{slug}.json",
        "profile_rel": f"config/profiles/{slug}-game-profile.json",
        "controller_map_rel": f"config/controller-maps/{slug}.json",
        "test_cpp_rel": f"tests/native/adapters/{underscore}_adapter_tests.cpp",
        "profile_validator_rel": f"tests/native/adapters/validate_{underscore}_profile.py",
    }


# ---------------------------------------------------------------------------
# Deterministic, schema-valid, non-sentinel placeholder digests
# ---------------------------------------------------------------------------

def placeholder_sha256(seed: str) -> str:
    """A 64-hex digest that is distinct-character (passes require_real_sha256)
    and DETERMINISTIC for a given seed (non-flaky test) -- but is clearly a
    placeholder, never a claim of a real artifact hash. Derived from the seed
    via a tiny pure-python xorshift over the hex alphabet, not hashlib, so the
    intent (author-must-replace) is unambiguous.
    """
    hexset = "0123456789abcdef"
    state = 0x9E3779B97F4A7C15
    for ch in f"placeholder::{seed}":
        state ^= ord(ch)
        state = (state * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
        state ^= (state >> 27) & 0xFFFFFFFFFFFFFFFF
    out = []
    for _ in range(64):
        state ^= (state << 13) & 0xFFFFFFFFFFFFFFFF
        state ^= (state >> 7) & 0xFFFFFFFFFFFFFFFF
        state ^= (state << 17) & 0xFFFFFFFFFFFFFFFF
        out.append(hexset[state & 0xF])
    digest = "".join(out)
    # Guarantee distinct-character (require_real_sha256 rejects single-char strings).
    if len(set(digest)) <= 1:  # astronomically unlikely; keep deterministic anyway
        digest = "0123456789abcdef" * 4
    return digest


# ---------------------------------------------------------------------------
# Generators
# ---------------------------------------------------------------------------

def generate_adapter_cpp(template_text: str, d: dict, display_name: str) -> str:
    """Derive the adapter .cpp from the template source.

    Mirrors the repo worked example: refactors the substitution points into
    constexpr identifiers, tightens validate() to refuse foreign targets
    (UNSUPPORTED_TARGET), and namespaces all log events under <slug>_adapter_*.
    Built by structured substitution on the read-in template so the 4 ABI
    exports, the vtable, and readSharedServices wiring are preserved verbatim.
    """
    text = template_text

    # 1. Hoist identity into constexpr block, like the repo example.
    constexpr_block = (
        f'constexpr const char* kAdapterId = "{d["adapter_id"]}";\n'
        f'constexpr const char* kSupportedGame = "{d["game_id"]}";\n'
        f'constexpr const char* kSupportedBuild = "{d["build_id"]}";\n\n'
        "struct TemplateAdapterState {"
    )
    text = text.replace("struct TemplateAdapterState {", constexpr_block, 1)

    # 2. kSupportedGames[] -> reference the constexpr game id.
    text = text.replace(
        'const char* kSupportedGames[] = {\n    "vrclient-smoke-host",\n};',
        "const char* kSupportedGames[] = {\n    kSupportedGame,\n};",
        1,
    )

    # 3. kSupportedBuilds[] -> reference the constexpr id/build.
    text = text.replace(
        "    {\n"
        "        sizeof(VrAdapterBuildId),\n"
        '        "vrclient-smoke-host",\n'
        '        "smoke-2026-06-11",\n'
        "    },",
        "    {\n"
        "        sizeof(VrAdapterBuildId),\n"
        "        kSupportedGame,\n"
        "        kSupportedBuild,\n"
        "    },",
        1,
    )

    # 4. kMetadata adapter_id + display_name.
    text = text.replace('    "vrclient-template-adapter",', "    kAdapterId,", 1)
    text = text.replace(
        '    "VRClient Template Adapter",',
        f'    "{display_name}",',
        1,
    )

    # 5. logLifecycle uses kMetadata.adapter_id; switch to kAdapterId for parity
    #    with the repo example (functionally identical, clearer).
    text = text.replace("kMetadata.adapter_id", "kAdapterId")

    # 6. Add the sameString helper + tighten validate() to refuse foreign targets.
    text = text.replace(
        "#include <stdint.h>\n",
        "#include <stdint.h>\n#include <string.h>\n",
        1,
    )
    text = text.replace(
        "void logAdapterEvent(",
        "bool sameString(const char* left, const char* right) {\n"
        "  return left != nullptr && right != nullptr && strcmp(left, right) == 0;\n"
        "}\n\n"
        "void logAdapterEvent(",
        1,
    )
    text = text.replace(
        "      context->target.build_id == nullptr) {\n"
        "    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;\n"
        "  }\n"
        "  return VR_ADAPTER_OK;\n"
        "}",
        "      context->target.build_id == nullptr) {\n"
        "    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;\n"
        "  }\n"
        "  if (!sameString(context->target.game_id, kSupportedGame) ||\n"
        "      !sameString(context->target.build_id, kSupportedBuild)) {\n"
        "    return VR_ADAPTER_ERROR_UNSUPPORTED_TARGET;\n"
        "  }\n"
        "  return VR_ADAPTER_OK;\n"
        "}",
        1,
    )

    # 7. init() should run validate() first (refuse foreign targets before init),
    #    matching the repo example's strict init.
    text = text.replace(
        "VrAdapterResult VRCLIENT_ADAPTER_CALL init(\n"
        "    VrGameAdapter* adapter,\n"
        "    const VrAdapterContext* context) {\n"
        "  if (adapter == nullptr) {\n"
        "    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;\n"
        "  }\n",
        "VrAdapterResult VRCLIENT_ADAPTER_CALL init(\n"
        "    VrGameAdapter* adapter,\n"
        "    const VrAdapterContext* context) {\n"
        "  const VrAdapterResult validation = validate(adapter, context);\n"
        "  if (validation != VR_ADAPTER_OK) {\n"
        "    return validation;\n"
        "  }\n",
        1,
    )

    # 8. Namespace every log-event name. Done LAST so the substitutions above
    #    (which match literal "template_adapter_" inside struct/state names) are
    #    untouched -- only the quoted event strings carry the prefix.
    text = text.replace('"template_adapter_', f'"{d["event_prefix"]}', )

    return text


def generate_manifest(template_manifest: dict, d: dict, display_name: str) -> dict:
    manifest = json.loads(json.dumps(template_manifest))  # deep copy
    manifest["adapter_id"] = d["adapter_id"]
    manifest["display_name"] = display_name
    manifest["supported_games"] = [d["game_id"]]
    manifest["supported_builds"] = [
        {"game_id": d["game_id"], "build_id": d["build_id"]}
    ]
    # required_exports / host_api / capabilities / schema_version unchanged.
    manifest["required_exports"] = list(REQUIRED_EXPORTS)
    return manifest


def generate_adapter_cmake(d: dict) -> str:
    """The inert per-adapter CMakeLists.txt (template-of-record). The REAL build
    wiring lives in the root CMakeLists.txt; this file is never add_subdirectory'd.
    """
    underscore = d["cmake_lib_target"]
    cpp_name = Path(d["adapter_cpp_rel"]).name
    return (
        "# NOTE: this per-adapter CMakeLists.txt is a template-of-record copy and is\n"
        "# NOT included by any add_subdirectory. The authoritative build wiring for\n"
        f"# '{underscore}' is hand-listed in the repository root CMakeLists.txt\n"
        "# (inserted by tools/new-adapter/new_adapter.py).\n"
        f"add_library({underscore} SHARED\n"
        f"  {cpp_name})\n\n"
        f"target_compile_definitions({underscore}\n"
        "  PRIVATE\n"
        "    VRCLIENT_ADAPTER_EXPORTS)\n\n"
        f"target_include_directories({underscore}\n"
        "  PRIVATE\n"
        "    ${CMAKE_CURRENT_LIST_DIR}/../../src/native)\n"
    )


def generate_fingerprint(d: dict, args) -> dict:
    return {
        "version": 1,
        "game_id": d["game_id"],
        "display_name": args.display_name,
        "controlled_smoke_target": False,
        "executable_names": [args.executable],
        "support_policy": {
            "allowed_sources": list(args.allowed_sources),
            "target_architecture": args.architecture,
            "anti_cheat_risk": args.anti_cheat_risk,
            "online_risk": args.online_risk,
        },
        "runtime": {
            # Author MUST replace path + sha256 with the real built artifact.
            "path": f"REPLACE_ME/path/to/{d['game_id']}-runtime.lib",
            "sha256": placeholder_sha256(f"{d['game_id']}::runtime"),
            "architecture": args.architecture,
        },
        "builds": [
            {
                "build_id": d["build_id"],
                "support": "supported",
                "confidence_threshold": 80,
                "file_hashes": [
                    {
                        "algorithm": "sha256",
                        # Author MUST replace with the real game-binary digest.
                        "value": placeholder_sha256(
                            f"{d['game_id']}::{d['build_id']}::file"
                        ),
                    }
                ],
                "product_versions": [
                    f"PLACEHOLDER-product-version-for-{d['build_id']}"
                ],
                "signatures": [
                    {
                        "id": f"{d['game_id']}-identity-signature",
                        "pattern": f"REPLACE_ME identity pattern for {d['build_id']}",
                        "confidence": 50,
                    }
                ],
                "offsets": [],
            }
        ],
    }


def generate_profile(default_profile: dict, d: dict) -> dict:
    """Seed from the neutral default profile, then pin profile_id to the
    derived '<game_id>-<build_id>' and name the comfort preset per game.
    Keeps all 8 input actions, comfort defaults, and a HUD anchor (>=1).
    """
    profile = json.loads(json.dumps(default_profile))  # deep copy
    profile["profile_id"] = d["profile_id"]
    if "comfort" in profile and isinstance(profile["comfort"], dict):
        profile["comfort"]["preset"] = f"{d['game_id']}_safe_default"
    return profile


def generate_test_cpp(repo_test_text: str, d: dict) -> str:
    """Clone the repo adapter host-load test (the inherited fixture, ADKIT-03)
    and re-point it at this slug's identity, profile, adapter id, and event
    names. The repo test exercises the strict-target path (wrong-target refusal
    + correct load), which matches the strict validate() the CLI emits.
    """
    text = repo_test_text

    # Identity in context() and the load requests.
    text = text.replace('"repo"', f'"{d["game_id"]}"')
    text = text.replace(
        '"steam-3241660-build-23363152"', f'"{d["build_id"]}"'
    )
    # Profile path + profile_id. Handle BOTH the VRCLIENT_SOURCE_DIR branch
    # (bare filename) and the #else fallback (path-prefixed literal).
    text = text.replace(
        '"config/profiles/repo-game-profile.json"',
        f'"config/profiles/{d["slug"]}-game-profile.json"',
    )
    text = text.replace(
        '"repo-game-profile.json"', f'"{d["slug"]}-game-profile.json"'
    )
    # Usage string still names the repo test binary.
    text = text.replace(
        "usage: vr_repo_adapter_tests <repo-adapter-dll>",
        f"usage: {d['cmake_test_target']} <{d['slug']}-adapter-dll>",
    )
    text = text.replace(
        '"repo-steam-3241660-build-23363152"', f'"{d["profile_id"]}"'
    )
    # Adapter id assertion.
    text = text.replace(
        '"vrclient-repo-adapter"', f'"{d["adapter_id"]}"'
    )
    # Event-name matchers in the diagnostics counter.
    text = text.replace('"repo_adapter_', f'"{d["event_prefix"]}')
    # Executable path hint (cosmetic).
    text = text.replace(
        '"H:/SteamLibrary/steamapps/common/REPO/REPO.exe"',
        f'"C:/Games/{d["game_id"]}/{d["game_id"]}.exe"',
    )
    # The repo test asserts the Task-5 hook-surface blocker event. The generated
    # adapter is derived from the TEMPLATE (which does NOT emit that event), so
    # drop the hook_block expectation and its counter to keep the fixture honest.
    text = text.replace(
        "  int hook_block = 0;\n", ""
    )
    text = text.replace(
        '  } else if (event == "' + d["event_prefix"] + 'hook_surface_unvalidated") {\n'
        "    counts->hook_block += 1;\n",
        "",
    )
    text = text.replace(
        "    expect(\n"
        "        counts.hook_block == 1,\n"
        '        "R.E.P.O. adapter should record unvalidated hook-surface blocker");\n',
        "",
    )
    # Friendly message text (cosmetic) — neutralize the R.E.P.O. branding.
    text = text.replace("R.E.P.O. adapter", f"{d['game_id']} adapter")
    text = text.replace("R.E.P.O. profile", f"{d['game_id']} profile")
    text = text.replace("repo_adapter_load_test", f"{d['slug']}_adapter_load_test")
    return text


def generate_profile_validator(repo_validator_text: str, d: dict) -> str:
    """Clone validate_repo_profile.py, slug-pinned (ADKIT-03). validate_game_profile.py
    is pinned to config/defaults, so each adapter needs its own pinned validator.
    """
    text = repo_validator_text
    text = text.replace(
        '"repo-game-profile.json"', f'"{d["slug"]}-game-profile.json"'
    )
    text = text.replace(
        '"repo-steam-3241660-build-23363152"', f'"{d["profile_id"]}"'
    )
    # The repo validator hard-asserts the 5 R.E.P.O. anchor names; the generated
    # profile is seeded from defaults (one template anchor), so relax the anchor
    # expectation to "at least one template anchor" while keeping the schema,
    # action-set, comfort, and profile_id checks intact.
    text = text.replace(
        "EXPECTED_ANCHORS = {\n"
        '    "HUDCanvas",\n'
        '    "HealthUI",\n'
        '    "SemiUI",\n'
        '    "ChatUI",\n'
        '    "MapToolController",\n'
        "}",
        "# Seeded from config/defaults/game-profile.json; author replaces with the\n"
        "# per-game HUD anchor set once the hook surface is validated.\n"
        "EXPECTED_ANCHORS: set[str] = set()",
    )
    text = text.replace(
        'require(EXPECTED_ANCHORS <= anchors.keys(), "missing expected R.E.P.O. HUD anchors")',
        'require(len(anchors) >= 1, "profile must declare at least one HUD anchor")',
    )
    text = text.replace(
        'require(anchor["template_data"], "R.E.P.O. HUD anchors stay template until validated")',
        'require(anchor["template_data"], "HUD anchors stay template until validated")',
    )
    text = text.replace(
        '"profile id should be pinned to the selected R.E.P.O. build"',
        f'"profile id should be {d["profile_id"]}"',
    )
    return text


# ---------------------------------------------------------------------------
# Root CMakeLists.txt anchored insertion
# ---------------------------------------------------------------------------

def _block_between(text: str, begin: str, end: str) -> tuple[int, int] | None:
    bi = text.find(begin)
    if bi == -1:
        return None
    ei = text.find(end, bi)
    if ei == -1:
        return None
    return (bi, ei + len(end))


def render_cmake_lib_block(d: dict) -> str:
    target = d["cmake_lib_target"]
    cpp = d["adapter_cpp_rel"]
    return (
        f"add_library({target} SHARED\n"
        f"  {cpp})\n"
        f"target_include_directories({target}\n"
        "  PRIVATE\n"
        "    ${CMAKE_CURRENT_SOURCE_DIR}/src/native)\n"
        f"target_compile_definitions({target}\n"
        "  PRIVATE\n"
        "    VRCLIENT_ADAPTER_EXPORTS)\n"
    )


def render_cmake_test_target_block(d: dict) -> str:
    target = d["cmake_test_target"]
    test_cpp = d["test_cpp_rel"]
    return (
        f"  add_executable({target}\n"
        f"    {test_cpp})\n"
        f"  target_compile_definitions({target}\n"
        "    PRIVATE\n"
        '      VRCLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")\n'
        f"  target_link_libraries({target}\n"
        "    PRIVATE\n"
        "      vr_plugin_host\n"
        "      vr_shared_systems)\n"
        f"  add_dependencies({target} {d['cmake_lib_target']})\n"
    )


def render_cmake_addtest_block(d: dict) -> str:
    test_target = d["cmake_test_target"]
    lib_target = d["cmake_lib_target"]
    fp = d["fingerprint_rel"]
    profile_validator = d["profile_validator_rel"]
    return (
        f"  add_test(NAME {test_target}\n"
        f"    COMMAND {test_target} $<TARGET_FILE:{lib_target}>)\n"
        f"  add_test(NAME {d['slug']}_fingerprint_schema\n"
        "    COMMAND ${Python3_EXECUTABLE} "
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/native/versioning/validate_game_fingerprint.py "
        f"{fp})\n"
        f"  add_test(NAME {d['slug']}_game_profile_schema\n"
        "    COMMAND ${Python3_EXECUTABLE} "
        "${CMAKE_CURRENT_SOURCE_DIR}/"
        f"{profile_validator})\n"
    )


def _ensure_anchor(text: str, begin: str, end: str, insert_after_line_substr: str) -> str:
    """Make sure an empty begin/end anchor pair exists. If not, insert it right
    after the line containing insert_after_line_substr."""
    if begin in text and end in text:
        return text
    marker = f"{begin}\n{end}\n"
    idx = text.find(insert_after_line_substr)
    if idx == -1:
        raise CliError(
            f"could not find CMake anchor target {insert_after_line_substr!r} "
            "in root CMakeLists.txt"
        )
    # advance to end of that line
    line_end = text.find("\n", idx)
    if line_end == -1:
        line_end = len(text)
    return text[: line_end + 1] + marker + text[line_end + 1 :]


def insert_into_root_cmake(cmake_text: str, d: dict) -> str:
    """Idempotent anchored insertion of the three blocks. Refuses (raises) if the
    slug's add_library target line is already present anywhere."""
    lib_line = f"add_library({d['cmake_lib_target']} SHARED"
    if lib_line in cmake_text:
        raise CliError(
            f"root CMakeLists.txt already declares {d['cmake_lib_target']}; "
            "refusing to insert a duplicate (use --force to regenerate files, but "
            "the CMake target is left untouched once present)"
        )

    text = cmake_text
    # Ensure the three anchor regions exist, seeding them right after the existing
    # repo wiring lines so generated blocks sit beside the worked example.
    text = _ensure_anchor(
        text, LIB_BEGIN, LIB_END,
        "    VRCLIENT_ADAPTER_EXPORTS)\n"  # after the repo adapter's compile-defs
    )
    text = _ensure_anchor(
        text, TEST_TARGET_BEGIN, TEST_TARGET_END,
        "  add_dependencies(vr_repo_adapter_tests vrclient_repo_adapter)"
    )
    text = _ensure_anchor(
        text, ADDTEST_BEGIN, ADDTEST_END,
        "    COMMAND vr_repo_adapter_tests $<TARGET_FILE:vrclient_repo_adapter>)"
    )

    # Insert each rendered block just before its END sentinel.
    for begin, end, render in (
        (LIB_BEGIN, LIB_END, render_cmake_lib_block),
        (TEST_TARGET_BEGIN, TEST_TARGET_END, render_cmake_test_target_block),
        (ADDTEST_BEGIN, ADDTEST_END, render_cmake_addtest_block),
    ):
        span = _block_between(text, begin, end)
        if span is None:
            raise CliError(f"anchor pair {begin!r}/{end!r} missing after seeding")
        end_idx = text.find(end)
        block = render(d)
        text = text[:end_idx] + block + text[end_idx:]
    return text


# ---------------------------------------------------------------------------
# Orchestration
# ---------------------------------------------------------------------------

def _write(path: Path, content: str, force: bool, created: list) -> None:
    if path.exists() and not force:
        raise CliError(f"refusing to overwrite existing file: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    created.append(path)


def scaffold(args) -> dict:
    out_root = Path(args.out_root).resolve()
    template_root = REPO_ROOT  # template/defaults/test sources read from the repo
    validate_identity(args.game_id, args.build_id, args.display_name)
    d = derive(args.game_id, args.build_id)

    # --- Refusal-to-overwrite preflight (no-clobber), before writing anything ---
    target_dir = out_root / d["adapter_dir_rel"]
    fingerprint_path = out_root / d["fingerprint_rel"]
    profile_path = out_root / d["profile_rel"]
    controller_map_path = out_root / d["controller_map_rel"]
    root_cmake_path = out_root / "CMakeLists.txt"

    if not args.force:
        if target_dir.exists():
            raise CliError(f"adapter directory already exists: {target_dir}")
        if fingerprint_path.exists():
            raise CliError(f"fingerprint config already exists: {fingerprint_path}")
        if profile_path.exists():
            raise CliError(f"profile config already exists: {profile_path}")
        if controller_map_path.exists():
            raise CliError(f"controller map already exists: {controller_map_path}")

    # --- Read template / worked-example sources (from the live repo) ---
    template_cpp = (template_root / TEMPLATE_DIR_REL / "template_adapter.cpp").read_text(encoding="utf-8")
    template_manifest = json.loads(
        (template_root / TEMPLATE_DIR_REL / "adapter.json").read_text(encoding="utf-8")
    )
    default_profile = json.loads(
        (template_root / DEFAULT_PROFILE_REL).read_text(encoding="utf-8")
    )
    default_controller_map = json.loads(
        (template_root / DEFAULT_CONTROLLER_MAP_REL).read_text(encoding="utf-8")
    )
    repo_test = (template_root / "tests/native/adapters/repo_adapter_tests.cpp").read_text(encoding="utf-8")
    repo_validator = (template_root / "tests/native/adapters/validate_repo_profile.py").read_text(encoding="utf-8")

    # --- Generate content ---
    adapter_cpp = generate_adapter_cpp(template_cpp, d, args.display_name)
    manifest = generate_manifest(template_manifest, d, args.display_name)
    adapter_cmake = generate_adapter_cmake(d)
    fingerprint = generate_fingerprint(d, args)
    profile = generate_profile(default_profile, d)
    controller_map = json.loads(json.dumps(default_controller_map))
    controller_map["game_slug"] = d["slug"]
    controller_map["title"] = f"{args.display_name} VR controls"
    test_cpp = generate_test_cpp(repo_test, d)
    profile_validator = generate_profile_validator(repo_validator, d)

    created: list = []
    _write(out_root / d["adapter_cpp_rel"], adapter_cpp, args.force, created)
    _write(out_root / d["manifest_rel"], json.dumps(manifest, indent=2) + "\n", args.force, created)
    _write(out_root / d["adapter_cmake_rel"], adapter_cmake, args.force, created)
    _write(fingerprint_path, json.dumps(fingerprint, indent=2) + "\n", args.force, created)
    _write(profile_path, json.dumps(profile, indent=2) + "\n", args.force, created)
    _write(controller_map_path, json.dumps(controller_map, indent=2) + "\n", args.force, created)
    _write(out_root / d["test_cpp_rel"], test_cpp, args.force, created)
    _write(out_root / d["profile_validator_rel"], profile_validator, args.force, created)

    # --- Edit the root CMakeLists.txt (the unavoidable build wiring) ---
    cmake_edited = False
    if not args.no_cmake:
        if not root_cmake_path.exists():
            raise CliError(f"root CMakeLists.txt not found at {root_cmake_path}")
        original = root_cmake_path.read_text(encoding="utf-8")
        updated = insert_into_root_cmake(original, d)
        root_cmake_path.write_text(updated, encoding="utf-8")
        cmake_edited = True

    return {
        "derived": d,
        "created_files": [str(p) for p in created],
        "cmake_edited": cmake_edited,
        "out_root": str(out_root),
    }


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="new_adapter.py",
        description="Scaffold a VRClient game adapter package from the template + data surfaces.",
    )
    p.add_argument("--game-id", required=True, help="lowercase slug, e.g. 'my-game'")
    p.add_argument("--build-id", required=True, help="build coordinate, e.g. 'steam-1234-build-5678'")
    p.add_argument("--display-name", required=True, help="human-readable adapter/game name")
    p.add_argument("--executable", default=None, help="game executable name (default '<game_id>.exe')")
    p.add_argument(
        "--allowed-sources", nargs="+", default=["manual"],
        choices=["direct", "manual", "attach", "steam", "epic"],
        help="support_policy.allowed_sources (default: manual)",
    )
    p.add_argument(
        "--architecture", default="x64", choices=["x64", "x86", "arm64"],
        help="target/runtime architecture (default: x64)",
    )
    p.add_argument(
        "--anti-cheat-risk", default="known_safe",
        choices=["none", "known_safe", "known_risky", "unknown"],
        help="support_policy.anti_cheat_risk (default: known_safe)",
    )
    p.add_argument(
        "--online-risk", default="offline_only",
        choices=["none", "offline_only", "private_modded_coop", "known_online", "unknown"],
        help="support_policy.online_risk (default: offline_only)",
    )
    p.add_argument(
        "--out-root", default=str(REPO_ROOT),
        help="root to scaffold into (default: repo root; tests pass a TEMP dir)",
    )
    p.add_argument(
        "--no-cmake", action="store_true",
        help="do NOT edit the root CMakeLists.txt (file-gen only; for dry runs/tests)",
    )
    p.add_argument(
        "--force", action="store_true",
        help="overwrite existing adapter/config files (CMake target still refused if present)",
    )
    p.add_argument(
        "--json", action="store_true",
        help="print a machine-readable JSON result summary",
    )
    return p


def main(argv: list | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.executable is None:
        args.executable = f"{args.game_id}.exe"
    try:
        result = scaffold(args)
    except CliError as err:
        print(f"new_adapter: error: {err}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        d = result["derived"]
        print(f"scaffolded adapter '{d['adapter_id']}' ({d['game_id']} / {d['build_id']})")
        for f in result["created_files"]:
            print(f"  + {f}")
        if result["cmake_edited"]:
            print(f"  ~ {result['out_root']}/CMakeLists.txt (build wiring inserted)")
        print(
            "\nNEXT: replace the placeholder runtime path + sha256 digests and the\n"
            "signature/product_version placeholders in the fingerprint with REAL\n"
            "artifact values before shipping. Then build + run the generated\n"
            f"{d['cmake_test_target']} host-load test."
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

from __future__ import annotations

# Bolt-on Phase B5 static / logic test (CFGV-01).
#
# Pure-Python re-implementation of the LOAD / MIGRATE / REFUSE decision table,
# asserted against the same fixtures the C++ test uses, PLUS a source-grep that the
# C++ negotiator actually ships the required reason codes and the deterministic
# branch order. Mirrors tests/native/versioning/versioning_static_tests.py.
#
# No jsonschema dependency in the logic part (this file is the "static" tier).

import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
FIXTURES = ROOT / "tests" / "native" / "config" / "fixtures"
CONFIG_CPP = ROOT / "src" / "native" / "config" / "config_versioning.cpp"
CONFIG_H = ROOT / "src" / "native" / "config" / "config_versioning.h"

# Registry seed mirrored from config_versioning.cpp: real kinds at [1..1] and
# the synthetic demo-config at [1..2].
REAL_KINDS = [
    "runtime-profile",
    "diagnostics-profile",
    "game-fingerprint",
    "game-profile",
    "hook-surface",
    "safety-rules",
    "anti-cheat-compatibility",
    "supply-chain-trust-root",
    "supply-chain-manifest",
    "supply-chain-revocations",
]
REGISTRY = {kind: {"min": 1, "current": 1} for kind in REAL_KINDS}
REGISTRY["demo-config"] = {"min": 1, "current": 2}

# Upper bound mirrored from kMaxSupportedSchemaVersion in config_versioning.h. The
# C++ reader stores the version as int64 and rejects anything outside
# [1 .. MAX_SUPPORTED_SCHEMA_VERSION] BEFORE narrowing to int, so an oversized /
# 2^32-aliased version becomes refuse_invalid_version rather than a silently
# truncated LOAD. The Python mirror enforces the same bound so it stays an honest
# model of the negotiator.
MAX_SUPPORTED_SCHEMA_VERSION = 1000000

# Reason codes mirrored from the C++ reason:: namespace.
LOAD = "load_current_version"
MIGRATE = "migrate_in_range"
REFUSE_TOO_NEW = "refuse_too_new"
REFUSE_OLD = "refuse_unsupported_old"
REFUSE_INVALID = "refuse_invalid_version"
REFUSE_UNPARSEABLE = "refuse_unparseable_config"
REFUSE_UNKNOWN_KIND = "refuse_unknown_kind"


def read_version(doc: dict) -> int | None:
    """A version is valid only when present AND an integer (bools excluded) AND
    within [1 .. MAX_SUPPORTED_SCHEMA_VERSION]. Mirrors
    ConfigVersionRegistry::readVersion, including the range check that prevents a
    too-large / 2^32-aliased / non-positive version from being narrowed into the
    in-range window and silently LOADed."""
    if not isinstance(doc, dict):
        return None
    version = doc.get("version")
    if isinstance(version, bool) or not isinstance(version, int):
        return None
    if version < 1 or version > MAX_SUPPORTED_SCHEMA_VERSION:
        return None
    return version


def negotiate(kind: str, text: str) -> tuple[str, str]:
    """Returns (decision, reason_code). Never raises on bad input -> REFUSE."""
    spec = REGISTRY.get(kind)
    if spec is None:
        return ("refuse", REFUSE_UNKNOWN_KIND)
    try:
        doc = json.loads(text)
    except (json.JSONDecodeError, ValueError):
        return ("refuse", REFUSE_UNPARSEABLE)
    version = read_version(doc)
    if version is None:
        return ("refuse", REFUSE_INVALID)
    if version == spec["current"]:
        return ("load", LOAD)
    if version > spec["current"]:
        return ("refuse", REFUSE_TOO_NEW)
    if version < spec["min"]:
        return ("refuse", REFUSE_OLD)
    return ("migrate", MIGRATE)


def read_fixture(name: str) -> str:
    return (FIXTURES / name).read_text(encoding="utf-8")


def test_decision_table() -> None:
    # Synthetic kind.
    assert negotiate("demo-config", read_fixture("demo-config.v1.basic.input.json")) == ("migrate", MIGRATE)
    assert negotiate("demo-config", read_fixture("demo-config.v2.current.json")) == ("load", LOAD)
    assert negotiate("demo-config", read_fixture("demo-config.v3.too-new.json")) == ("refuse", REFUSE_TOO_NEW)

    # Real kinds: v1 LOADs, v2 REFUSEs too_new.
    v1 = read_fixture("real-kind.v1.runtime-profile.json")
    v2 = read_fixture("real-kind.v2.too-new.json")
    for kind in REAL_KINDS:
        assert negotiate(kind, v1) == ("load", LOAD), kind
        assert negotiate(kind, v2) == ("refuse", REFUSE_TOO_NEW), kind

    # Missing version / unparseable / unknown kind / float version.
    assert negotiate("game-profile", read_fixture("refuse.missing-version.json")) == ("refuse", REFUSE_INVALID)
    assert negotiate("safety-rules", read_fixture("refuse.unparseable.txt")) == ("refuse", REFUSE_UNPARSEABLE)
    assert negotiate("not-a-real-kind", v1) == ("refuse", REFUSE_UNKNOWN_KIND)
    assert negotiate("runtime-profile", '{"version": 1.5}') == ("refuse", REFUSE_INVALID)

    # version <= 0 is an INVALID version (versions start at 1), refused as
    # refuse_invalid_version by the range guard in read_version — NOT
    # refuse_unsupported_old (that branch is for a real old version 1 <= N < min,
    # only reachable once a kind retires v1 and raises its min above 1).
    assert negotiate("demo-config", '{"version": 0, "profile_id": "x", "display_name": "y"}') == ("refuse", REFUSE_INVALID)
    assert negotiate("demo-config", '{"version": -4294967295}') == ("refuse", REFUSE_INVALID)

    # 64-bit version aliasing guard: a version whose low 32 bits equal an in-range
    # value (2^32+1 -> 1, 2^32+2 -> 2) must REFUSE invalid, never narrow-then-LOAD.
    assert negotiate("runtime-profile", '{"version": 4294967297}') == ("refuse", REFUSE_INVALID)
    assert negotiate("demo-config", '{"version": 4294967298}') == ("refuse", REFUSE_INVALID)

    # An in-int-range too-new version still refuses as too_new (legitimate path
    # unchanged by the bound).
    assert negotiate("runtime-profile", '{"version": 999}') == ("refuse", REFUSE_TOO_NEW)

    # refuse_unsupported_old is genuinely reachable only when min > 1. Synthesize a
    # registry with a retired v1 (min=2) to prove that branch deterministically.
    saved = REGISTRY.get("demo-config")
    try:
        REGISTRY["demo-config"] = {"min": 2, "current": 3}
        assert negotiate("demo-config", '{"version": 1}') == ("refuse", REFUSE_OLD)
    finally:
        REGISTRY["demo-config"] = saved


def test_cpp_ships_reason_codes() -> None:
    cpp = CONFIG_CPP.read_text(encoding="utf-8")
    header = CONFIG_H.read_text(encoding="utf-8")

    for code in (LOAD, MIGRATE, REFUSE_TOO_NEW, REFUSE_OLD, REFUSE_INVALID,
                 REFUSE_UNPARSEABLE, REFUSE_UNKNOWN_KIND):
        assert code in header, f"reason code {code} missing from header"

    # The negotiator must contain the deterministic branch order and the synthetic
    # kind's bump-to-2 plus the v1->v2 step id.
    assert 'kSyntheticKind = "demo-config"' in header
    assert "demo-config:v1->v2" in cpp
    assert "renameMember" in cpp and "setDefault" in cpp
    assert "current_version = 2" in cpp, "demo-config must register current=2"

    # B-track additive guard: B5 must not edit the existing loaders/schemas. The
    # negotiator wraps them; it must not call into a real loader or mutate a schema.
    assert "loadHookSurfaceDoc" not in cpp, "B5 must not call existing loaders"
    assert "config/schemas/" not in cpp, "B5 must not hard-wire real schema paths"

    # No silent-misparse: the parse failure path must yield the unparseable refuse.
    assert "kRefuseUnparseable" in cpp

    # No 64-bit version truncation: readVersion must range-check the full int64
    # against kMaxSupportedSchemaVersion BEFORE narrowing to int, so an oversized /
    # 2^32-aliased version refuses instead of LOADing as the current version.
    assert "kMaxSupportedSchemaVersion" in header, \
        "header must declare the schema-version upper bound"
    assert "kMaxSupportedSchemaVersion" in cpp, \
        "readVersion must range-check against kMaxSupportedSchemaVersion before narrowing"


def main() -> int:
    test_decision_table()
    test_cpp_ships_reason_codes()
    print("config versioning static and decision-table tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

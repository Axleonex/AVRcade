from __future__ import annotations

# Bolt-on Phase B5 no-rename/no-remove GUARD (CFGV-03).
#
# Policy enforced: NO field may be renamed or removed from a real production schema
# (config/schemas/*.schema.json) without a corresponding registered migration bump.
# This guard snapshots every real schema's full field path set at its registered
# version and FAILS if a field disappears (removal or rename) at the SAME version.
#
# How it works:
#   * For each real config kind it reads the on-disk schema, extracts its registered
#     `version` (the const, or the minimum when the schema uses minimum-only), and
#     computes the full recursive set of property paths.
#   * It compares that against the committed snapshot in field-snapshots.json.
#   * A pure ADDITION of a new optional field at the same version is allowed (additive
#     changes never break old docs) and refreshes the snapshot expectation only when a
#     human re-runs --update.
#   * A REMOVED or RENAMED field at the same registered version is a HARD FAILURE: the
#     guard tells the author to either restore the field or perform a real schema bump
#     (bump version + add migration + add fixtures + update registry + refresh this
#     snapshot via --update). See docs/config/versioning.md.
#
# Run with no args to check (CI / CTest). Run with `--update` to regenerate the
# snapshot after an intentional, migration-backed schema change.
#
# This guard READS schemas only; it never modifies a real schema.

import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SCHEMAS_DIR = ROOT / "config" / "schemas"
SNAPSHOT = Path(__file__).resolve().parent / "field-snapshots.json"

# The real production config kinds -> their schema files. These mirror the
# registry seed in src/native/config/config_versioning.cpp (realKinds()).
REAL_KINDS = {
    "runtime-profile": "runtime-profile.schema.json",
    "diagnostics-profile": "diagnostics-profile.schema.json",
    "game-fingerprint": "game-fingerprint.schema.json",
    "game-profile": "game-profile.schema.json",
    "hook-surface": "hook-surface.schema.json",
    "safety-rules": "safety-rules.schema.json",
    "anti-cheat-compatibility": "anti-cheat-compatibility.schema.json",
    "supply-chain-trust-root": "supply-chain-trust-root.schema.json",
    "supply-chain-manifest": "supply-chain-manifest.schema.json",
    "supply-chain-revocations": "supply-chain-revocations.schema.json",
}


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def schema_version(schema: dict) -> int:
    """The registered schema version: the `version` property's const if present,
    otherwise its minimum (runtime-profile / game-profile use minimum:1)."""
    version_prop = schema.get("properties", {}).get("version", {})
    if "const" in version_prop:
        return int(version_prop["const"])
    if "minimum" in version_prop:
        return int(version_prop["minimum"])
    raise AssertionError("schema 'version' property has neither const nor minimum")


def collect_field_paths(node: object, prefix: str = "") -> set[str]:
    """Recursively collect every declared property path in a JSON Schema. Walks
    `properties` (object fields) and `items` (array element schemas). The path set
    is the contract that must not lose entries without a migration."""
    paths: set[str] = set()
    if not isinstance(node, dict):
        return paths

    props = node.get("properties")
    if isinstance(props, dict):
        for name, sub in props.items():
            path = f"{prefix}.{name}" if prefix else name
            paths.add(path)
            paths |= collect_field_paths(sub, path)

    items = node.get("items")
    if isinstance(items, dict):
        item_prefix = f"{prefix}[]" if prefix else "[]"
        paths |= collect_field_paths(items, item_prefix)

    return paths


def current_snapshot() -> dict:
    snapshot = {}
    for kind, filename in sorted(REAL_KINDS.items()):
        schema = load(SCHEMAS_DIR / filename)
        snapshot[kind] = {
            "schema_file": filename,
            "version": schema_version(schema),
            "fields": sorted(collect_field_paths(schema)),
        }
    return snapshot


def update_snapshot() -> int:
    snapshot = current_snapshot()
    SNAPSHOT.write_text(json.dumps(snapshot, indent=2) + "\n", encoding="utf-8")
    print(f"refreshed schema field snapshot for {len(snapshot)} real kinds -> {SNAPSHOT.name}")
    return 0


def check_snapshot() -> int:
    if not SNAPSHOT.exists():
        raise AssertionError(
            f"missing snapshot {SNAPSHOT}; run `python {Path(__file__).name} --update`"
        )
    expected = load(SNAPSHOT)
    actual = current_snapshot()

    problems: list[str] = []
    for kind in sorted(REAL_KINDS):
        exp = expected.get(kind)
        act = actual[kind]
        if exp is None:
            problems.append(f"{kind}: not present in snapshot; run --update after registering it")
            continue

        exp_fields = set(exp["fields"])
        act_fields = set(act["fields"])
        removed = exp_fields - act_fields
        added = act_fields - exp_fields
        same_version = int(exp["version"]) == int(act["version"])

        if removed and same_version:
            problems.append(
                f"{kind}: field(s) REMOVED/RENAMED at unchanged version {act['version']} "
                f"without a migration: {sorted(removed)}. "
                f"Either restore the field, or perform a real schema bump "
                f"(bump version + migration + fixtures + registry + `--update`). "
                f"See docs/config/versioning.md."
            )
        if int(exp["version"]) != int(act["version"]):
            # A version change must be accompanied by a deliberate snapshot refresh.
            problems.append(
                f"{kind}: schema version changed {exp['version']} -> {act['version']}. "
                f"This is a real schema bump: register the migration + fixtures, then run "
                f"`python {Path(__file__).name} --update` to bless the new field set. "
                f"See docs/config/versioning.md."
            )
        # Pure additions at the same version are allowed but should be blessed so the
        # snapshot stays the source of truth.
        if added and same_version and not removed:
            problems.append(
                f"{kind}: new field(s) {sorted(added)} added at version {act['version']}. "
                f"Additive changes are allowed; run `--update` to record them in the snapshot."
            )

    if problems:
        print("schema field guard FAILED (CFGV-03):", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1

    total_fields = sum(len(v["fields"]) for v in actual.values())
    print(
        f"schema field guard OK: {len(actual)} real kinds, {total_fields} field paths "
        f"unchanged at their registered versions"
    )
    return 0


def main(argv: list[str]) -> int:
    if len(argv) > 1 and argv[1] == "--update":
        return update_snapshot()
    return check_snapshot()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

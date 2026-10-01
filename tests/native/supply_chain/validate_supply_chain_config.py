from __future__ import annotations

import json
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[3]
SCHEMAS = ROOT / "config" / "schemas"
SUPPLY_CHAIN = ROOT / "config" / "supply-chain"


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def validate(path: Path, schema_name: str) -> dict:
    schema = load(SCHEMAS / schema_name)
    jsonschema.Draft202012Validator.check_schema(schema)
    doc = load(path)
    jsonschema.validate(doc, schema)
    return doc


def main() -> int:
    trust_root = validate(
        SUPPLY_CHAIN / "trust-root.test.json",
        "supply-chain-trust-root.schema.json",
    )
    assert trust_root["trusted_keys"], "trust root must list at least one key"
    key_ids = {key["key_id"] for key in trust_root["trusted_keys"]}
    assert "avrcade-test-signing-rsa-2026-06" in key_ids

    manifest = validate(
        SUPPLY_CHAIN / "manifest.test.json",
        "supply-chain-manifest.schema.json",
    )
    artifact_types = {artifact["artifact_type"] for artifact in manifest["artifacts"]}
    assert {"runtime-dll", "adapter-dll", "config-bundle"} <= artifact_types, (
        "SIGN-01 fixture manifest must cover runtime DLL, adapter DLL, and config bundle"
    )
    for artifact in manifest["artifacts"]:
        assert artifact["signatures"], (
            f"{artifact['artifact_id']} must be signed in the signed manifest"
        )
        for signature in artifact["signatures"]:
            assert signature["key_id"] in key_ids, (
                f"{artifact['artifact_id']} signature must chain to a trusted key"
            )

    validate(
        SUPPLY_CHAIN / "revocations.empty.test.json",
        "supply-chain-revocations.schema.json",
    )
    revoked = validate(
        SUPPLY_CHAIN / "revocations.revoked-runtime.test.json",
        "supply-chain-revocations.schema.json",
    )
    revoked_ids = {artifact["artifact_id"] for artifact in revoked["revoked_artifacts"]}
    assert "vrclient-runtime-fixture" in revoked_ids, (
        "SIGN-03 revoked-runtime fixture must revoke the runtime artifact"
    )

    print("validated supply-chain trust root, manifest, and revocation configs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

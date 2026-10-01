# Unified Safety Model + Artifact Signing (M5)

VRClient has TWO safety implementations that serve different trust models. M5
reconciles them into ONE conceptual model without a false runtime unification,
and adds signature verification for client-produced packs.

## Two engines, one model

| | .NET `SafetyGate` (client) | native `vr_safety` (injector) |
|---|---|---|
| Role | The runtime gate for the managed-orchestration client | The gate for native injection |
| Inputs | game config `support_policy` + a `default-rules.json` rule row | a full `SafetyEvaluationRequest` (binary-fingerprinted identity + injector preflight + detection) |
| Why not swapped | The client does NOT inject, so it has no injector preflight/identity to feed the native evaluate | — |

The native `evaluateSafetyVerdict` is request-driven and needs inputs the client
cannot produce, and the two engines use **disjoint reason-code vocabularies by
design** (M1 D9). So the .NET gate stays the client's runtime gate (M5D1), and
"one model" is delivered as an **equivalence map** + a **verdict-class parity
test**, not a runtime swap.

## Reason-code equivalence map (`SafetyReasonMap`)

| .NET reason | native reason | canonical class |
|---|---|---|
| `no_safety_rule` | `no_rule_for_game` | UnknownBlocked |
| `anti_cheat_present` | `anti_cheat_detected` | Block |
| `offline_only_ok` | `approved` | Allow |
| `private_modded_coop_requires_acknowledgement` | `acknowledgement_required` | Warn |
| `online_scope_unsafe` | `launch_mode_not_permitted` | Block |

`SafetyParityTests` proves the .NET gate's verdict CLASS matches this map for
R.E.P.O. (Warn) and demo-coop (UnknownBlocked), and that every reason the gate
can emit is covered. Parity is asserted at the verdict-class level — never as
identical reason strings (the vocabularies differ by design).

## Native rules-level cross-check CLI (`vrclient_safety_cli`)

An injection-free CLI over the real `loadSafetyRuleSet`/`SafetyRuleSet::findRule`
API. It reports only the rule-level facts both engines share — it does NOT run
the full injector evaluate.

Contract: `vrclient_safety_cli --game <path> --rules <path> --mode <launch_mode>` →
```
RULECHECK game_id=<id> rule_found=<true|false> permitted=<true|false> modes=<comma-list>
```
`permitted` = whether `--mode` is in the matched rule's `allowed_launch_modes`.

The client's `safety` verb appends `crosscheck=available:permitted=<..>` when the
CLI is present (located via `VRCLIENT_SAFETY_CLI` or a `build/…` path), else
`crosscheck=unavailable`. The cross-check NEVER changes the verdict or exit code
(M5D1). On a machine without a C++ toolchain the CLI is absent and the verb
prints `crosscheck=unavailable` — expected, non-blocking.

## Client-produced pack signing

Client-produced packs (e.g. M4 framework plugins, config bundles) are signed with
B6's `vr_supply_chain` trust-root + manifest + RSA/SHA-256 model. The client
verifies BEFORE install, fail-closed:

- A pack with NO sibling `<zip>.manifest.json` → a Thunderstore community mod
  (M1 trust-on-first-use) → `unsigned_community_mod_allowed`.
- A pack WITH a manifest → verified via `vrclient_sign_cli --verify` (or another
  `IPackVerifier`): trusted → `pack_signature_ok`; refused → `pack_signature_invalid`;
  verifier unavailable → `pack_signature_unverifiable` (fail-closed, M5D8).

`vrclient_sign_cli --verify --manifest <path> --trust-root <path> --revocations <path> --artifact-id <id> --base-dir <dir>`
→ `SIGN ok=<true|false> reason=<..> state=<..>` over `verifyArtifact`.

Signing a NEW pack is a documented procedure using a user-held private key over
B6's `canonicalArtifactSigningPayload`; the M5 test path uses the EXISTING B6
fixtures (`config/supply-chain/trust-root.test.json`, `manifest.test.json`) and a
stub verifier — no private key in git.

## Diagnosis map

| Symptom | Check | Action |
|---|---|---|
| Parity test fails (class ≠ mapped) | print both verdicts | genuine model disagreement → log + STOP; never edit `SafetyGate` or the map to force a match |
| `crosscheck=unavailable` | is the native CLI built? | expected without a C++ toolchain — non-blocking |
| signed pack refused `pack_signature_invalid` | recompute sha256 vs manifest | real mismatch (good) or manifest not regenerated after editing the pack |
| manifested pack refused `pack_signature_unverifiable` | is a verifier available? | fail-closed by design — build/point to `vrclient_sign_cli` |
| unsigned pack installs | is `PackSigning.CheckBeforeInstall` wired before `ModInstaller.Install`? | it must run first |

# VRClient Safety Policy

This document describes what the VRClient platform does, what it refuses to do, and
why. It is written plainly so the future manager application can quote or link sections
of it directly inside warning and refusal screens. The machine-readable reason codes and
user-facing message keys defined here are the stable contract the manager app (Phase 9)
will resolve into localized copy.

Scope note: this is a safety/behavior policy. The legal terms are in
[`../legal/disclaimer.md`](../legal/disclaimer.md) and the diagnostics/privacy behavior is
in [`../privacy/diagnostics.md`](../privacy/diagnostics.md).

---

## 1. What the platform is for

VRClient converts supported games for VR through a bootstrap/runtime path. Its design
focus is **single-player and offline play of supported builds**. Online, competitive,
and anti-cheat-protected play are explicitly outside the supported envelope.

The platform is conservative by construction: it only proceeds against a target it can
positively identify as a known, supported build whose policy permits the requested
action. Anything it cannot positively confirm is refused.

## 2. The core rule: default-block

**If the platform is not sure, it does not proceed.** Uncertainty is treated as a refusal,
never as permission. Concretely, every one of the following resolves to a refusal verdict:

- Unknown or ambiguous game identity.
- Unknown or unsupported build identity.
- A missing, unreadable, or invalid safety rule set.
- An unknown or unevaluated safety verdict.
- Any uncertain anti-cheat, online-mode, source, launch-mode, or runtime-integrity signal.
- Any missing, unsigned, tampered, untrusted, or revoked runtime/adapter/config artifact.

This is the **default-block** posture. A target reaches the injection/bootstrap step only
after an explicit `allow` verdict. There is no override flag, no "force" path, and no way
for a missing rule to be interpreted as a quiet yes. Refusal is a **normal, expected
user-facing state** — not an error or a crash — and the manager app should present it as
such.

## 3. Verdict model

Every safety decision produces one of four verdicts. Each verdict carries a stable
machine reason code (for diagnostics and logic) and a user-facing message key (for the
manager app to render localized text).

| Verdict | Meaning | Effect on injection/bootstrap |
|---|---|---|
| `allow` | The target is a known supported build and its policy permits the requested action. | Proceed to the bootstrap/runtime step. |
| `warn` | Injectable only after the user explicitly acknowledges a stated risk. | Blocked until acknowledged. The manager app (Phase 9) collects acknowledgement; command-line smoke tools may use a test-only `--acknowledge-risk` flag. |
| `block` | Refused because a known-risky condition was detected. | Hard stop. No process touch. |
| `unknown-blocked` | Refused because a required signal was missing or uncertain. This is the default-block fallback. | Hard stop. No process touch. |

Notes:
- `warn` never auto-proceeds. Absent an explicit acknowledgement, a `warn` is treated as a
  refusal at the injection boundary.
- `block` and `unknown-blocked` both prevent any process touch. They differ only in *why*:
  `block` means "we detected something risky"; `unknown-blocked` means "we could not get
  enough trustworthy signal to allow."

## 4. Modding posture: a favorable signal, never an authority

Games with official modding toolkits, documented mod APIs, workshop support, or common
mod-application clients are better adapter candidates — especially for offline and
single-player use. The platform records a target's **modding posture** as one of:

- `official` — first-party modding support exists.
- `community-supported` — established community modding, no first-party support.
- `unsupported` — no meaningful modding support.
- `unknown` — modding posture has not been established.
- `conflicting-with-integrity-policy` — modding here is known to conflict with the title's
  integrity/anti-cheat policy.

**Modding posture only ever lowers review friction. It can never override a safety check.**
A favorable posture (`official`/`community-supported`) does **not** grant permission to:

- inject into an unknown or unsupported build,
- inject into a target with risky or unknown anti-cheat state,
- inject into an online, public-matchmaking, or unknown-online session,
- bypass the configured allowlisted source or launch-mode rules, or
- proceed past any runtime-integrity (path/architecture/hash) or supply-chain trust check.

Even where a publisher officially supports mods, that support does **not** automatically
permit online or anti-cheat-sensitive injection. Identity, anti-cheat, online-mode, source,
and launch-mode checks are evaluated independently and must each pass on their own merits.
`conflicting-with-integrity-policy` is a negative signal and is never treated as favorable.

## 5. Anti-cheat: detect and refuse, never bypass

The platform's relationship with anti-cheat is **detection only**, and detection exists so
the platform can **refuse**, not engage.

The platform **does not, and will not**:

- disable, unload, hide from, patch, tamper with, spoof, or circumvent any anti-cheat
  product, service, driver, process, or module;
- interact with anti-cheat components beyond passively recognizing that they are present or
  configured; or
- attempt any stealth, evasion, or injection technique intended to avoid anti-cheat
  detection.

What the platform **does**: it reads a configured, data-only list of known anti-cheat
indicators (product names, module/service signatures), known online-only mode flags, and
storefront/DRM signals to decide whether a target is too risky to touch. These indicator
lists live in configuration (`config/safety/default-rules.json`), not in compiled code, so
they can be updated and delivered independently of a client release. Detection is
conservative: an uncertain anti-cheat signal yields `unknown-blocked`, never a guessed
`allow`. A recognized risky anti-cheat condition yields `block`.

## 6. Launch-only compatibility for anti-cheat targets

Some anti-cheat-protected games may still be represented by the manager as
**launch-only** entries. Launch-only means VRClient can start or orchestrate the
game without using the injector, without loading the runtime into the game process,
and without touching memory.

The launch-only path is available only when configuration explicitly records a
legitimate non-injection path. If anti-cheat is detected and such a path exists,
the manager may show the target as "launch-only, injection disabled." If anti-cheat
is detected and no legitimate non-injection path exists, the target remains blocked.
If anti-cheat state is uncertain, the target remains blocked.

This does not weaken the injector safety gate: injector-capable routing is possible
only when anti-cheat is positively clear, and the normal Phase 7 safety verdict still
has to allow or warn before any injector flow may proceed.

## 7. Where the safety gate sits

The safety verdict is evaluated **between version detection and any injector launch or
attach**, as a precondition. The injector requires an `allow` verdict before it touches the
target process — this applies to both the launch-time path and the attach-to-a-running-process
path. A `block` or `unknown-blocked` verdict stops the flow before any process touch occurs.
The safety reason is recorded in diagnostics so refusals are auditable.

## 8. Diagnostics and telemetry posture (summary)

- Diagnostics are **local and user-owned**. Export is **opt-in** and user-triggered.
- There is **no automatic telemetry upload**. Nothing leaves the user's machine on its own.
- Exports are redacted (user paths, secrets/tokens) before they are written.

See [`../privacy/diagnostics.md`](../privacy/diagnostics.md) for the full behavior.

---

## 9. Reason code → human explanation mapping

The table below maps each stable machine reason code to a user-facing explanation. The
**message key** is the stable lookup token the manager app (Phase 9) resolves to localized
copy; the explanation column is the developer-facing reference text. Machine codes and
message keys are stable across releases — they are added to, not renamed or removed.

> Source-of-truth note: the **verdict-level** and **detection-input** rows below are the
> exact `reason_code` / `message_key` strings emitted by the Phase 7 core engine
> (`src/native/safety/safety_verdict.cpp`, `evaluateSafetyVerdict`). They are reconciled
> to the engine, which is authoritative. The **preflight** rows reuse the existing Phase 3
> `safety_preflight` reason codes verbatim; when the preflight spine refuses, the engine
> wraps the preflight's `reason_code` unchanged and emits a `safety.block.preflight.<code>`
> message key for it (see the preflight-propagation row).

### Verdict-level and core-engine codes (four-state model)

These are emitted directly by `evaluateSafetyVerdict`. The engine does not emit generic
`safety_block` / `unknown_blocked` codes; every refusal carries the specific code for the
condition that triggered it (listed across this section).

| Reason code | Verdict | Message key | User-facing explanation |
|---|---|---|---|
| `approved` | allow | `safety.allow.approved` | This target passed all safety checks and can be converted. |
| `acknowledgement_required` | warn | `safety.warn.acknowledgement_required` | This target passed the core checks but carries residual risk. You must explicitly acknowledge it before continuing. |
| `missing_rule_set` | unknown-blocked | `safety.unknown.missing_rule_set` | The safety rules could not be loaded, so the platform will not proceed. |
| `unknown_game_or_build` | unknown-blocked | `safety.unknown.identity` | The platform could not confirm this is a known, supported build, so it will not proceed. |
| `unsupported_identity` | block | `safety.block.unsupported_identity` | This build is explicitly not supported. |
| `no_rule_for_game` | unknown-blocked | `safety.unknown.no_rule_for_game` | No safety rule entry exists for this game, so the platform will not proceed. |
| `launch_mode_not_permitted` | block | `safety.block.launch_mode_not_permitted` | The requested launch mode is not permitted for this game. |
| `<preflight reason code>` (propagated) | block | `safety.block.preflight.<preflight reason code>` | The Phase 3 preflight spine refused; the wrapped preflight reason (see the Preflight-spine sections below) is propagated unchanged with a `safety.block.preflight.` message-key prefix. |
| `modding_conflicts_with_integrity_policy` | block | `safety.block.modding_conflicts_with_integrity_policy` | The declared modding posture conflicts with the title's integrity policy. |

### Detection-input codes (DEFENSIVE recognition; T02)

The engine recognizes configured, data-only indicators to **refuse** risky targets. It
never disables, hides from, patches, or bypasses anti-cheat. A recognized risky indicator
is a hard `block`; an uncertain signal is `unknown-blocked` unless the rule explicitly
permits warn-on-uncertain.

| Reason code | Verdict | Message key | User-facing explanation |
|---|---|---|---|
| `anti_cheat_detected` | block | `safety.block.anti_cheat_detected` | A known anti-cheat indicator was observed. The platform refuses to proceed and does not interact with anti-cheat. |
| `online_only_mode_detected` | block | `safety.block.online_only_mode_detected` | A known online-only mode flag was observed. The platform refuses to proceed. |
| `storefront_drm_detected` | block | `safety.block.storefront_drm_detected` | A known storefront DRM indicator was observed. The platform refuses to proceed. |
| `uncertain_detection_signal` | unknown-blocked | `safety.unknown.uncertain_detection_signal` | A detection signal was uncertain (for example, module enumeration was incomplete), so the platform refuses by default. |

### Supply-chain integrity codes (B6)

The signing verifier checks artifact SHA-256 first, then verifies the signature
against the configured trust root, then applies the revocation list. These checks
are data-driven and happen before any launch or injection path can proceed.

| Reason code | Verdict | Message key | User-facing explanation |
|---|---|---|---|
| `artifact_trust_not_evaluated` | unknown-blocked | `safety.unknown.supply_chain.not_evaluated` | Artifact trust was not evaluated, so the platform refuses by default. |
| `artifact_unsigned` | block | `safety.block.supply_chain.unsigned_artifact` | A required runtime, adapter, or config artifact is unsigned. |
| `artifact_hash_mismatch` | block | `safety.block.supply_chain.hash_mismatch` | Artifact bytes do not match the signed manifest hash. |
| `artifact_signature_invalid` | block | `safety.block.supply_chain.signature_invalid` | The artifact signature did not verify against the trust root. |
| `artifact_signing_key_untrusted` | block | `safety.block.supply_chain.untrusted_key` | The artifact signature does not chain to a trusted signing key. |
| `artifact_revoked` | block | `safety.block.supply_chain.revoked_artifact` | The artifact hash appears on the active revocation list. |
| `artifact_signing_key_revoked` | block | `safety.block.supply_chain.revoked_artifact` | The artifact signing key appears on the active revocation list. |
| `artifact_unsupported_signature_algorithm` | block | `safety.block.supply_chain.unsupported_algorithm` | The artifact uses a signature algorithm this client does not support. |
| `artifact_unreadable` | block | `safety.block.supply_chain.unreadable_artifact` | The artifact could not be read for trust verification. |
| `artifact_invalid_config` | block | `safety.block.supply_chain.invalid_config` | Artifact signing metadata is invalid or incomplete. |

### Anti-cheat launch-routing codes (B8)

The anti-cheat launch router consumes the same detection-input catalog as the Phase 7
verdict engine plus schema-guarded compatibility data from
`config/safety/anti-cheat-compatibility.json`. It never launches or touches a process;
it only returns the route the manager may present.

| Reason code | Route | Message key | User-facing explanation |
|---|---|---|---|
| `anti_cheat_launch_only_injection_disabled` | launch-only | `safety.launch.anti_cheat.launch_only` | Anti-cheat was detected, a legitimate non-injection path is configured, and injection is disabled. |
| `anti_cheat_no_sanctioned_launch_path` | blocked | `safety.block.anti_cheat.no_sanctioned_launch_path` | Anti-cheat was detected and no legitimate launch-only path is configured. |
| `anti_cheat_signal_uncertain` | blocked | `safety.unknown.anti_cheat.signal_uncertain` | Anti-cheat state was uncertain, so the platform refuses by default. |
| `anti_cheat_compatibility_not_loaded` | blocked | `safety.unknown.anti_cheat.compatibility_not_loaded` | Anti-cheat compatibility data could not be loaded, so the platform refuses by default. |
| `injector_capable_no_anti_cheat_detected` | injector-capable | `safety.launch.injector_capable` | Anti-cheat is positively clear; this only makes injector routing eligible for the normal safety verdict. |
| `launch_only_selected` | launch-only | `safety.launch.launch_only` | A launch-only variant was selected; the injector is not used. |
| `no_compatible_launch_variant` | blocked | `safety.block.launch.no_compatible_variant` | No compatible launch route is configured for this game/build. |

### Preflight-spine codes (Phase 3 `safety_preflight`)

The sections below enumerate the Phase 3 `safety_preflight` reason codes that form the
authoritative precondition spine (identity, source, anti-cheat, online, runtime integrity).
The keys in these sections are the **preflight layer's own** message keys and are stable.
When the verdict engine runs the spine and it refuses, the engine returns a `block` verdict
whose `reason_code` is the preflight code **verbatim**, additionally exposing it as
`safety.block.preflight.<code>` (see the propagated-preflight row above); the per-section
keys here remain the canonical, human-facing keys for each underlying condition.

#### Identity codes

| Reason code | Verdict | Message key | User-facing explanation |
|---|---|---|---|
| `unknown_identity` | unknown-blocked | `safety.identity.unknown` | The platform could not identify this game, so it will not proceed. |
| `ambiguous_identity` | unknown-blocked | `safety.identity.ambiguous` | The evidence matched more than one build, so identity is uncertain. |
| `unsupported_identity` | block | `safety.identity.unsupported` | This build is explicitly not supported. |
| `unknown_build` | unknown-blocked | `safety.identity.unknown_build` | This game's build is not recognized. |
| `ambiguous_build` | unknown-blocked | `safety.identity.ambiguous_build` | Fingerprint evidence matched multiple builds. |
| `untrusted_identity_evidence` | unknown-blocked | `safety.identity.untrusted_evidence` | The identity evidence came from an untrusted path. |
| `executable_name_mismatch` | block | `safety.identity.executable_mismatch` | The selected executable does not match the expected game. |
| `identity_not_supported` | block | `safety.identity.not_supported` | A conversion was requested without a confirmed supported identity. |

#### Source and launch-mode codes

| Reason code | Verdict | Message key | User-facing explanation |
|---|---|---|---|
| `unsafe_target_source` | block | `safety.source.not_allowlisted` | This game was launched/discovered through a source that is not allowed for it. |

#### Anti-cheat codes (preflight-spine; see also the Detection-input codes above)

| Reason code | Verdict | Message key | User-facing explanation |
|---|---|---|---|
| `unknown_anti_cheat_risk` | unknown-blocked | `safety.anticheat.unknown` | Anti-cheat risk is unknown, so the platform will not proceed. |
| `unsafe_anti_cheat_risk` | block | `safety.anticheat.risky` | A known anti-cheat risk was detected. The platform refuses to proceed. |

#### Online-mode codes

| Reason code | Verdict | Message key | User-facing explanation |
|---|---|---|---|
| `unknown_online_risk` | unknown-blocked | `safety.online.unknown` | Online-mode risk is unknown, so the platform will not proceed. |
| `online_mode_risk` | block | `safety.online.known_online` | This target is in a known online mode and was refused. |
| `public_matchmaking_risk` | block | `safety.online.public_matchmaking` | Public matchmaking or an unknown public online state was detected. |
| `offline_policy_multiplayer_scope` | block | `safety.online.offline_policy_scope` | An offline-only target cannot be approved for a multiplayer session. |
| `private_multiplayer_not_confirmed` | unknown-blocked | `safety.online.private_not_confirmed` | A private/modded session was required but not confirmed. |
| `multiplayer_mod_compatibility_unconfirmed` | unknown-blocked | `safety.online.mod_compat_unconfirmed` | Mod compatibility for a private session was not explicitly confirmed. |

#### Runtime-integrity codes

| Reason code | Verdict | Message key | User-facing explanation |
|---|---|---|---|
| `runtime_already_loaded` | block | `safety.runtime.already_loaded` | The runtime is already present in the target; the platform stops to avoid a double load. |
| `architecture_mismatch` | block | `safety.runtime.arch_mismatch` | The target's architecture does not match the runtime policy. |
| `runtime_architecture_mismatch` | block | `safety.runtime.runtime_arch_mismatch` | The runtime binary's architecture does not match the target. |
| `runtime_binary_missing` | unknown-blocked | `safety.runtime.binary_missing` | The runtime binary is missing or unavailable. |
| `runtime_hash_missing` | unknown-blocked | `safety.runtime.hash_missing` | The runtime binary's integrity hash is missing or malformed. |
| `runtime_hash_mismatch` | block | `safety.runtime.hash_mismatch` | The runtime binary's hash does not match the expected value. |

> Codes not enumerated above (for example, future detection-input codes from T02) inherit
> the default-block rule: any unrecognized or unevaluated code is treated as
> `unknown-blocked`.

---

## 10. Summary of guarantees

- Unknown identity, unknown build, missing rules, or unknown verdict → refuse.
- Detection is used to refuse risky targets; the platform never bypasses anti-cheat.
- Anti-cheat targets can only use launch-only routes when a legitimate non-injection
  path is configured; injection remains disabled.
- Unsigned, tampered, untrusted, or revoked artifacts are blocked before launch.
- Official modding support is favorable but cannot override identity, anti-cheat,
  online-mode, source, launch-mode, runtime-integrity, or supply-chain trust checks.
- The safety verdict runs before any injector action; a refusal stops the flow before any
  process touch and is recorded in diagnostics.
- Diagnostics are local, user-owned, opt-in, and never uploaded automatically.

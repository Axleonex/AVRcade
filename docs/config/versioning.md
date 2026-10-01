# Config Schema Versioning & Migration (Bolt-on Phase B5)

**Requirements:** CFGV-01 (version negotiation), CFGV-02 (migration framework +
fixture corpus), CFGV-03 (no-rename/no-remove-without-migration policy),
CFGV-04 (accepted version range in diagnostics).

This document defines how a VRClient build decides whether it can consume a config
document, how it upgrades an older one, and the policy that keeps schemas
forward/backward compatible. It is the additive versioning layer that sits *around*
the existing config loaders and schemas — it changes none of them.

---

## 1. What B5 governs (and what it does not)

B5 governs the **integer `version` field** carried by every config document in
`config/schemas/*.schema.json`. Every real production schema is currently
**version 1**.

B5 does **not** govern:

- `safety-rules.rule_set_version` — that is a *content/data* version string
  (SAFE-03), bumped when the rule data changes. It is independent of the schema
  `version` integer and is out of B5's negotiation scope.
- The internal structural validation each existing loader already performs. B5
  decides *whether to load/migrate/refuse*; the loader still validates the body.

B5 is **additive and dependency-free**: a new static library `vr_config_versioning`
(`src/native/config/`) with its own self-contained JSON DOM, mirroring the repo's
existing hand-rolled recursive-descent readers
(`src/native/tooling/hookdisc/hook_surface_doc.cpp`,
`src/native/safety/safety_verdict.cpp`). No external JSON library is introduced.

---

## 2. The version registry

Each config *kind* is registered with an accepted range `[min .. current]`:

| Config kind          | min | current | Migration chain |
|----------------------|-----|---------|-----------------|
| `runtime-profile`    | 1   | 1       | none yet |
| `diagnostics-profile`| 1   | 1       | none yet |
| `game-fingerprint`   | 1   | 1       | none yet |
| `game-profile`       | 1   | 1       | none yet |
| `hook-surface`       | 1   | 1       | none yet |
| `safety-rules`       | 1   | 1       | none yet |
| `anti-cheat-compatibility` | 1 | 1 | none yet |
| `supply-chain-trust-root` | 1 | 1 | none yet |
| `supply-chain-manifest` | 1 | 1 | none yet |
| `supply-chain-revocations` | 1 | 1 | none yet |
| `demo-config` (SYNTHETIC, test-only) | 1 | 2 | `v1 -> v2` |

`demo-config` is **not a production schema**. It exists only to prove the migration
chain machinery end to end before the first real schema bump. Its v1/v2 schemas and
fixtures live under `tests/native/config/fixtures/`, deliberately outside
`config/schemas/` so they can never be mistaken for a real kind.

The registry is seeded in
`ConfigVersionRegistry::makeDefault()` (`src/native/config/config_versioning.cpp`).

---

## 3. The negotiation rule (CFGV-01)

Given a config kind and a document, the registry returns a deterministic
`ConfigVersionDecision { decision, reason_code, from_version, to_version }`:

Let `N` be the document's declared `version` and `[min..current]` the registered
range for the kind.

| Condition                              | Decision | Reason code |
|----------------------------------------|----------|-------------|
| Kind not registered                    | REFUSE   | `refuse_unknown_kind` |
| Document does not parse as JSON         | REFUSE   | `refuse_unparseable_config` |
| `version` missing or not an integer     | REFUSE   | `refuse_invalid_version` |
| `N == current`                         | LOAD     | `load_current_version` |
| `min <= N < current`                   | MIGRATE  | `migrate_in_range` |
| `N > current` (from a newer client)     | REFUSE   | `refuse_too_new` |
| `N < min` (unsupported old)            | REFUSE   | `refuse_unsupported_old` |

**No silent misparse.** A document that is unparseable, has a missing version, or
has a non-integer `version` (e.g. `1.5`) yields a clear REFUSE with a reason code —
never a partial or guessed parse. This mirrors the strict "reject -> safe default"
discipline already used by `safety_verdict.cpp`.

The negotiator is the *single authority* on the decision. It deliberately does NOT
call the existing loaders; it decides LOAD/MIGRATE/REFUSE first, and a caller only
hands a document to its loader after a LOAD (or after a successful MIGRATE). This is
why B5 wraps, and never edits, existing loaders — notably `loadHookSurfaceDoc`,
which today hard-refuses `version != 1`; once a real v2 exists, the negotiator owns
the range decision so a legitimately-migrated v2 is not vetoed by that older check.

Entry points (`src/native/config/config_versioning.h`):

- `negotiateText(kind, text)` — parses then decides (the no-silent-misparse path).
- `negotiate(kind, doc)` — decides on an already-parsed DOM.
- `negotiateAndMigrate(kind, text)` — decides and, on MIGRATE, runs the chain.

---

## 4. The migration framework (CFGV-02)

A migration is an ordered chain of `MigrationStep`s, one per version increment.
Each step:

1. `apply(JsonValue&)` mutates the DOM to the next version (add/rename/remove field,
   set a default, bump `version`).
2. `validate(const JsonValue&)` checks the **output** doc is structurally valid for
   the target version.

`ConfigVersionRegistry::migrate()` walks `N -> N+1 -> ... -> current`, locating the
ordered step for each increment and refusing (`refuse_migration_failed`) if a step
is missing or its apply/validate fails. The chain is deterministic and validated at
every step.

### The proven synthetic migration: `demo-config v1 -> v2`

A real-shaped transform:

- **renames** `display_name` -> `title` (value and position preserved), and
- **adds** a defaulted `comfort_mode = "standard"`, and
- bumps `version` 1 -> 2.

Fixture corpus (`tests/native/config/fixtures/`):

- `demo-config.v1.basic.input.json` + `.expected-v2.json`
- `demo-config.v1.repo.input.json` + `.expected-v2.json`
- `demo-config.v2.current.json` (LOAD case)
- `demo-config.v3.too-new.json` (REFUSE too_new)
- `real-kind.v1.runtime-profile.json` (real kind LOAD)
- `real-kind.v2.too-new.json` (real kind REFUSE too_new)
- `refuse.missing-version.json`, `refuse.unparseable.txt`

The C++ test (`tests/native/config/config_versioning_tests.cpp`) loads each v1 doc,
migrates it, and asserts equality with the expected v2 doc **and** v2-schema
validity. The Python validator
(`tests/native/config/validate_config_versioning.py`) re-derives the same transform
independently and validates against the actual v1/v2 JSON schemas, so the two sides
must agree.

---

## 5. No-rename / no-remove-without-migration policy (CFGV-03)

> **A field is never renamed or removed from a real schema without a migration and a
> fixture.** Additive (new optional field) changes are allowed at the same version;
> any field disappearing is a breaking change that requires a version bump.

This is enforced mechanically by the guard
`tests/native/config/schema_field_guard.py`, registered as the CTest
`config_field_guard` and auto-run by the CI script. The guard:

1. Reads every real schema, derives its registered `version`, and computes the full
   recursive set of property paths.
2. Compares against the committed snapshot
   `tests/native/config/field-snapshots.json`.
3. **FAILS** if any field path is removed/renamed at the *same* registered version,
   or if a version changed without a deliberate snapshot refresh, telling the author
   exactly what to do.

This protects the schemas that later phases (7/8/8.5) add: a careless rename in a
PR fails CI instead of silently breaking older config docs.

---

## 6. Accepted version range in diagnostics (CFGV-04)

`ConfigVersionRegistry::supportMatrix()` returns the `[min..current]` range per
kind. `emitSupportMatrix(AsyncLogger*)` emits one structured
`config_version_support` event per kind through the existing Phase 2 `AsyncLogger`
(fields: `config_kind`, `min_version`, `current_version`), following the
`game_fingerprint.cpp::logDetection` precedent. It is nullptr-safe (a no-op when no
logger is supplied) and adds no new logger method or severity.

The C++ test reads the emitted records back via `AsyncLogger::recentRecords` and
asserts the demo-config `[1..2]` range is reported.

### Gating Phase 8 bundle loading (forward intent)

CFGV-04 has two halves: the accepted range is *visible in diagnostics* (proven
above) **and** it *gates Phase 8 bundle loading*. The gate is not a separate
mechanism — it is the same negotiation authority. When Phase 8's bundle loader
receives a delivered config bundle, it calls `negotiateText` /
`negotiateAndMigrate` on each document before accepting it: a **REFUSE** decision
rejects the bundle (the accepted-version matrix surfaced here is exactly the range
that decision enforces), a **MIGRATE** decision upgrades the document through the
chain and then loads it, and a **LOAD** decision accepts it as-is. So the
diagnostics matrix and the load/migrate/refuse gate are one and the same authority:
what the matrix advertises as accepted is precisely what the negotiator admits.
Phase 8 is not built yet, so the wired-in gate (a bundle loader that calls these
entry points) is correctly out of B5's scope; B5 ships the seam and the authority
Phase 8 will consume, and this is the documented intent rather than a silently
absent half of the requirement.

---

## 7. Procedure for the FIRST real schema bump

When a real production schema genuinely needs a breaking change (a field renamed or
removed, or a new *required* field added), follow this procedure. Do **not** edit a
`const: 1` to `const: 2` in isolation — that is exactly the breaking change this
framework forbids without the full set of accompanying changes.

Worked example: bumping `game-profile` from v1 to v2.

1. **Bump the schema.** Edit `config/schemas/game-profile.schema.json`: change the
   `version` const/minimum to allow 2 (e.g. `"enum": [1, 2]` during the overlap, or
   `"const": 2` once v1 docs are gone), and make the field change (rename/add/remove).
2. **Add a migration step.** In `src/native/config/config_versioning.cpp`, write a
   `makeGameProfileV1ToV2()` `MigrationStep` (apply + validate) using the DOM
   helpers (`renameMember`, `setDefault`, `removeMember`, `setMember`). The
   `validate` callback must accept the v2 shape and reject the v1 shape.
3. **Register it.** In `makeDefault()`, change the `game-profile` spec to
   `current_version = 2` and push the new step into `migrations`. Leave `min_version`
   at 1 while v1 docs must still upgrade; raise it only when v1 is fully retired.
4. **Add fixtures.** Under `tests/native/config/fixtures/`, add a real `game-profile`
   v1 doc and its expected v2 doc, and extend the C++ migration test + the Python
   validator to cover the new pair.
5. **Update the field-set snapshot.** Run
   `python tests/native/config/schema_field_guard.py --update` to bless the new field
   set at the new version. Without this, the guard correctly fails the build.
6. **Update this table.** Bump the `game-profile` row in section 2.
7. **Build green.** `scripts/ci/build-and-test.ps1 -BuildDir build/ci` must pass end
   to end — the migration test, the schema validator, the field guard, and the
   diagnostics range test all gate the change.

---

## 8. Proven vs. pending (honesty note)

- **PROVEN** by B5 fixtures and tests: the generic negotiation
  (LOAD/MIGRATE/REFUSE with reason codes), the migration-chain machinery (synthetic
  `demo-config` v1->v2), the no-rename/remove guard, and the CFGV-04 diagnostics
  emit. These are exercised by the synthetic test-only kind and the real-kind
  negotiation fixtures.
- **PENDING** until the first real schema bump: no production kind moves off v1 in
  B5. All real kinds register at `current = 1`, matching their on-disk schemas.
  The framework is ready; the first real bump is the documented procedure in
  section 7, not something performed here.
- **PENDING** until Phase 8 exists: the *wired-in* CFGV-04 bundle-loading gate. The
  diagnostics half of CFGV-04 (accepted range visible in diagnostics) is PROVEN; the
  gating half is the negotiation seam Phase 8's bundle loader will call
  (section 6, "Gating Phase 8 bundle loading"). B5 ships that authority; the loader
  that consumes it is built in Phase 8. This is honestly deferred, not omitted.

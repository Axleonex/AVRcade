# Phase 3 — Bootstrap Injector: Troubleshooting & Bug-Checking

Covers `src/native/injector/` (`process/` discovery, `safety/` preflight,
`bootstrap/` smoke) and the closely-related `src/native/safety/`,
`src/native/tooling/hookdisc/`, `src/native/config/`, `src/native/versioning/`.

Read the [cross-cutting index](../TROUBLESHOOTING.md) first.

## The defining constraint: the injection primitive is user-supplied

The actual DLL-injection core — `OpenProcess` → `VirtualAllocEx` →
`WriteProcessMemory` → `CreateRemoteThread(LoadLibraryW)` (~30 lines) — is
**blocked from AI code generation by the assistant usage-policy filter**. Two
separate workflows were blocked at this step (real-injector build; vendoring a
third-party MIT injector). It is therefore **supplied by the user**, and
`INJ-01` / `INJ-06` stay open in `.planning/REQUIREMENTS.md`.

- **Do not** loop trying to generate, vendor, or "work around" the primitive —
  you'll just re-trigger the filter. Scaffold *around* it.
- Everything else in Phase 3 is AI-buildable and **is** built: process
  discovery, the safety preflight + refusal matrix, the hook-discovery
  (observe-only) tooling, config/versioning, fingerprinting.
- `bootstrap/bootstrap_smoke` is a smoke around the boundary; it is not real
  injection until the user's primitive is wired in.

## Real bugs / lessons here

### Integer narrowing in the version parser (config_versioning)
`readVersion` narrowed an `int64` to `int`, so a too-new document **truncated
into an in-range value and loaded silently** — a security-relevant parser bug.
Fixed with a full-width range check.
- **Check:** audit every version/size/count parser for `int64 → int` (or any)
  narrowing; reject out-of-range explicitly. Tests: `vr_config_versioning_unit_tests`,
  `config_field_guard`, `config_versioning_schema`.

### Safety gate must REFUSE, not just allow
The value of the safety layer is the refusal path. `injector_refusal_matrix` and
`safety_rules_schema` exist to prove disallowed targets are **rejected**.
- **Check:** when you touch the safety preflight, test both the allow path and
  the refuse path. A safety test that only checks the happy path proves nothing.

### Hook-discovery is observation-only
`tooling/hookdisc/` (`observe`, `inspection`, `safety_gate`, `byte_patch_detour`,
`hook_surface_doc`) **documents and observes** hook surfaces; it is not the
runtime patching mechanism. Keep it side-effect-free; `hookdisc_static_checks`
and the schema tests guard the surface docs.

## Open-source / no-authorization product law

No per-user auth IDs, no license keys, no required user authorization (max = a
Gmail). Do **not** add an authorization/verification gate to unlock injection or
adapters. Engine-modding licenses (BepInEx LGPL-2.1, HarmonyX MIT, MelonLoader
Apache-2.0, UE4SS MIT; UEVR is custom/NOASSERTION — not open) are a modder-side
concern, not a user gate. See `docs/adapters/engine-hooking-strategy.md`.

## Verification

```powershell
# OFF gate runs: vr_injector_unit_tests, vr_safety_unit_tests, injector_static_checks,
# injector_refusal_matrix, safety_rules_schema, vr_discovery_sources_tests,
# vr_hookdisc_unit_tests, hook_surface_schema(_repo), hookdisc_static_checks,
# vrclient_hookdisc_obs_capture, vr_versioning_unit_tests, game_fingerprint_schema,
# repo_fingerprint_schema, vr_config_versioning_unit_tests, config_versioning_(schema|static_checks),
# config_field_guard
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\ci\build-and-test.ps1 -BuildDir build\ci
ctest --test-dir build\ci -R "injector|safety|hookdisc|versioning|config" --output-on-failure
```

## Bug-checking checklist

- [ ] Did not attempt to generate/vendor the injection primitive (policy-blocked;
      user-supplied). INJ-01/INJ-06 remain honestly open.
- [ ] Every parser range-checks full width; no integer narrowing.
- [ ] Safety changes test BOTH the allow and the refuse path.
- [ ] hookdisc stays observation-only (no live patching side effects).
- [ ] No authorization/license gate introduced for users.
- [ ] Tests use throwing `expect()`, not `assert()`.

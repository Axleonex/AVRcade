# Phase 2 — Diagnostics & Operations: Troubleshooting & Bug-Checking

Covers `src/native/diagnostics/`: `logging/` (diagnostic_logger), `export/`
(diagnostics_exporter), `overlay/` (diagnostics_overlay), `crash/`
(crash_capture), plus `diagnostics_system` and `diagnostics_profile`.

Read the [cross-cutting index](../TROUBLESHOOTING.md) first.

## Real bugs / lessons here

### Honesty of diagnostics evidence (DIAG-02/06, commit `bc0f353`)
DIAG items were closed by **capturing the runtime-binary hash** and
cross-checking it (`vr_runtime_hash_tests`, `runtime_binary_hash_crosscheck`) —
i.e. proving the evidence refers to the binary actually built, not a stale one.
- **Lesson:** a diagnostics/export artifact is only trustworthy if it's tied to
  the exact binary/run that produced it. When adding an exporter or evidence
  field, include an identity anchor (hash / commit / schema version) and a test
  that fails if the anchor is missing or mismatched.

### `assert()` → NDEBUG
Applies here too — any diagnostics test asserting via `assert()` is vacuous
under Release. Use throwing `expect()`. See cross-cutting trap #1.

### Overlay reconstruction (DIAG-03) is not the real overlay
The headset smoke harness emits a **host-reconstructed overlay-equivalent
snapshot**; the real in-headset overlay is only verified on a Quest. Don't tick
DIAG-03 from the reconstructed snapshot alone.

## Hot-path rule specific to diagnostics

**No synchronous logging on the frame/tick hot path.** Diagnostics logging must
be buffered/deferred; `hot_path_audit` will flag a synchronous log call inside a
hot-path region. The runtime adds identity/swapchain log lines **outside** the
hot-path markers for exactly this reason — keep new log calls out of the marked
regions.

## Privacy / redaction

Logs and exports must not leak PII or game internals beyond policy. See
`docs/privacy/diagnostics.md`. When adding a logged field, ask whether it could
contain a path, username, or game secret, and redact at the source.

## Schema & pipeline discipline

Diagnostics has both unit tests and **schema/static validators** (Python). A
field added to a profile/export without updating its schema will fail the
validator, not the unit test — run the full validator suite.

## Verification

```powershell
# OFF gate runs: vr_diagnostics_unit_tests, vr_export_run_tests, vr_crash_capture_artifact,
# vr_diag_session_log_tests, diagnostics_profile_schema, diagnostics_static_checks,
# diagnostics_single_pipeline
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\ci\build-and-test.ps1 -BuildDir build\ci
ctest --test-dir build\ci -R "diagnostics|crash|export|session_log" --output-on-failure
```

## Bug-checking checklist

- [ ] Every evidence/export artifact carries an identity anchor (hash/commit/
      schema version) and a test that fails on mismatch.
- [ ] No synchronous logging added inside hot-path markers.
- [ ] New logged fields reviewed for PII/secret leakage and redacted.
- [ ] Schema updated alongside any profile/export field change; validators green.
- [ ] Tests use throwing `expect()`, not `assert()`.
- [ ] DIAG boxes ticked only with real headset evidence, not reconstructions.

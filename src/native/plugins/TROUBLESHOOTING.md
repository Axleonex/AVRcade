# Phase 4 — Plugin System / Adapter SDK: Troubleshooting & Bug-Checking

Covers `src/native/plugins/`: `sdk/` (igame_adapter, adapter_context,
adapter_metadata, shared_services), `host/` (plugin_host, plugin_library),
`validation/` (adapter_validator). The SDK boundary is documented in
`docs/sdk/adapter-authoring.md`.

Read the [cross-cutting index](../TROUBLESHOOTING.md) first.

## Real bugs / lessons here

### `assert()` → NDEBUG
`lifecycle_full_tests.cpp` (CTest `vr_lifecycle_full_tests`) exercises the full
adapter lifecycle; like the rest of the tree it must use throwing `expect()`,
not `assert()` (vacuous under Release). See cross-cutting trap #1.

### ABI must not shrink or break across versions
Adapter structs are **versioned with `size` fields**; the host reads them
defensively. Mirror the runtime enum lesson (Phase 1): when evolving the ABI,
**add** fields/values, keep reserved ones, never renumber or remove. Rules
adapters must obey (enforced socially + by `plugin_validation_matrix`):
- No exceptions thrown across the ABI.
- No STL object ownership transferred across the ABI.
- No allocation through host memory unless the service table allows it.
- No runtime implementation headers exposed.

### Plugin loading is a security surface
The host resolves plugins from **explicit local paths only**: rejects path
traversal and network paths, resolves the four required exports
(`vrclient_get_adapter_abi`, `vrclient_get_adapter_metadata`,
`vrclient_create_adapter`, `vrclient_destroy_adapter`) **before** init, and on
Windows uses DLL search flags scoped to the plugin directory + default safe
locations.
- **Check:** any change to path resolution must keep rejecting `..`, UNC/network
  paths, and unexpected search dirs. `plugin_validation_matrix` is where the
  refuse-paths live — extend it, don't bypass it.

### Identity gating
Adapter metadata's `game_id` / `build_id` must use the **Phase 3 fingerprint
namespace**; the host rejects a mismatched adapter **before** init. Don't load
first and validate later.

### Host isolation is a hard guarantee
Adapter code is **never loaded into the manager process**. Recoverable adapter
failures quarantine the adapter + record diagnostics; unrecoverable target
crashes should carry adapter identity in crash artifacts. Don't add a code path
that runs adapter code in-process for convenience.

## Hot-path rules for adapters

`tick` inherits the runtime render-loop rules: no blocking I/O, no synchronous
logging, no avoidable allocation, stay under the callback timing budget. The
host records callback duration + overruns; repeated recoverable overruns
**disable the adapter**. Hot reload is refused while callbacks/hooks/owned
threads/teardown are active — fast restart is the baseline iteration path.

## Verification

```powershell
# OFF gate runs: vr_plugins_unit_tests, vr_template_adapter_load_tests, vr_repo_adapter_tests,
# vr_lifecycle_full_tests, plugin_static_checks, plugin_validation_matrix,
# repo_adapter_static_checks, vrclient_smoke_host_once, game_profile_schema,
# repo_game_profile_schema
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\ci\build-and-test.ps1 -BuildDir build\ci
ctest --test-dir build\ci -R "plugin|adapter|lifecycle|smoke_host" --output-on-failure
```

## Bug-checking checklist

- [ ] ABI changes are additive + `size`/`version`-gated; nothing renumbered/removed.
- [ ] No exceptions / STL ownership / host-mem allocation across the ABI.
- [ ] Path resolution still rejects traversal + network paths; 4 exports resolved
      pre-init; identity checked pre-init.
- [ ] Adapter code never runs in the manager process.
- [ ] New `tick` work respects the timing budget + hot-path rules.
- [ ] Tests use throwing `expect()`; both happy and refuse paths covered in the
      validation matrix.

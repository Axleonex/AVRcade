# Managed-aware hook discovery (M6 Half A)

Extends the B2 hook-discovery toolkit (`src/native/tooling/hookdisc/`) with
**read-only** enumeration of managed (Mono / IL2CPP) methods, so the toolkit can
describe a hook surface for managed-runtime AAA games, not just native ones.

## Read-only guarantee

This is DISCOVERY, not injection. The managed locator reads a mapped image's
metadata and LISTS candidate methods. It **patches nothing, injects nothing,
evades nothing** (M6D4) — the same posture as B2's native inspection. There is
NO `LoadLibrary` / `CreateRemoteThread` / `WriteProcessMemory` / `VirtualAllocEx`
/ `SetWindowsHookEx` / byte-patching in the discovery half, and it never defines
`VRCLIENT_HOOKDISC_ENABLE_BYTE_PATCH_DETOUR`. A scoped grep over the new files
proves this (M6 plan §7b.4).

## Schema addition (additive, B5-guarded)

`config/schemas/hook-surface.schema.json` gained an additive `managed_method`
property and a 4th item-level `anyOf` branch `{required:[managed_method]}` (the
existing branches require `symbol`/`signature`/`offset`; a managed entry has
none of those). The B5 field guard confirmed the change is additive
(208 -> 214 field paths, re-snapshotted). A managed locator entry:

```json
{
  "name": "camera_update",
  "area": "camera",
  "module": "Assembly-CSharp",
  "status": "candidate",
  "confidence": "medium",
  "managed_method": {
    "locator_kind": "mono_method",
    "type_name": "PlayerCamera",
    "method_name": "LateUpdate"
  }
}
```

`locator_kind` is `mono_method` (Mono `Assembly-CSharp`-style image, class+method
by name) or `il2cpp_method` (metadata-token driven).

## Refusal posture

The managed locator reuses the existing `safety_gate` refusal: an unapproved
target returns `not_controlled_smoke_target` and writes nothing. It runs only
against a controlled fixture (`tests/native/tooling/managed_locator_fixture/`) in
CI — never a real commercial game in Half A (M6D2).

## How it feeds a future adapter

A validated managed hook surface is data-only (`config/hooks/<slug>.json`), just
like B2's native surface feeds Phase 6. A managed-runtime adapter reads the
`managed_method` locators to know which Mono/IL2CPP methods to target — the same
data-over-code discipline the whole project uses.

## Env note

The `managed_locators.{h,cpp}` implementation + its CTest require a C++ toolchain
to build. On a machine without one they are env-deferred (DEVIATIONS-M6.md); the
schema extension + fixture + this doc are the verifiable Half-A deliverables.

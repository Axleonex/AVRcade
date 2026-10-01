# Engine Hooking Strategy (per-engine adapter routing)

**Status:** Reference + routing guidance (not an implementation). Last verified: 2026-06-15.

This document records *which hooking approach fits which game engine*, so a new game
conversion starts from the right approach instead of re-deriving it — while preserving
the project's one-adapter-at-a-time discipline. It is the engine-selection companion to
[adapter-authoring.md](../sdk/adapter-authoring.md).

## The two layers (why you don't need a per-engine injector)

Converting a flatscreen game to VR means (1) getting your code **into** the game's
process, then (2) **hooking** its camera/rendering/input from the inside. These are
separate layers:

- **Layer 1 — delivery: ONE shared, engine-agnostic native injector** (Phase 3). It
  loads a DLL into any process via the standard OS mechanism, regardless of engine. It is
  **never duplicated per engine** — Unity, Unreal, Godot, and bespoke engines all use the
  same injector.
- **Layer 2 — hooking: per-engine.** *How* you find and patch the camera/rendering once
  inside depends entirely on the engine. This is the per-game **adapter's** job (Phase 4 SDK)
  and the only engine-specific work.

So the architecture is **one injector + N adapter backends**. Adding an engine = a new
adapter backend, **not** a new injector.

## Engine → hooking-tool routing (licenses verified 2026-06-15)

| Engine | Hooking layer | License | Notes |
|---|---|---|---|
| **Unity** | **MelonLoader** (loader) + **Harmony** (method patching) | Apache-2.0 + MIT | Most-permissive mature path; the default for new Unity targets. **Alternative: BepInEx (LGPL-2.1)** — larger/more-active ecosystem for some games. Choose per-game (see guidance #3). |
| **Unreal** | **UE4SS** (general mod loader; build VR on top) | MIT | The open Unreal modding loader. **Avoid depending on UEVR** (custom "All rights reserved" / NOASSERTION — not an open license) unless its actual terms are read and accepted. |
| **Godot** | bespoke (GDExtension / engine hooks) | engine MIT | No standard VR mod loader. Godot 4 ships native OpenXR, but shipped games rarely enable it. Lowest priority / most effort. |
| **Other / proprietary** | native injector + custom hooking | — | No mod loader exists; bespoke per game. |

## When to use this — LLM / contributor guidance

Reach for this doc when **scoping or authoring an adapter for a NEW game** (engine
selection, hooking approach). Rules:

1. **One-at-a-time discipline.** Do NOT build engine tooling speculatively. Integrate an
   engine's hooking layer only when a **real game of that engine is the active conversion
   target**. (See PROJECT.md "build exactly one complete adapter before any second game";
   multi-engine is `V2-MULTI-01`.)
2. **Identify the engine first**, from the game's files: Unity → `UnityPlayer.dll` /
   `*_Data/Managed/Assembly-CSharp.dll`; Unreal → `*-Win64-Shipping.exe` / `.pak`; Godot →
   `.pck` / Godot boot splash.
3. **Prefer the open, mature, community-standard loader for that specific game.** The
   license-permissive default for Unity is MelonLoader (Apache-2.0); but if the game's own
   modding ecosystem strongly favors the other (e.g. **R.E.P.O. → BepInEx**), match the
   ecosystem and accept its license (BepInEx LGPL-2.1 used as a separate loader = light
   obligations; your plugin stays your license).
4. **Verify the tool's CURRENT license before depending on it.** Licenses change and
   "popular" ≠ "open" — **UEVR is the cautionary case** (widely used, but custom
   all-rights-reserved). Re-check at integration time.
5. **The native injector is shared + foundational — never write a per-engine injector.**
6. The **VR runtime (OpenXR + Vulkan)** and the **adapter SDK** are engine-agnostic; keep
   engine-specific code confined to the hooking/adapter layer.

## Current status

- **Unity (R.E.P.O., Phase 6):** active target. The loader choice (BepInEx vs MelonLoader)
  is finalized at adapter-authoring time per guidance #3 (R.E.P.O.'s ecosystem leans BepInEx).
- **Unreal → UE4SS, Godot → bespoke:** **deferred to V2** (no target game yet). Listed here
  so the approach is known when the time comes — **not built**.

## License reference (verified 2026-06-15)

- Unity: BepInEx = LGPL-2.1; MelonLoader = Apache-2.0; Harmony/HarmonyX = MIT.
- Unreal: UE4SS = MIT; UEVR = custom / "All rights reserved" (NOASSERTION) — not open.
- Godot: engine = MIT; no standard VR mod loader.
- The injector: write-it-yourself = your code (no third-party license); copying an
  open injector = that injector's license (e.g. MIT, keep attribution).

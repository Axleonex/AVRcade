# Adapter Authoring

Phase 4 establishes the first stable adapter SDK boundary. Adapters provide
per-game behavior through a native plugin ABI and must not include OpenXR,
injector, manager-app, or runtime implementation internals.

**Before authoring a new adapter, pick the engine hooking approach.** See
[engine-hooking-strategy.md](../adapters/engine-hooking-strategy.md) for the
per-engine routing (Unity → MelonLoader/BepInEx + Harmony, Unreal → UE4SS, Godot →
bespoke), their licenses, and the rule that engine tooling is integrated only when a
real game of that engine is the active target (one adapter at a time).

## Files To Copy

Start a new adapter from:

- `adapters/_template/template_adapter.cpp`
- `adapters/_template/adapter.json`
- `adapters/_template/CMakeLists.txt`

Change the adapter ID, display name, supported game IDs, supported build IDs,
and manifest values before adding game-specific hooks.

## ABI Contract

Every adapter exports these C ABI symbols:

- `vrclient_get_adapter_abi`
- `vrclient_get_adapter_metadata`
- `vrclient_create_adapter`
- `vrclient_destroy_adapter`

The ABI uses a fixed calling convention, versioned structs with `size` fields,
host-owned service tables, and explicit string/buffer ownership. Adapters must
not throw exceptions across the ABI, transfer STL object ownership across the
ABI, allocate through host memory unless the service table explicitly allows it,
or expose runtime implementation headers.

The host resolves plugins from explicit local paths only. It rejects traversal
and network paths, resolves the four required export names before init, and on
Windows uses DLL search flags scoped to the plugin directory and default safe
search locations.

## Lifecycle

Adapter-implemented callbacks:

- `validate`
- `init`
- `tick`
- `suspend`
- `resume`
- `reload_config`
- `shutdown`

Framework-provided services:

- Diagnostics event service
- Runtime pose/timing service
- Adapter/game config lookup
- Optional/versioned input, comfort, and HUD services

The adapter context uses optional service handles so Phase 5 can add services
without breaking SDK v1 adapters.

## Shared Systems

Input, comfort, and HUD helpers are exposed through `VrAdapterService` entries
using `VrClientInputService`, `VrClientComfortService`, and
`VrClientHudService`. These service structs have `size` and `version` fields,
are host-owned, and are optional. Adapters must query for service availability
and degrade cleanly if a service is absent.

Input services return semantic action state such as `interact`, `recenter`, and
comfort toggles. Adapters should consume these semantic actions instead of raw
device-specific controller buttons.

Comfort services return a snapshot of snap turn, smooth turn, vignette,
seated/standing, world scale, and height offset settings. These values are
profile-backed and runtime-changeable without recompiling adapters.

HUD services expose per-game data as anchor snapshots. HUD transforms are
row-major 4x4 matrices in meters using a right-handed convention where forward
from the headset is negative Z. Per-game data belongs in game profiles and
adapter manifests, not in shared runtime or manager code.

## Game And Build Identity

Adapter manifests and metadata must use the same game/build identity namespace
produced by Phase 3 fingerprint detection. The host rejects adapters whose
metadata does not match the detected `game_id` and `build_id` before init.

## Hot Path Rules

Frame/tick callbacks inherit the runtime render-loop rules:

- No blocking file, network, process, or OS wait operations
- No synchronous logging
- No avoidable allocation
- No long-running work
- Stay under the host callback timing budget

The host records callback duration and overrun counts. Repeated recoverable
overruns disable the adapter and leave diagnostics.

## Failure Handling

Recoverable adapter failures quarantine the adapter and record structured
diagnostics. Unrecoverable target-process crashes should include adapter
identity in crash artifacts when available. The hard isolation guarantee is that
adapter code is not loaded into the manager process.

## Development Loop

Fast restart is the baseline iteration path and preserves config plus
diagnostics context. Hot reload is opt-in and is refused while callbacks,
hooks, owned threads, thread-local/static teardown risks, or target teardown are
active.

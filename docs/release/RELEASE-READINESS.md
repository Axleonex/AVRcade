# VRClient Release Readiness

This document separates reusable platform readiness from the individual compatibility work required by each supported game.

## Current evidence

| Area | Current state | Release interpretation |
|---|---|---|
| Windows launcher | Built desktop client with library, launch, controller-reference, appearance, and per-game workflow surfaces | Usable developer-alpha front end |
| OpenXR/headset path | Real headset evidence exists for the shared runtime and supported conversion flows | A supported game still needs its own headset acceptance record |
| Safety and local rollback | Safety gates and local backups are used by supported workflows | Needs one generic, versioned package/repair/rollback contract before public delivery |
| Controller and comfort settings | Shared concepts exist and Cyberpunk validates the interaction model | Needs title-neutral persistence, schemas, and migration rules |
| Diagnostics | Logs and local troubleshooting exist | Needs a single redacted export and recovery/doctor workflow |
| Cross-platform | Windows-first implementation | SteamOS/Linux remains a later portability phase |

## Available shared release surface

The title-neutral release foundation is available through the standalone `VrClient.ReleaseTool` and the core release modules. It supports local manifest inspection, verified package lifecycle, preferences, variant evaluation, Doctor reporting, redacted diagnostics, and platform capability evaluation. The launcher consumes the passive readiness status only; game launch behavior remains owned by each conversion profile.

## Definition of release-ready

A public Windows release is ready only when:

1. Shared packages are versioned, integrity-checked, repairable, and rollback-capable.
2. Every title presents a clear flat/VR/launch-only state and never enables an unsafe action.
3. Users can run a preflight check, export a redacted diagnostic bundle, and recover from a failed update.
4. Each supported title passes the common qualification gate: flat launch, VR launch, stereo/head tracking, input, safety/rollback, and diagnostics.
5. Legal and privacy text has completed human review.

## Active-work isolation

The release-foundation milestone deliberately does not modify engine adapters, title-specific profiles, or `external/` source snapshots. New converters can continue in parallel; they consume the generic release contracts once each contract is stable.

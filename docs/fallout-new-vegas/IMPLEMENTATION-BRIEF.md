# Archived Fallout: New Vegas VR Integration Brief

Historical input only. Its VorpX-based direction was superseded by the user's
2026-09-06 decision to use VRClient's native OpenXR renderer. It is retained as
provenance, not as current implementation or launch guidance. See `README.md`
and `NATIVE-COMPLETION-PLAN.md` for the active direction.

Build the Fallout: New Vegas VR integration for VRClient as close to production-ready as possible before physical headset verification. Work only in New Vegas-owned paths and do not modify, revert, or depend on unrelated unmerged work in this dirty checkout.

## Supported stack

- Legitimately owned Fallout: New Vegas; Steam is primary, with GOG where supportable.
- Mod Organizer 2 as the preferred isolated profile manager.
- FNV 4GB Patcher, xNVSE, JIP LN NVSE Plugin, ShowOff xNVSE Plugin.
- Fallout: New Virtual Reality by iloveusername and its open-source FNVR companion tracker.
- User-owned VorpX, SteamVR, and Virtual Desktop through SteamVR.

Do not build a speculative DirectX 9 stereo renderer or claim VorpX replacement. Never bypass a license or activation mechanism.

## Required implementation

1. Follow existing adapter, manifest, installer, safety, comfort, launch, diagnostics, UI, CLI, and test patterns. Avoid broad shared refactors.
2. Record authoritative dependency URLs, versions, licenses, authors, hashes where reliable, redistribution conditions, and retrieval dates in a machine-readable manifest.
3. Discover Steam libraries, GOG installs where supported, and manual paths; identify executable/build state and produce precise wrong-directory or unsupported-edition diagnostics without automating accounts.
4. Detect and validate FNV 4GB patch state, xNVSE, JIP LN, ShowOff, FNVR plugin and enabled `FNVR.esp`, FNVR tracker, VorpX presence, SteamVR, Virtual Desktop-through-SteamVR, MO2, and the selected profile.
5. Provide a reversible workflow with dry-run planning, explicit acknowledgement before mutation, isolated profile cloning/selection, backups for managed configuration, idempotent apply, repair, and uninstall/restore. Never overwrite/delete existing mods, silently reorder the main profile, or bundle restricted packages.
6. Preserve ESP/ESM and xNVSE behavior. Classify mods as verified, warned, incompatible, or unknown. Warn about VR-hostile HUD, camera, animation, skeleton, weapon-position, input, VATS, post-processing, and incompatible DLL categories without silently disabling anything. Keep VR and non-VR profiles separate and make no blanket compatibility claim.
7. Orchestrate visible, user-controlled helpers and runtimes in correct order, launch the patched xNVSE route, capture component-specific logs and exit codes, clean up only processes VRClient started, distinguish direct SteamVR from Virtual Desktop to SteamVR, and issue actionable failures.
8. Support only upstream-verified comfort/configuration fields: seated/standing, turning, movement orientation, handedness, weapon calibration, HUD guidance, gestures, and performance presets. Do not invent keys; mark hardware-only fields pending.
9. Present credits and licenses for Bethesda, Obsidian, xNVSE contributors, JIP LN, ShowOff, iloveusername/FNVR, VorpX, MO2, and all incorporated dependencies. Link to third-party downloads instead of bundling restricted files and respect FNVR credit, modification, open-source, and non-commercial conditions.
10. Add CI-safe unit, schema, storefront, fake-install, dirty-profile, dependency-failure, dry-run/acknowledgement, idempotency, backup/restore, launch-order/cleanup, SteamVR, Virtual Desktop, Unicode/space-path, interruption, and recovery tests. No commercial software, credentials, game files, or headset may be required.
11. Run two verification passes: first build/test and repair; then skeptical full-diff review for security, licensing, mod preservation, cleanup, error handling, and test gaps, followed by another test run.
12. Document final human verification for wired/native SteamVR and Quest through Virtual Desktop to SteamVR, stereo/per-eye projection, 6DoF, both controllers, weapon/shot alignment, locomotion/turning, HUD/dialogue, Pip-Boy/gestures, save/load, representative mods, clean shutdown, and restoration of the non-VR profile.

## Completion report

Report readiness, architecture, changed files, dependencies/licenses, exact commands/results, mod-compatibility behavior, unresolved limitations, routing evidence, and precise final human steps. Do not commit, merge, push, or claim hardware verification.

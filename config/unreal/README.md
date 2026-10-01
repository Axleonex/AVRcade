# Unreal route index

**Current primary direction:** VRClient-owned native Unreal conversion.

- Native schema: `config/schemas/unreal-native-profile.schema.json`
- Archived Meccha native profile: `archive/meccha-chameleon-2026-08-06/config/unreal/native-meccha-chameleon.json`
- Architecture: `docs/unreal/NATIVE-ARCHITECTURE.md`
- Current handoff: `PASSOVER-NATIVE-MECCHA.md`

Everything named `*.uevr.json`, `uevr-release.json`, and
The archived `meccha-chameleon` profile belongs to the legacy Windows v0.2 UEVR fallback.
Those files remain supported historical/fallback data, but they are no longer part of the active catalog and cannot satisfy
`NUE-05..NUE-08` or prove the native route.

Do not infer a live D3D12 renderer, game bridge, stereo, or head tracking from
the native profile's expected values. Promote its states only from the evidence
ladder documented in the current passover.

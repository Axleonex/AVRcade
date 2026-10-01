# R.E.P.O. Adapter Notes

## Target Identity

- Game: R.E.P.O.
- Adapter ID: `vrclient-repo-adapter`
- Game ID: `repo`
- Steam app ID: `3241660`
- Supported build: `steam-3241660-build-23363152`
- Executable: `H:\SteamLibrary\steamapps\common\REPO\REPO.exe`
- Executable SHA-256: `412f7cf79cf16888999e22905ca3bb11a6074857efdf8c754073a47ef872317c`
- Unity build: `2022.3.67f2 (6bedba8691df)`

The adapter and fingerprint config support only this selected build. Other game IDs, executable names, hashes, and build IDs must refuse before adapter init.

## Safety Policy

R.E.P.O. is Phase 6 private/modded co-op capable, not public-matchmaking capable. Public matchmaking, public online state, unconfirmed mod compatibility, and unknown network state are block conditions before injection or attach.

Current policy evidence:

- `config/games/repo.json` uses `controlled_smoke_target: false`.
- Allowed sources: `steam`, `manual`.
- Target architecture: `x64`.
- Anti-cheat risk: `known_safe`.
- Online risk: `private_modded_coop`.
- The preflight approves only known-supported identity, allowlisted source, `known_safe` or `none` anti-cheat risk, confirmed private/modded session scope, explicit mod compatibility confirmation, matching architecture, and exact runtime hash.

Official modding support, community toolkits, and mod application clients are useful compatibility signals because they suggest the game can tolerate local client-side modification in some contexts. They are not safety approval by themselves and do not bypass exact build identity, private/modded session confirmation, anti-cheat scan, or pre-injection refusal rules.

## Static Hook Map

Non-invasive static inspection found candidate managed/Unity surfaces. These names are not yet validated hooks, offsets, signatures, or detour points.

| Area | Candidate Surfaces | Phase 6 Use |
| --- | --- | --- |
| Camera/projection | `MainCamera`, `CameraAim`, `CameraTransform`, `CameraUpdate`, `CameraPosition`, `CameraZoom`, `CameraOverlay` | Candidate camera pose/FOV/projection research surface. No validated call site or stable hook ABI yet. |
| HUD/UI | `HUDCanvas`, `HealthUI`, `SemiUI`, `ChatUI`, `MapToolController` | Candidate HUD anchor names. Profile anchors are present but marked `template_data: true` until real transform validation. |
| Input/interaction | `InputMovement`, `InputMovementX`, `InputMovementY`, `GetRotationInput`, `InputMouseX`, `InputMouseY`, `Grab`, `GrabBeamLogic`, `ForceGrabPhysObject`, `GetItemPlayerIsHolding` | Candidate interaction and grab-alignment research surface. Adapter currently consumes shared input/comfort services but does not patch or redirect game input. |
| Single-player/private markers | `MenuActionSingleplayerGame`, `MainMenuIsSingleplayer`, `MainMenuSetSingleplayer` | Candidate non-public mode allow markers for later runtime verification. |
| Block markers | `MainMenuIsMultiplayer`, `MenuActionRandomMatchmaking`, `PhotonNetwork`, `PhotonView`, `PhotonVoice`, `LeavePhotonRoom` | Candidate block/review signals. Public matchmaking, public online state, unconfirmed private room state, or unknown network state must refuse. |

## Implementation Status

Implemented in this pass:

- Selected-build adapter package under `adapters/repo/`.
- Adapter metadata and manifest for only `repo` / `steam-3241660-build-23363152`.
- Shared input, comfort, and HUD service consumption.
- R.E.P.O. profile at `config/profiles/repo-game-profile.json`.
- Versioning loader support for `controlled_smoke_target: false` configs.
- Safety preflight approval for selected commercial targets only when policy is explicitly safe/private-modded and identity/runtime evidence match.
- Automated tests for fingerprint matching, unsafe policy refusal, schema checks, and adapter host load.

Not implemented:

- Camera pose/FOV/projection correction.
- HUD transform correction against live game coordinates.
- Menu/cutscene/gameplay state transitions.
- Grab/interaction alignment against live gameplay.
- Real injector/runtime attachment to R.E.P.O.
- Headset validation.

## Task 4 Conclusion

Static strings identify candidate R.E.P.O. surfaces, but this workspace does not yet contain a concrete safe hook surface: no validated managed method signatures, offsets, stable detour points, mode-state callback, or non-invasive hook API has been proven for the selected build.

Per `06-02-PLAN.md`, execution must stop before Task 5. The adapter remains a selected-build-only scaffold and service integration test target, not a playable VR conversion.

# R.E.P.O. First Target Selection

## Selected Target

- Selected game: R.E.P.O.
- Slug: `repo`
- Steam app ID: `3241660`
- Install source: Steam library on `H:\`
- Install root: `H:\SteamLibrary\steamapps\common\REPO`
- Executable: `H:\SteamLibrary\steamapps\common\REPO\REPO.exe`
- Executable architecture: x64
- Steam manifest: `H:\SteamLibrary\steamapps\appmanifest_3241660.acf`
- Steam build ID: `23363152`
- Steam target build ID: `23363152`
- Installed depot manifest: `8225435692675993447`
- Manifest updated: `2026-06-12T07:08:42Z`
- Executable SHA-256: `412f7cf79cf16888999e22905ca3bb11a6074857efdf8c754073a47ef872317c`
- Executable file version: `2022.3.67.7073210`
- Executable product version: `2022.3.67f2 (6bedba8691df)`
- Unity build GUID: `8445f541673a4a2290ab26acd259917f`

## Selection Rationale

R.E.P.O. is selected for the Phase 6 target-selection dossier because the user explicitly provided it as the locally available target on `H:\`, the install has a concrete Steam build identity, the executable can be fingerprinted without launch or injection, and the game is a Unity/Mono Windows x64 title with a small first-proof research surface.

The target is accepted for dossier and future planning under a private/modded co-op safety restriction. Steam lists both Single-player and Online Co-op, while the store description and local install show networked co-op support. Phase 6 implementation must not support public matchmaking or unknown online injection. Private/modded co-op can be represented only when session scope and mod compatibility are explicitly confirmed before load.

## Safety Rationale

Anti-cheat status is treated as `known_safe` for this dossier, not as a permanent guarantee:

- The local install scan found no files or directories matching common anti-cheat packages such as Easy Anti-Cheat, BattlEye, Xigncode, nProtect/GameGuard, EQU8, Riot Vanguard, FACEIT, or generic `anticheat` names.
- Public reporting from March 2025 quotes semiwork discussing anti-cheat as a tradeoff for matchmaking and mods, implying it was not an already-settled bundled requirement at that point.
- Thunderstore hosts a community `RepoAntiCheat` client-side mod, which is separate from a bundled first-party anti-cheat and supports the conclusion that anti-cheat handling is not currently a standard packaged service in this local install.

R.E.P.O.'s mod ecosystem is favorable evidence for adapter feasibility, but it is not treated as automatic safety approval. VRClient still requires exact build identity, private/modded session confirmation, anti-cheat scan evidence, and a pre-injection allow verdict.

Online/account risk is gated:

- Steam lists Single-player, Online Co-op, in-game chat, and online interactivity.
- Local files include Photon, Photon Voice, Steamworks, Discord, WebRTC audio, and Steam API libraries.
- VRClient support for this target must be gated to private/modded co-op or offline validation. Unknown online/public co-op state blocks launch or attach.

No launch, injection, attach, hook, patch, or VRClient runtime load was performed during this dossier pass.

## Local Build Evidence

Evidence is indexed at `artifacts/phase06/repo/selection/index.md`.

Key non-invasive evidence:

- Steam app manifest confirms app ID `3241660`, install directory `REPO`, build ID `23363152`, and target build ID `23363152`.
- `REPO.exe` SHA-256 is a real 64-character digest.
- `REPO.exe` PE machine is x64.
- `REPO.exe` product version identifies Unity `2022.3.67f2`.
- `REPO_Data\boot.config` includes Unity build GUID `8445f541673a4a2290ab26acd259917f`.
- Managed assembly inventory includes `Assembly-CSharp.dll`, Photon networking assemblies, and Unity modules.

## Rejected Candidates

- Lethal Company: rejected for this first proof because no local install/build evidence was provided or captured in this checkpoint, and selecting a second co-op horror title would violate the exactly-one target constraint.
- Phasmophobia: rejected for this first proof because it is multiplayer/online-oriented and was not locally fingerprinted in this pass; it carries more safety ambiguity than the R.E.P.O. single-player-only proof path.

## Decision

Proceed with `repo` as the concrete Phase 6 first target for ADAPT-01. Treat the target as selected only for dossier, fingerprinting, and the next concrete planning brief. Adapter implementation, hooks, injection, HUD correction, and headset playability remain deferred to `06-02-PLAN.md`.

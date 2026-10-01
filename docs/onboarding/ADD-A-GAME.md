# Add a Game to VRClient (orchestrate track)

Audience: an LLM or a person onboarding a NEW Unity game that **already has a
community VR mod on Thunderstore**. No code changes are needed — a game is data.
If the game has NO community VR mod, STOP: this recipe does not apply (that needs
the M4 conversion framework).

## Before you start — is this game eligible?
1. The game is a **Unity** game (has `<Game>_Data/` or `UnityPlayer.dll`).
2. A **VR mod exists on Thunderstore** for it (search the game's community).
3. The mod loads via **BepInEx** (its dependencies include `BepInEx-BepInExPack`).
4. The game is **not** anti-cheat protected and its online scope is offline or
   private/modded co-op. If anti-cheat is present or online scope is public →
   STOP; VRClient blocks these by policy.

## Step 1 — Scaffold the data files
Run (replace the angle-bracket values):
```
dotnet run --project client/VrClient.Cli -c Release -- add-game <slug> \
  --app-id <steam-app-id> --community <thunderstore-community-slug> \
  --mods BepInEx/BepInExPack,<ModAuthor>/<ModName>
```
This writes four files and refuses if the game already exists:
- `config/modpacks/<slug>.modpack.json`
- `config/modpacks/<slug>.comfort-map.json`
- `config/games/<slug>.json`   (fields deliberately set to "unknown" — fail-closed)
- `config/profiles/<slug>-game-profile.json`

Do NOT write mod version numbers. Versions are pinned live by `resolve`.

## Step 2 — Set the safety posture (required; the game is blocked until you do)
Edit `config/games/<slug>.json` → `support_policy`:
- `anti_cheat_risk`: `known_safe` if you confirmed no bundled anti-cheat, else leave `unknown` (stays blocked).
- `online_risk`: `offline_only` or `private_modded_coop` (never a public value).
Then add ONE rule row to `config/safety/default-rules.json` `rules[]`:
```
{ "game_id": "<slug>", "controlled_smoke_target": false, "allow_offline": false,
  "allow_warn_on_uncertain": false, "adapter_id": "<slug>",
  "allowed_launch_modes": ["private_modded_coop","single_player"],
  "acceptable_modding_postures": ["community-supported"] }
```

## Step 3 — Point the comfort map at the mod's real config keys
The scaffolded `<slug>.comfort-map.json` has placeholder keys and
`verified_against_cfg: false`. After a first modded launch (Step 6) the mod
writes its `.cfg`; run `verify-comfort-map <slug> --game-dir "<dir>"`, fix the
`map` to the reported real keys, and set `verified_against_cfg: true`.

## Step 4 — Resolve and gate
```
dotnet run --project client/VrClient.Cli -c Release -- resolve <slug>
dotnet run --project client/VrClient.Cli -c Release -- check-game <slug>
```
`check-game` must print `RESULT: check=pass`. If it prints `fail`, fix the
`CHECK: … FAIL` items and re-run. `resolve` must produce
`config/modpacks/<slug>.lock.json` with `https://` URLs and 64-hex sha256s.

## Step 5 — Safety
```
dotnet run --project client/VrClient.Cli -c Release -- safety <slug>
```
Must print `verdict=Allow` or `verdict=Warn`. `Block`/`UnknownBlocked` means the
posture in Step 2 is missing or unsafe — do not proceed.

## Step 6 — Install, configure, launch (on a machine with the game + a VR runtime)
```
dotnet run --project client/VrClient.Cli -c Release -- convert <slug> --game-dir "<install dir>" --acknowledge
dotnet run --project client/VrClient.Cli -c Release -- launch  <slug> --game-dir "<install dir>" --acknowledge
```
Confirm stereo VR + head tracking, then record the result under
`artifacts/friendslop/<slug>/headset-confirmation.md`.

## Worked example: Lethal Company
- slug: `lethal-company`, app id: `1966720`, community: `lethal-company`
- mods: `BepInEx/BepInExPack,DaXcess/LethalCompanyVR`
- posture: `anti_cheat_risk: known_safe`, `online_risk: private_modded_coop`
(Version numbers are resolved live — never hardcoded.)

## Definition of onboarded
`vrclient check-game <slug>` passes AND `vrclient safety <slug>` is Allow/Warn AND
a headset confirmation exists. No `.cs` file changed.

# VrClient (T-Unity M1)

`vrclient` is the headless .NET 8 client for the T-Unity track: it resolves,
hash-verifies, installs, safety-gates, comfort-configures, and launches the
community RepoXR VR mod for R.E.P.O.

Build and test (from the repo root):

```
dotnet build client/VrClient.sln -c Release
dotnet test client/VrClient.sln -c Release
```

## Live resolve smoke (network; user/CI-run)

This is run when network is available; it is NOT a unit test and NOT part of
`dotnet test`:

```
dotnet run --project client/VrClient.Cli -c Release -- resolve
```

Expected: writes `config/modpacks/repo.lock.json` validating against
`config/schemas/modpack-lock.schema.json`, containing at least `BepInExPack`
and `RepoXR` with 64-hex sha256 values and `https://` download URLs.

## Verifying the comfort map

`config/modpacks/repo.comfort-map.json` maps our comfort profile fields to
RepoXR cfg keys, but the RepoXR key names are marked
`"verified_against_cfg": false` until validated against a REAL generated cfg.
RepoXR writes `BepInEx/config/io.daxcess.repoxr.cfg` into the game directory
on its first modded launch. After that first launch, run:

```
dotnet run --project client/VrClient.Cli -c Release -- verify-comfort-map --game-dir "<REPO install dir>"
```

The report lists which mapped keys were matched vs unmatched in the real cfg
and prints a suggested corrected map. If keys are unmatched, update
`config/modpacks/repo.comfort-map.json` to the reported keys and set
`"verified_against_cfg": true`. This is a user step — it requires a real
modded install and is not part of the unit test suite.

## Milestone 1 headset protocol

Run on a machine WITH R.E.P.O. installed (Steam app 3241660), Steam running,
and a VR runtime active (e.g. Virtual Desktop / SteamVR). All commands run
from the repo root.

1. `dotnet run --project client/VrClient.Cli -c Release -- resolve`
2. `dotnet run --project client/VrClient.Cli -c Release -- safety` (must print `verdict=Allow` or `verdict=Warn`)
3. `dotnet run --project client/VrClient.Cli -c Release -- convert --game-dir "<REPO install dir>" --acknowledge`
4. Launch once via `--dry-run` to confirm the plan, then for real:
   `dotnet run --project client/VrClient.Cli -c Release -- launch --game-dir "<REPO install dir>" --acknowledge`
5. In-game: open VR Settings (RepoXR), confirm stereo + head tracking; then run
   `verify-comfort-map --game-dir "<REPO install dir>"` and, if it reports
   unmatched keys, update `config/modpacks/repo.comfort-map.json` to the
   reported keys and set `"verified_against_cfg": true`.
6. Record the result: create `artifacts/friendslop/repo/headset-confirmation.md`
   stating whether the game rendered in stereo VR with head tracking
   (yes/no + notes).

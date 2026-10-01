# Community VR routes: PEAK, Content Warning, Big Walk, RV There Yet?

VRClient lists these four flat games without claiming an official VR edition.
The first three use Thunderstore packages. RV There Yet? uses the existing UEVR
route because it is an Unreal game. On its page, **Download and install VR setup**
fetches the pinned UEVR nightly, app-local .NET Desktop runtime, and reviewed
profile directly from their upstream sources, verifies their hashes, and installs
them for the current user. The profile has not been headset-tested by VRClient.

| Game | VR route | Current evidence | VRClient headset status |
| --- | --- | --- | --- |
| PEAK | [Andrey04o/PeakVR](https://thunderstore.io/c/peak/p/Andrey04o/PeakVR/) | 1.5.0 lists PEAK 2.4.b as tested; OpenXR, 6DoF head and hands | Unverified locally |
| Content Warning | [DaXcess/CWVR](https://thunderstore.io/c/content-warning/p/DaXcess/CWVR/) | 1.2.0 is deprecated; the source repository is archived and the game has updated since its release | Compatibility with the current game build unverified |
| Big Walk | [CircuitLord/Big_Walk_VR](https://old.thunderstore.io/c/big-walk/p/CircuitLord/Big_Walk_VR/) | 1.0.18 uses BepInEx 6 IL2CPP and SteamVR; author describes it as early work | Unverified locally |
| RV There Yet? | [UEVR](https://github.com/praydog/UEVR) plus the [immutable reviewed profile tree](https://github.com/uevr-profiles/repo/tree/975fde4c8e3d4ec12883d67a264742cecf4e9496/a5d68bf1-cb90-09ba-0ef0-659645fb5007) for `Ride-Win64-Shipping.exe` | Config-only profile identified and hashed; upstream has no declared license, so VRClient uses user import and does not redistribute it | Unverified locally; headset + gamepad milestone |

The Unity package locks under `config/modpacks/` pin downloaded package hashes.
PEAK's loader uses `BepInExPack_PEAK/`; its VR mod uses top-level `plugins/`
and `patchers/`. Big Walk's package uses backslash paths and a preloader that
copies runtime files into the game directory on first run. VRClient's installer
maps these layouts and its uninstall removes matching generated Big Walk files
only when they were absent before install. Any changed or preexisting files stay.

The comfort maps are intentionally empty until a real mod config is observed.
Do not invent comfort keys. All three Unity entries stay `Profile unverified`
after install until stereo, head tracking, and controls are confirmed in a
headset. Content Warning additionally needs a current-build compatibility run.

For private modded co-op, the CLI route is:

```powershell
# [PowerShell] Replace <slug> with peak, content-warning, or big-walk.
dotnet run --project client/VrClient.Cli -c Release -- check-game <slug>
dotnet run --project client/VrClient.Cli -c Release -- safety <slug>
dotnet run --project client/VrClient.Cli -c Release -- convert <slug> --game-dir '<Steam game directory>' --acknowledge
```

For Big Walk, start SteamVR before launch. RV There Yet? can use either one
healthy VirtualDesktopXR route or SteamVR/OpenXR; SteamVR is not mandatory when
VDXR owns the live headset session. Its `Profile unverified` state is expected
until the directly downloaded, hash-checked profile is tested on a fingerprinted
game build. The optional ZIP import remains available for offline setup.
The app must not mark stereo, tracking, gamepad, motion controls, or private
co-op as proven before their separate evidence gates pass.

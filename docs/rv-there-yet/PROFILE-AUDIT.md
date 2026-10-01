# RV There Yet? community profile audit

Status: source identified and statically reviewed; not headset-verified.

## Immutable identity

- Repository: <https://github.com/uevr-profiles/repo>
- Commit: `975fde4c8e3d4ec12883d67a264742cecf4e9496`
- Profile directory: `a5d68bf1-cb90-09ba-0ef0-659645fb5007`
- Immutable tree: <https://github.com/uevr-profiles/repo/tree/975fde4c8e3d4ec12883d67a264742cecf4e9496/a5d68bf1-cb90-09ba-0ef0-659645fb5007>
- Git tree object: `fe8f10ada4e56c3d20efeb49e00b274dcf4d55e8`
- Author recorded by the profile: `d_rey86`
- Published: 2025-11-09

The repository declares no license. VRClient therefore does not vendor or
redistribute these files. The supported route is a user-selected local archive
whose contents are checked against the hashes in
`config/unreal/rv-there-yet.uevr.json`.

## Reviewed contents

| File | SHA-256 | Classification | Disposition |
| --- | --- | --- | --- |
| `ProfileMeta.json` | `704bf28bfa1a425f4a54bc74e01b0bd26047d5e13e425934b8822dcfcc4590a3` | Metadata | Import |
| `README.md` | `25f59b0fc7a8bdd26fccfe04ec0d70aea8aa913ea2cc0fd3d11c65b48fe08f9f` | Documentation | Import |
| `cameras.txt` | `ea92615b01591b49b4ba23041ba34b9c886ff3e043c5ce6c856f31f0cfd62250` | UEVR camera configuration | Import |
| `config.txt` | `0942c8f2361e76d7ca65f2551f3ce50940e1119d45ac8531edbd705bf42ebbfc` | UEVR configuration | Import |
| `imgui.ini` | `dd32c13ea4a0be4d71200f7a67333a3fb6beba4ee9e333f7b8500c9fa2eb0998` | UEVR UI configuration | Import |
| `_EXTRAS.zip` | `206aef1c3fbe18139b63ff174b5460ba6b4cb1f07ab11e92abb4adc5b001ea0d` | Diagnostic archive containing only `log.txt` | Exclude |

No Lua script, plugin DLL, executable, or motion-controller mapping is present.
The profile README asks for a recent UEVR nightly and describes a quick
head-aiming profile with a resized/repositioned HUD. `config.txt` requests the
OpenXR loader, enables controllers generically, and disables 2D-screen mode;
those settings do not prove tracked-controller mappings.

`ProfileMeta.json` contains a stale `downloadUrl` for another directory and an
expiring Discord CDN URL. Neither is an authority. VRClient uses the immutable
GitHub commit and per-file hashes above.

## Import and trust boundary

In VRClient, open RV There Yet? and click **Download and install VR setup**.
The app downloads UEVR, its app-local .NET prerequisite, and the five profile
files from the pinned upstream commit. It checks each profile hash before
installing anything in the UEVR profile directory.

For an offline import, create a ZIP containing exactly the five imported files
at its root, then run:

```powershell
# [PowerShell]
dotnet run --project client/VrClient.Cli -c Release -- uevr-profile-import rv-there-yet --archive 'C:\path\reviewed-rvty-profile.zip'
```

The importer rejects traversal, missing or extra files, hash changes, and code
content. It backs up replaced files and writes a deployment manifest. Roll back
unchanged imported files with `uevr-profile-rollback rv-there-yet`; files edited
after import are preserved.

## Capability verdict

Stereo and head tracking are upstream claims only. Gamepad, private co-op, game
build compatibility, and all comfort behavior remain untested. Motion controls
are not present in the reviewed artifact. None may be shown as verified until
the corresponding protocol has evidence for a fingerprinted game build.

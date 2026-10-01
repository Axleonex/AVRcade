# No-mod Unity target: A Short Hike

SELECTED SLUG: a-short-hike
STEAM APP ID: 1055540
EXE NAME: A Short Hike.exe
MONO CHECK: pending local install

## Why this target

- The official Steam listing describes a small, single-player 3D game with no
  online or anti-cheat dependency.
- Public Unity modding evidence references its `Assembly-CSharp.dll`, making it
  a strong Mono candidate, but the local filesystem check remains authoritative.
- No maintained OpenXR/PCVR mod was found in the searched GitHub and Thunderstore
  surfaces as of 2026-07-16.
- Its modest hardware requirements and short play loop make repeated headset
  bring-up less costly than a large game.

## Hard entry gate

Before deployment, `build-plugin.ps1` must find
`A Short Hike_Data/Managed/Assembly-CSharp.dll`, must not find `il2cpp_data`, and
must find `UnityEngine.XRModule.dll`. The target remains **provisional** until
those checks run against the installed Steam build. The OpenXR provider itself
is a separate runtime gate: `UXR-BOOT-FAIL: no XR loader` is an honest failed
experiment, not permission to claim stereo.

Sources: the official Steam product page and Unity's assembly documentation are
linked in `docs/unity-vr/FRAMEWORK.md`.

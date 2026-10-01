# VRClient UnityVR framework

This is a game-agnostic BepInEx plugin experiment for Mono Unity games that do
not already have a VR mod. It is deliberately downstream of the shipped v0.2
orchestration product.

The plugin:

1. asks Unity XR Management to initialize its active loader;
2. makes the selected game camera stereo and applies the tracked head pose;
3. reads OpenXR-backed controller move/look/trigger features;
4. exposes those inputs to a per-game `GameModule`.

It logs falsifiable states: `UXR-BOOT-OK`, `UXR-BOOT-FAIL`, `UXR-RIG`,
`UXR-RIG-FAIL`, `UXR-INPUT`, and `UXR-READY`. A DLL build proves only the ABI
surface compiles. It does not prove that a target contains a compatible Unity
OpenXR provider, that stereo presents, that head tracking is correct, or that a
game module consumes controller input.

Build and deploy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File unity-vr\build-plugin.ps1 `
  -GameDir "C:\Program Files (x86)\Steam\steamapps\common\A Short Hike" `
  -Slug a-short-hike
```

The script refuses IL2CPP, requires the Mono and XR assemblies, fetches the
official pinned BepInEx archive, verifies its SHA-256, builds against local game
assemblies without copying them into the repository, and deploys only VRClient's
two DLLs. A target lacking `Unity.XR.Management`/an active OpenXR loader stops at
`UXR-BOOT-FAIL: no XR loader`; supplying a version-matched provider is the next
target-specific R&D task.

Primary references:

- [A Short Hike on Steam](https://store.steampowered.com/app/1055540/A_Short_Hike/)
- [Unity assembly documentation](https://docs.unity.cn/Manual/assembly-definitions-intro.html)
- [BepInEx project and releases](https://github.com/BepInEx/BepInEx)

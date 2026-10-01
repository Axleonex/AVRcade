# Add a no-mod Mono Unity game

1. Require `<Game>_Data/Managed/Assembly-CSharp.dll`; refuse `il2cpp_data`,
   anti-cheat, online-only play, and any game with a maintained VR mod.
2. Run `unity-vr/build-plugin.ps1 -GameDir "<game>" -Slug <slug>`. The script
   installs only the hash-pinned BepInEx loader and VRClient's local plugin.
3. Launch once with an active OpenXR runtime and inspect `BepInEx/LogOutput.log`.
4. If `UXR-BOOT-FAIL: no XR loader` appears, the game has no compatible Unity
   OpenXR provider. Stop and select/version the provider for this exact Unity
   build; do not mark stereo as proven.
5. If `UXR-RIG-FAIL` appears, set `CameraObjectName` in
   `BepInEx/config/vrclient-unityvr.<slug>.json` from a real object dump.
6. `UXR-BOOT-OK` plus `UXR-RIG` means initialization reached the render loop;
   only headset observation can prove stereo and tracking.
7. Move both thumbsticks and triggers. `UXR-INPUT: controller activity observed`
   proves OpenXR input reached the framework; a target module and in-game motion
   prove that the game actually consumes it.

Record the log, machine/build facts, configuration and DLL hashes, then explicit
T1 stereo/tracking and input results under `artifacts/unity-vr/<slug>/`.
`unity-vr/record-headset-evidence.ps1` enforces the order: T1 requires direct
stereo + tracking confirmation and the boot/rig log tokens; T2 refuses until a
T1 artifact exists, then requires both controller-activity telemetry and
observed in-game look/locomotion.

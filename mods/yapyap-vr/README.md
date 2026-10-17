# AVRcade YAPYAP VR mod

This is the game-specific control layer for the YAPYAP Unity Mono VR route. It
connects UUVR's SteamVR actions to a virtual gamepad in Unity's Input System and
pairs that gamepad with YAPYAP's player input. It also bypasses the game's
pixel-scaled flat render image so UUVR can track and render the main camera in
stereo. Voice spell recognition remains provided by the game and uses the
active headset microphone.

The current `0.3.39` build is an experimental VR bridge. The local test package
combines it with [UUVR](https://github.com/Raicuparta/uuvr) for stereoscopic
rendering and headset tracking, pinned at revision
`de8e9218f8a9310778f2f57c189bef9ccc8c4dc8`. UUVR is GPL-3.0 licensed;
its license travels with the staged files. The plugin selects OpenVR, absolute
camera tracking, and no flat UI mirror when it starts. It preserves YAPYAP's
game camera yaw while applying headset rotation, so the right stick turns the
view. Quest 2 stereo, head tracking, menu selection, movement, interaction,
and right stick turning were confirmed in a level with QuotaDifficulty loaded.
The bridge projects YAPYAP's secondary Options and Play panels in front of the
main VR camera. Right stick press directly opens or closes the game's
settings/pause menu; the SteamVR Start action also uses that shortcut. The
Quest 2 test confirmed readable Play text and a visible, selectable pause
menu. The native hands now receive controller position and rotation through the
game's existing arm IK. Controller hand alignment and held-item movement still
need a headset check. Finger animation remains provided by the game.

`0.3.22` samples both controllers and the HMD from the same OpenVR tracking
space and applies game yaw once. It overrides only the local player's native
hand targets during IK, then restores the game's targets and weights. A second
solve after the game's late chest/shoulder adjustment preserves rendered wrist
placement. Dead, ragdolled, climbing, mantling, emoting and polymorphed states,
dashboard focus, disconnected devices and invalid poses defer to native
animation. Existing grip, inventory, casting and menu buttons are unchanged.
Native prop muscles remain responsible for held items; no item reparenting or
multiplayer ownership changes are introduced. `TrackedHands` configuration has
an enable switch plus left/right grip-position and wrist-rotation offsets.
This does not add bare-hand tracking or new two-handed spell gestures.
Pose conversion, yaw, recentering and invalid/stale-pose gates have unit tests;
native rig and held-item behavior require the live headset check. Runtime logs
report local rig binding, tracking availability and hand-to-target distance.

`0.3.28` introduced reversible local clothing performance options in
`BepInEx/config/avrcade.yapyap-vr-controls.cfg`:

- `Performance / StableLocalClothing = false` (current default): preserves
  normal cloth flutter. Set `true` to stop MagicaCloth simulation on the local
  VR avatar while keeping the garment mesh and animated arm rig.
- `Performance / ReduceLocalClothShadows = true`: disables shadow casting on
  local cloth meshes, including skins using the cloth's root bones.
- `Performance / LogInitialFrameTimes = true`: records twelve five-second
  game frame-time summaries after entering a level. CPU/GPU samples are included
  only when Unity supplies them; this does not measure headset compositor FPS.

Other players, gameplay physics, controller mappings and the established wrist
solve are unchanged. Original simulation and shadow settings are restored when
VR control stops or the local avatar changes. Set either clothing option to
`false` and relaunch to restore that behavior. Controller-family detection for
custom analog mappings is cached rather than repeated every frame. The build
and existing controller/hand tests pass; sleeve appearance and any frame-rate
improvement require a connected-headset comparison.

`0.3.29` restores cloth flutter by default, retaining the separate cloth shadow
optimization. It returns the hand reference from 70% of wrist-to-middle-knuckle
distance to the palm midpoint: the former placed that midpoint about 2 cm behind
the physical grip. Wrist orientation and button assignments are unchanged.
The extra final hand solve now runs once just before rendering, after UUVR's
last headset update. HMD/raw controller poses use `GetLastPoses` and grip actions
use `GetPoseActionDataForNextFrame`, which Valve documents as matching the same
`WaitGetPoses` frame. Valid stereo eye centers anchor the hands to the displayed
view instead of an offset camera transform. This replaces the former extra
late-update solve rather than adding another full IK pass. Set
`TrackedHands / RenderSynchronizedHands = false` to use the previous solve
timing. The build and 77 core tests pass; physical palm alignment and motion
still require headset confirmation.

`0.3.30` adds `ClothComfort` settings for the local robe/sleeves and cloak/cowl.
The stock garments have gravity 5, damping 0.1 and full movement inertia;
cloak wind influence is 1.2. Comfort mode keeps simulation enabled with gravity
9.81, robe damping 0.35, cloak damping 0.45, movement inertia 0.35 and wind
influence 0.2. Sleeves use world-down gravity. The cloak uses a normalized
downward/rearward direction (`HoodBackwardPull = 0.45`) following horizontal
body heading, so free fabric tends to settle behind the avatar. Direction
updates are limited to 10 Hz and parameter writes require a meaningful change.
This biases simulated cloth; it does not move the head, hands, garment anchors
or rigid bodies. Hat/ear spring cloth is excluded. Original serialized and
animation-property values are restored when VR controls stop or the local
avatar changes. Set `ClothComfort / Enabled = false` and relaunch to restore
stock garment physics. `StableLocalClothing` remains false by default, keeping
flutter. Visual clearance around the eyes still requires a headset check.

The `0.3.30` headset check found gravity/damping insufficient: sleeves and the
cowl still covered the eyes despite the parameters applying. `0.3.31` adds
`ClothComfort / HeadClearance = true`: a 0.20 m MagicaCloth sphere follows the
eye center (4 cm forward) and is registered only with local robe/cloak cloth.
It has no Unity gameplay collider. The four `CL-Robe-Collar.*` cosmetic anchors
also move 10 cm backward and 6 cm down relative to body heading, folding the
cowl's attached rim away from the face. No head, wrist, shoulder or item bone
is repositioned. Pose offsets are reapplied from saved originals, not added
each frame. Collar positions, collider registrations and the owned object are
restored/removed when VR controls stop. Radius and collar back/drop offsets
are configurable; disabling `HeadClearance` restores the prior behavior.
Flutter remains active. Build/regression checks do not establish physical
eye clearance; that requires the next headset test.

The user confirmed `0.3.31` improves sleeves, but backward walking still swings
the cowl into view. `0.3.32` retains the sleeve settings and adds a cloak-only
motion limit: `HoodMaxSwingDistance = 0.06` meters from its animated folded
shape, with full constraint stiffness. `HoodMovementInertia = 0.05` reduces
movement-driven forward swing for the cloak only. These use the existing
MagicaCloth motion constraint, restore the original values on Suspend and keep
limited flutter. The sleeve inertia remains 0.35. Backward-walking clearance
requires a headset test.

The `0.3.32` headset check still found eye obstruction. `0.3.33` adds
`ClothComfort / HoodEyeFade = true`, using the game's existing
`_CAMERADEPTHFADEALPHA_ON` shader feature on the local cloak/cowl and hat.
The native `Clothing_FPSOpacityMask` only covers selected texture regions;
these two accessories now use a uniform white fade mask and retain their
native depth-fade length/offset. Only the game's per-renderer runtime material
instances are changed. Robe/sleeve and hand materials, shared assets, cloth
physics and controller tracking are unchanged. Original keyword, toggle and
mask restore when VR control stops. This is a rendering clearance measure;
it does not further constrain flutter. Physical clearance remains unverified.

The small pink lobby artifact persisted with `HoodEyeFade = false`, so its
cause is still unresolved. `0.3.34` adds optional
`Diagnostics / CaptureLobbyMenus = false` (enabled only for the current local
diagnostic run). It saves one title and one save/lobby capture per launch,
plus the existing first gameplay HUD capture, under
`BepInEx/diagnostics/yapyap-ui`. Reports include UI shader support, texture
names, screen bounds and active cameras; a separate game backbuffer image is
saved when available. No material, shader, menu selection or layout is changed
by this diagnostic. Capturing/PNG encoding can cause a one-time frame hitch.

`0.3.35` corrects the title capture timing: wait for active VR controls and at
least five seconds after scene load. The supplied screenshot places the pink
square in the 3D title scene below Quit, so title/lobby reports now also list
world renderers in the main camera's view, their material/shader support,
textures, keywords, bounds and projected centers. Reports are saved before
the optional game-backbuffer read. No scene material is changed by this build.

`0.3.36` fixes diagnostic `_MainTex` errors: query texture properties only
after `HasProperty`, including `_BaseMap` when available. Main-camera lookup
also supports the title scene before `CameraManager.MainCamera` exists.
Reports include procedural GPU fairy/butterfly materials, which do not appear
in Unity's normal renderer list. With `CaptureLobbyMenus` enabled only, the
diagnostic `BepInEx/diagnostics/yapyap-ui/isolate-menu.txt` may contain `boids`
or `outline` to pause that decorative menu subsystem for an A/B check.
Write `none` or remove the file to restore; originals also restore on scene
exit or plugin destruction. No mesh, shader, button, gameplay effect or
controller mapping is modified. No pink-square fix is claimed yet.

Both the fairy and title-outline isolation tests left the pink square visible.
`0.3.37` extends the same temporary menu diagnostic: `aux` excludes only
`UICamera` and `RaycastCamera` from URP XR rendering while keeping those cameras
and their raycasters enabled; `volumes` pauses the Menu scene's volume
components. `feature:<exact renderer feature name>` pauses that one active
feature on the main camera's renderer. The report lists those names and camera
XR settings. Every original flag restores on `none`, scene exit, or plugin
destruction. These are diagnostic comparisons, not a verified artifact fix.

The full-screen pass comparison also left the pink square visible. The new
screenshot shows a square with a circular opening, matching the geometry of
an XR hidden-area mesh. `0.3.38` adds diagnostic mode `occlusion`: Harmony hooks
both URP `XRPass.RenderOcclusionMesh` overloads, logs the actual mask material,
shader, view count, scale and mesh bounds once, and skips only those draws in
the Menu scene during this comparison. It does not change scene materials,
tracking, stereo views, volume/renderer-feature settings or input. Write
`none` to restore the normal mask draw. Mask skipping may increase GPU work
outside the visible lens region; this diagnostic does not claim an FPS gain
or a confirmed artifact fix.

The user confirmed the pink square disappears and the stereoscopic title menu
still works when the unsupported XR mask draw is skipped. The runtime material
`Hidden/Universal Render Pipeline/XR/XROcclusionMesh` reports `supported=False`.
`0.3.39` makes this a compatibility guard with
`Rendering / SkipUnsupportedXrMask = true`: both XR draw overloads skip only
unsupported or Unity error shaders, in menus and gameplay. Supported mask
shaders retain the normal draw. Reflection handles and the shader support/name
result are cached; the guard allocates no per-draw collections or strings.
It works with lobby diagnostics disabled and no isolation request. The local
diagnostic captures are turned off after installation, and all earlier scene,
camera, volume and renderer-feature tests are restored. Skipping an unusable
mask may increase GPU work outside the visible lens area; no FPS gain is claimed.

`0.3.23` corrects the hand tracking problems reported in the `0.3.22` headset
test. The stock WizKid arm mappings have weight 0.5, leaving wrists blended
toward the flat animation even when the hand effectors have weight 1. Tracked
hands now temporarily use arm mapping weight 1, restoring the native value
after each solve. The mapping and effector must both end at the referenced
wrist. Wrist-axis alignment uses normalized finger directions; the previous
cutoff rejected this avatar's small geometry and fell back to zero alignment.
Regression tests use the actual serialized finger offsets and rotated avatars.
Rendered-wrist diagnostics record target distance and active mapping weight.
The top-left timer moves into the central upper-left area, and task notification
messages move inward toward center. Their artwork, queue, fades and vertical
layout remain intact. The revised wrist/held-item feel and HUD placement still
need a headset check.

`0.3.24` corrects the reversed wrist roll reported in the `0.3.23` headset
test. The previous basis used the palm normal as the controller's upper side;
the corrected cross-product order aligns the back of the hand to that side
while preserving the verified wrist position and finger-forward direction.
A left/right orientation regression test checks the upper side under controller
rotation. The timer and compass now form one row centered at the top of the
view, with a shared top edge and a gap between their visible artwork. This
supersedes the previous compass alignment to the inventory edge. Task
notifications are centered by their active entries' bounds and moved below the
top HUD/dialogue. Native pooling, fades and vertical entry layout are preserved.
The corrected wrist orientation and exact HUD comfort still need headset
confirmation; the prior runtime recorded zero wrist-to-target position error.

The user confirmed `0.3.24` fixes the wrist twist, but reported the visible hands
extend beyond the physical controller grip. `0.3.25` preserves that rotation
and places the native hand grip attachment at the tracked controller origin.
Previously the controller origin was applied directly to the wrist joint,
leaving the avatar's palm and fingers beyond it. The authored left/right Prop
Muscle Target attachments supply the grip offsets; the wrist target subtracts
the offset transformed by the corrected rotation and current avatar scale.
The original IK targets, weights and item ownership are still preserved.
Regression tests use the exact attachment offsets for both hands at two avatar
scales. A bounded final-render log compares wrist/grip positions and the stereo
eye center with the camera transform. The visible alignment and held-item feel
still require the next headset check. Top-center HUD placement is unchanged.

`0.3.26` replaces the previous outside-palm item attachment with the anatomical
palm midpoint between wrist and middle knuckle. Inspection of the Body skin
shows the old attachment lies outside the visible palm. Position now comes
from SteamVR's explicit `handgrip` pose for each controller, rather than its raw
device origin. Raw controller rotation is retained to preserve the twist fix
already confirmed by the user. Invalid/inactive grip poses defer to native
animation rather than using an incorrect raw position.

Quest left X and Index left A now duck. Right B grabs on both; it goes back in
menus and stores in the inventory. The left lower button closes inventory.
Right A still jumps/confirms/stores, left trigger toggles inventory, and right
stick press pauses. The grip+right-B pause chord was removed so grabbing while
squeezing cannot open pause. Knuckles uses the same action outputs as Quest,
with its native thumbstick paths and force-based grip button sources. The
app's front page and Controls tab use the same annotated diagrams, including
corrected Index A/B stamps and callouts. Runtime mappings and diagram labels
are checked together. Index hardware and revised palm placement require
physical headset confirmation; configuration checks are not that confirmation.

`0.3.17` keeps the normal gameplay HUD visible, including inventory, map and
input prompts when the game displays them. The inventory HUD is enlarged and
centered below the crosshair; interactive inventory opens at the center of view.
Closing inventory selection restores movement while its HUD remains visible.
Settings and the captured HUD are anchored again after the tracked MainCamera
pose updates, preventing render callback order from leaving them above/below
the headset view. No additional VR camera is created.

The actual `0.3.16` inventory capture had no transparent pixels. `0.3.17`'s
final-blit alpha override did not solve it; that override was removed. The new
capture showed full-screen dither behind the HUD. The default world renderer
contains Fullscreen_Simple_Dither, which runs independently of the post-processing
flag. `0.3.18` selects the game's existing PC_Renderer_Empty only for the UI
capture camera. Its renderer list is checked by asset name and empty feature list;
the world keeps its renderer and the UI keeps its actual artwork and alpha.
An automatic gameplay capture records alpha and camera-relative placement.
Transparency, menu comfort and continuous HUD behavior still need a headset check.

`0.3.19` places the health/stamina orb group directly to the right of the
centered inventory row, preserving the orb artwork's original size. The two
groups share a vertical center in the normal HUD and inventory selection mode.
Capture diagnostics wait for the loading fade and include canvas children that
retain their original layer, so early loading backgrounds do not confuse HUD
transparency verification. The user confirmed the move from bottom right to
bottom center improves inventory placement; the adjacent orb placement needs
a headset check.

`0.3.20` moves the night/quota/coin and compass group from the top right to
the top center. The existing artwork keeps its original size and internal
layout, with a small inset below the top edge. Final headset placement still
needs confirmation.

`0.3.21` aligns the compass artwork's right edge with the passive inventory
row's right edge, keeping that top position fixed while inventory selection
opens. Spoken subtitles move from the bottom to the top center below the top
HUD; their original font and fades are preserved. A bounded diagnostic records
up to 32 UI panels that fade out within one second of appearing, including the
caller and input state, to investigate the reported merchant panel flash.
The merchant behavior is not yet a verified fix.

`0.3.16` explicitly detaches MainCamera's saved flat render target during
the early scaler bypass. The title and gameplay scenes serialize this target;
disabling the scaler before its own render texture exists leaves it attached.
UUVR then skips MainCamera and the headset shows the UI camera's yellow view.
The shared texture asset is not destroyed, and auxiliary camera targets remain
attached. This corrects the startup regression introduced in `0.3.14`.

`0.3.15` records one inventory UI capture per game session under
`BepInEx/diagnostics/yapyap-ui`. Its PNG preserves the capture texture's alpha;
the accompanying report records alpha counts, the projection shader and active
UI graphics. This is a diagnostic for the remaining black rectangle, not a
confirmed rendering fix. It does not change UI content, camera or controls.

`0.3.14` disables the pixel-scaled flat image during scaler initialization,
before its first render, including when controllers have not connected yet.
The Unity XR fallback now uses the same Quest face-button mapping as SteamVR.
While the virtual pad is active, headset scene input owns gameplay focus;
the SteamVR dashboard still suspends it. Background-input settings restore
when the bridge stops. Transition logs identify input source, focus and menu
locks. Startup flash and interrupted-session recovery need headset confirmation.

`0.3.10` pairs YAPYAP's player input with the VR controllers by device, not
only by control-scheme name. Earlier builds skipped pairing whenever the
player already used the Gamepad scheme, so a physical pad, or a virtual pad
replaced after the controllers dropped out, could hold the input while the
headset view still worked. While the bridge is active, PlayerInput no longer
switches schemes on its own; the game's own setting is restored when the
bridge stops. The pairing code checks the installed Input System's void-returning
switch method by verifying the paired device after the call.

`0.3.11` aligns local-player selection and item aiming with the last rendered
headset camera pose. It leaves camera rendering and yaw unchanged and falls back
to the game camera if the pose is stale, the camera changes, or the bridge stops.
It adds grip bindings and a controller inventory mode. These new aim, grip and
inventory behaviors still need headset confirmation.

`0.3.12` configures UUVR's existing UI capture camera with transparent clearing,
HDR off and an explicit URP camera component with post processing disabled.
The plain camera's URP fallback otherwise enables post processing, which can
discard the captured alpha and put a black plane behind the inventory HUD.
This changes only the UI capture path; the world camera and thumbstick menu
shortcuts keep their existing behavior. The transparency fix needs a headset check.
The AVRcade YAPYAP front page now shows the same Quest controls as the Controls tab,
including hand/button positions, inventory actions and optional chord caveats.

`0.3.13` swaps the two left-hand shortcuts: squeeze left trigger to toggle
inventory; click the left thumbstick for ragdoll. Tilting the stick still moves
or selects inventory items. Trigger hysteresis prevents small squeeze jitter
from repeatedly toggling inventory. SteamVR and Unity XR fallback use the same
shortcut helper. The YAPYAP front page now has controller illustrations with
leader lines and action labels. At that version only Quest actions were configured;
`0.3.26` adds the matching Valve Index bindings and diagrams described above.
The new shortcut swap still needs a headset check.

## Quest and Valve Index controls

Look at the item with your headset to select it. Hands receive tracked position
and rotation; item selection continues to use headset aim.

| Control | In gameplay | While inventory is open |
|---|---|---|
| Right grip / right B | Pick up or hold/interact | Store the right-hand item once per press |
| Quest left X / Index left A | Duck | Close inventory |
| Left grip | Use the held item | Suppressed |
| Left trigger | Open inventory | Close inventory |
| Left stick press | Ragdoll | Ragdoll |
| Left stick | Move | Left/right: cycle and retrieve items; down: switch hands |
| Right stick | Turn | Turn |
| Right A | Jump; submit in menus | Store the right-hand item |
| Quest Y / Index left B | Drop item | Drop item |
| Right trigger | Fire | Fire |
| Right stick press | Open/close settings/pause | Close inventory and open pause |

Center the left stick between inventory selections. To store a left-hand item,
switch hands first. Inventory closes on pause, loss of game focus or controller
connection, and when leaving the local player. The table describes the SteamVR
Quest and Index routes; XR fallback uses the same face-button action helper.
In menus right B goes back. Grip + right B does not open pause.

AVRcade refuses **VR only** and **VR + mods** until SteamVR is running. The
bridge selects OpenVR, so Virtual Desktop's OpenXR runtime alone leaves the
game on the monitor.

## Build

Build against the local YAPYAP install and BepInEx 5 files:

```powershell
dotnet build .\YapyapVrControls.csproj -c Release -p:TargetGameDir="W:\SteamLibrary\steamapps\common\YAPYAP"
```

The plugin is compiled against the installed game assemblies and is not
redistributed with Unity or YAPYAP binaries.

## Local headset-test package

`prepare.ps1` stages BepInEx 5, a locally built UUVR modern-Mono package, and
the AVRcade bridge under the ignored `artifacts/pre-headset` folder. `Stage`
requires the three local dependency paths, checks UUVR's Git revision, and
records SHA-256 hashes. `Install` refuses to overwrite differing game or mod
files and records exactly what it copied in
`YAPYAP/BepInEx/avrcade-yapyap-vr-install.json`. `Rollback` removes only
unchanged files listed in that record. Run the script with process-scoped
`-ExecutionPolicy Bypass` if PowerShell blocks an unsigned script on a network
drive.

```powershell
# [PowerShell] Use absolute local paths for your own BepInEx pack and UUVR build.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\prepare.ps1 -Action Stage -BepInExPackRoot '<BepInExPack root>' -UuvrBuildRoot '<UUVR build root>' -UuvrSourceRoot '<UUVR source root>'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\prepare.ps1 -Action Install
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\prepare.ps1 -Action Rollback
```

To update an existing install to a new bridge build, close YAPYAP and
double-click `update-vr-plugin.cmd`. It builds the plugin and runs
`prepare.ps1 -Action UpdatePlugin`, which replaces the two AVRcade bridge
DLLs and the Quest SteamVR binding after checking all three installed hashes, then updates the
install record. BepInEx, UUVR and their configuration stay as installed. Pass
the game folder as the first argument if YAPYAP is not in
`W:\SteamLibrary\steamapps\common\YAPYAP`.

After install, AVRcade checks the recorded files before offering **VR only**.
VR Only starts SteamVR when needed and launches directly without r2modman.
AVRcade refuses to change modes while YAPYAP is already running.
The **VR + mods** button opens the selected manager first, without starting
SteamVR and taking over the headset during profile selection. Choose the
profile and use Start Modded; SteamVR is still required for the VR game.
Its profile must contain
UUVR and the AVRcade bridge because r2modman redirects BepInEx to the selected
profile; an empty manager profile loads no VR plugins. On this PC the prepared
profile is **AVRcade VR**. The manager log confirms both plugins load after
**Start Modded**; headset confirmation for this route is still pending. An
additional game mod is optional for the manager-mode success criterion.

**Mods, no VR** opens a separate manager profile without UUVR or the AVRcade
bridge. On this PC, **AVRcade Desktop Mods** contains BepInEx and the pinned
QuotaDifficulty 1.0.0 test mod at its default Original preset. **Vanilla**
starts through Steam with Doorstop disabled using both supported command-line
switch spellings. The process command line and unchanged BepInEx logs confirmed
the loader stayed off in a local desktop test. The manager desktop route still
needs a complete launch check.

For a one-extra-mod test, `extra-mod-test.ps1` toggles the staged
[QuotaDifficulty 1.0.0](https://thunderstore.io/c/yapyap/p/psh_games/QuotaDifficulty/)
DLL. Its default `Original` preset does not change the quota. The package ZIP
had SHA-256 `A94CCF3F474A2B5AF276DAE1A15AC82D2C550DABF7F974210668B247C49384EB`,
and the staged DLL is pinned to
`C59215F5A72A697386808043BD46D3EE516A2EB82406CB734C39AE037B7062C5`.
With that DLL present, AVRcade's **VR + mods** button starts the installed Steam
game using its game-folder VR package. Removing the DLL restores the **VR only**
button. The toggle refuses to replace or remove a changed DLL.

```powershell
# [PowerShell] Run from mods/yapyap-vr on this prepared PC.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\extra-mod-test.ps1 -Action Status
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\extra-mod-test.ps1 -Action Install
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\extra-mod-test.ps1 -Action Remove
```

A headset launch with QuotaDifficulty at `Original` loaded all three BepInEx
plugins: the controller bridge, QuotaDifficulty, and UUVR. The user confirmed
stereo, head tracking, menu input, movement, interaction, and right stick
turning in a level. The right-stick press shortcut and projected Options and
Play panels were subsequently confirmed in the Quest 2. Spell interactions
still need a focused headset check.
The first launch can rewrite `BepInEx/config/BepInEx.cfg`, so AVRcade checks
that it exists but does not require its original hash. Rollback preserves a
changed config file.
The game-folder package stays installed for VR only. Vanilla disables its
Doorstop loader for that launch; desktop mods use a manager profile with no VR
plugins. Changing profiles while the game is still running does not relaunch
it with new flags, so AVRcade blocks that mode switch.

The Quest 2 test exposed two YAPYAP-specific problems: its PixelPerfectScaler
sent the main camera to a flat render texture, and Unity's XR input features
did not report Quest button or stick values. The plugin now removes that flat
image and reads UUVR's bundled SteamVR action bindings. YAPYAP gameplay
also needed its camera yaw restored after UUVR applied the headset pose.

## AVRcade custom controller guides (0.3.27)

AVRcade now saves controller permutations separately for each Windows user, game, and Quest/Index family. Select a visual callout and choose an action; reset restores this controller only. The bridge reads generated SteamVR manifests without modifying the shipped defaults. Grip/trigger swaps convert the grip's boolean value and the trigger's analog value in the reader. The anatomical reference moves from halfway to 70% of the wrist-to-middle-knuckle vector (about 2 cm toward the knuckles), preserving the confirmed wrist rotation. Physical confirmation of this revision remains pending.


## Confirmed 0.3.39 headset checkpoint

Quest 2 through Virtual Desktop and SteamVR: title square removed with the
unsupported XR mask guard; normal AVRcade VR Only launch verified with lobby
diagnostics off. In gameplay, restoring `ClothComfort / HoodEyeFade = true`
kept the hat/cowl clear during backward walking and head turns. Cloth flutter
remains enabled. Keep `Rendering / SkipUnsupportedXrMask = true`. Existing
profiles with `HoodEyeFade = false` must set it back to `true` and relaunch.
Index controller bindings remain source verified; physical Index testing is pending.

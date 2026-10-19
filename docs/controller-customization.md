# AVRcade controller customization

## Use

Open a game, choose its Quest or Valve Index tab, and select a callout on the
controller picture. Choose an action in the picker. Its previous assignment
swaps places, so no control is silently lost. Settings save immediately and
apply on the next VR launch. The selected controller tab is remembered too.

Reset restores the selected game/controller only. The other controller family
and other games keep their saved settings. Preferences belong to the current
Windows user: `%LOCALAPPDATA%\VRClient\controllers\<game>\<device>.json`.
Reference JSON and original binding templates stay unchanged. Fixed native manifest configuration can be redirected to generated copies; its original bytes are backed up privately and restored on reset.

Face buttons and stick presses can swap on the same hand. Triggers and grips
can swap on the same hand. Stick tilt, physical hand identity, poses, touch,
haptics and system buttons keep their original bindings. Game prompts may
still name a default button; follow the customized picture.

## Runtime adapters

`ControllerCustomization` owns validation, permutation swaps, per-user storage,
reset and the effective guide. `ControllerEditorViewModel` drives the pictures.
`ControllerRuntimeBindings` compiles preferences and stages the chosen adapter.
`ControllerBindingAdapters` registers game-specific input formats; the editor
does not depend on a game engine.

| Route | Application |
| --- | --- |
| OpenXR games | A Windows x64/x86 API layer remaps suggested input paths for the exact configured executable and matching controller profile. Unmatched apps pass through. Data is loaded at instance creation. |
| Big Walk / OpenVR | Its native/preloader manifest points at generated binding copies. Source binding templates stay unchanged; resetting both controller families restores original manifest bytes. Selected manager profiles are handled independently. |
| YAPYAP / OpenVR | Generated manifests/binding copies are loaded by bridge 0.3.27. Its reader converts boolean grips and analog triggers when swapped. Shipped files remain unchanged. |
| GTA IV / OpenVR | Generated per-game manifests plus the AVRcade companion's grip/trigger conversion. |
| GTA V / GTAVR | Native version 3–6 input binding profiles, verified from the pinned installer's static IL/default arrays. Private Quest/Index preferences are staged for one session; a helper restores the original native file after exit or a failed/cancelled launch. Keyboard, wheel/gamepad settings and analog flags are preserved. The independent both-secondary weapon-wheel chord stays on physical secondary buttons. |

The GTA V helper journals before staging, verifies original/applied hashes,
restores original bytes, archives valid native in-game edits privately, and
preserves unrecognized foreign edits rather than overwriting them. Its state
and restore result are under `controllers\gta-v\native-lease.json*`. A leftover
journal is recovered or reported before another customized launch.

The optional OpenXR registration is under the current user's Khronos layer key.
It registers support, not shared Quest/Index preferences. Routes and settings
remain game-specific. x64 and x86 manifests use distinct names; each process
loads the compatible DLL. Test runtimes/layers are never packaged.

VR launch hooks cover the ordinary four-mode flow and the separate Fallout
buttons. Monitor launches retain their existing input paths. A mod-manager
profile must contain the current YAPYAP bridge to use its saved mappings.

## Add a game

1. Add its normal catalogue record and optional authored
   `config/controller-maps/<game>.json` guide with Quest/Index devices.
2. Standard OpenXR input needs no new editor or storage code. Confirm its exact
   executable path(s), suggested controller profile paths and action types.
3. A different input format adds one `ControllerBindingAdapter` registration.
   Stage copies or a reversible native profile; do not edit another game's
   preferences. Return a clear failure if the format is unsupported.
4. Verify action swaps, reset, saved reopening, cross-game/family isolation,
   runtime transforms and actual headset behavior.

If no authored guide exists, the picture names the existing physical/default
inputs. It does not invent action names or claim headset verification.

## Evidence for 2026-10-14

Headless UI checks exercised both diagrams via actual callout click and picker
events, persistence/reopening, another game's unchanged bindings, and reset.
All 14 catalogue entries received independent editor checks. Core tests cover
storage isolation, malformed data, runtime binding transforms, manifest copies,
exact executable scope, adapter registration, and native session recovery.
Official OpenXR loaders exercised the layer -> fake-runtime path in both x64
and x86. Fake runtimes are fixtures, not headset validation.

YAPYAP 0.3.27 retains the confirmed wrist rotation and moves the anatomical
reference from 50% to 70% of the wrist-to-middle-knuckle vector (about 2 cm).
The new hand position and customized in-game mappings still need physical
headset confirmation; no all-game hardware pass is claimed.

## Selected reset defaults

The front-page editor saves current assignments automatically. **Save as my defaults** snapshots the current mapping separately under `controllers/<game>/defaults/<device>.json`. **Reset to my defaults** reloads that snapshot; with no selected default, reset uses the original mapping. **Restore original bindings** sets the active mapping back to upstream defaults while keeping the personal snapshot. Both current and default mappings stay isolated per game and controller family. These are application preferences; upstream packages remain unchanged.

The Cyberpunk 0.1.7 guide was compared with the official tagged README and OpenXR suggested paths. It includes scanner context, the cyberware chord, overlay chord duration, and melee power on right grip. Stick tilt and physical gestures remain upstream behavior. Diagram instructions show the selected mapping and remapped combination controls; in-game rebinding may still change the gamepad actions underneath these defaults. Headset validation of custom layouts remains pending.

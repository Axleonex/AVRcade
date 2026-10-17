# Backrooms controller hands

The [2026-10-17 checkpoint](BACKROOMS-TESTED-2026-10-17.md) includes the accepted
Quest arm alignment and automatic recovery. Valve physical hardware remains
unverified. Semantic left/right hand IDs 0/1 support UEVR's OpenXR and OpenVR
controller poses without assuming runtime device indices.

AVRcade embeds `backrooms_hands.lua` and installs it into Backrooms' imported
profile before launch. It creates an owner-only poseable copy of the local
player's animated arms, with separate transient controller anchors. Wrists stay
at controller goals; finger and item animation are inherited from the source.

Reviewed flat-screen FOV arm materials are replaced on the copy to prevent
visual distortion. Shoulder and clavicle roots use the reviewed Hazmat body's
Spine2 reference frame, avoiding shoulder sockets that the game zero-scales to
hide its original body arms. Unsupported meshes retain their original rendering.
The local torso and legs use `OwnerNoSee`; collision and remote players are
unchanged. The driver does not continuously rewrite camera calibration.

Temporary invalid poses suspend the copy and retry after a bounded 0.25 to 5
second cooldown. Healthy components are retained and hidden source animation
continues updating for recovery. Tracking loss, disable, pawn replacement and
reset restore original visibility and animation tick settings. UObject changes
run after the engine tick using the last completed stereo update, so one frame
of tracking latency is possible.

## Settings and rollback

Profile file `data/avrcade-backrooms-hands.json` defaults to:

```json
{"enabled":true}
```

Set `enabled` to false with the game closed, or use **AVRcade Backrooms hands**
in UEVR's Script UI. AVRcade preserves this preference across launches. Missing
or malformed settings disable the driver. Removing the script alone is
temporary because the launcher deploys it again.

The launcher preserves externally edited scripts. An update to a prior owned
script creates a UUID `.bak`; its ownership hash is stored in
`.avrcade-backrooms-hands.json`. Status is available in the UEVR log and
`data/avrcade-backrooms-hands-status.txt`; entered text is not recorded.

Regression tests cover independent hands, rotated/long/folded reach, exact
wrist goals, body alignment, temporary pose and allocation failures, preference
errors, source restoration, external edits and isolation from other games.

# Backrooms keyboard, menus and HUD

The [2026-10-17 checkpoint](BACKROOMS-TESTED-2026-10-17.md) was accepted on Quest.
Install the native keyboard and four comfort scripts using that guide before
launching a fresh source build. The scripts run only against reviewed Backrooms
widgets, materials and the local player.

## Keyboard and popups

The compact QWERTY keyboard opens for reviewed name/password prompts. It supports
letters, digits, spaces and `-_.`; the game retains validation of the field.

| Control | Action |
|---|---|
| Left stick | Choose a key |
| Right A | Type the selected key |
| Right B or bottom-right **DONE - Right B** | Commit text and close |
| Left lower face button | Erase one character |
| Left upper face button | Shift while typing; reopen on a name prompt |
| Right A or Right B on a single-button informational popup | Acknowledge it |

Quest left lower/upper are X/Y; Index left lower/upper are A/B. These use UEVR's
default bindings; custom SteamVR bindings can differ. The visible **Open keyboard**
hint also reopens a keyboard that was closed on a name prompt. Multi-choice
dialogs retain normal selection; the helper does not change crossplay settings.

Character delivery goes to this game's Windows/Slate input handling. The native
plugin targets only Backrooms' window, including when a headset session leaves
another application in desktop foreground. Clear requires game foreground.
Focus and Enter commit are sequenced across engine frames before the overlay
closes, and held closing inputs drain before gameplay input resumes. Physical
Enter uses the same finish sequence while the overlay is active. **Keys sent**
reports delivery, not the complete field contents or successful game validation.
Password previews are masked and entered text is not logged.

Both-stick UEVR settings opening is disabled for this game to prevent accidental
camera adjustment. Desktop Insert still opens UEVR settings deliberately.

## Stationary stereoscopic menu rooms

`backrooms_menu.lua` holds the title/lobby room and active interactive menu's
aiming reference throughout both eye renders. Turning the headset looks around
the room in stereo. Normal gameplay aiming resumes after menus close. The
persistent VOIP player list does not activate menu anchoring.

The catalogue and keyboard apply `UI_Size=1.0`, `UI_Distance=1.5`,
`UI_FollowView=false`, `UI_Framework_FollowView=false` and
`VR_DecoupledPitchUIAdjust=false`. Existing in-session tuning is retained after
startup. Recentring can change the standing origin.

## Central gameplay HUD

`backrooms_hud.lua` moves the reviewed sanity, VOIP status and progress-bar
groups toward the viewport center and renders them at 85% of their original
size. It preserves crosshairs, interactive menus, fonts, layout widths and
full-screen effects. Original numeric layouts and scales are restored on
disable, replacement, leaving VR or script reset, without accumulating offsets.

Optional profile file `data/avrcade-backrooms-hud.json`:

```json
{"enabled":true,"inset":0.40,"scale":0.85}
```

`inset` accepts 0 through 0.45; 0 restores original positions. `scale` accepts
0.75 through 1.25; 1 keeps original rendered size. Delete the preference file
to reset these HUD settings. Invalid settings preserve the last valid layout.

## Lens guard and rollback

`backrooms_lens.lua` suppresses the local player's reviewed VHS barrel-distortion
toggle while VR is active. It preserves other material parameters and restores
its owned changes when disabled or reset.

Close Backrooms before restoring files from the installer's `comfort-backup-*`
directory. Removing an individual comfort script disables that helper; the
separate hand driver is automatically deployed at launch and has its own
[persistent disable setting](BACKROOMS-CONTROLLER-HANDS.md).

# Per-game controller references

Controller references are a VRClient-wide game contract. They are not owned by
Unity, Unreal, REDengine, or any particular conversion backend.

Each supported game may provide `config/controller-maps/<game-slug>.json` using
`config/schemas/controller-map.schema.json`. VRClient loads the file by the same
slug used by the library entry and displays it wherever that game came from.

Bindings use stable spatial slots such as `left_primary`, `right_trigger`, and
`both_sticks`. The `control` field names the physical Quest or Index input. The
`action` field is the important user-facing text and must describe what the
input does inside that game. The controller diagram renders the action as the
headline and the physical input as secondary context.

New game integrations should copy `config/defaults/controller-map.json`, author
one device entry for every verified controller family, and verify each action
against the conversion's actual input translation before changing
`verification_state` from `template` to `verified`. Template maps are deliberately
not shown. An absent or unverified map does not invent mappings: VRClient simply
omits the reference until the adapter has verified data.

`source_verified` maps may also be displayed when the input translation is
confirmed from upstream documentation or shipped bindings. They must include
HTTPS source credit and a visible `note` stating headset-test limitations.
This is not a claim of hardware validation or tracked-hand support.

Big Walk includes the upstream-confirmed Quest Touch and Valve Index actions.
RV There Yet includes UEVR's default Xbox-prompt translations; game-specific
action names and tracked hands remain unverified. Custom runtime bindings can
override these defaults.

For UEVR games, launch installs a text version of the same reference under
`scripts/vrclient_controls.lua` in the game's UEVR profile. Click both sticks
to open UEVR, then expand Script UI and VRClient controls. Nothing opens
automatically during gameplay. Changed guide files are backed up before an
update; camera settings and other scripts are untouched. Big Walk's guide is
currently on its VRClient page only, not an injected in-game overlay.

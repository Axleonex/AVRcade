# Meccha Chameleon — VRClient catalog UEVR profile

> **STATUS: LEGACY UEVR FALLBACK PROFILE.**
> This profile cannot close native `NUE-*` requirements. Current native profile:
> `config/unreal/native/meccha-chameleon.json`.

Deployed by `uevr-launch` into `%APPDATA%\UnrealVRMod\PenguinHotel-Win64-Shipping\`
(copy-if-missing; `--push-profile` forces). UEVR ignores this README.

Set at startup (source-verified key names; combos persist as numeric indices):

- `VR_AimMethod=2` — Right Controller (0=Game, 1=Head, 2=Right, 3=Left,
  4/5=Two-Handed). The game's paint targeting follows the hand, so the game's
  own paint preview marks where strokes land (user directive 2026-07-11).
- `VR_DecoupledPitch=true` — comfort: head pitch decoupled from game camera.

- `scripts/first_person_head.lua` — FIRST-PERSON via head bone, RELEASED in
  paint mode (2026-07-13):
  - **Normal play:** pins the VR camera to the character's head bone every
    frame (first person). Auto-detects the bone name at runtime (socket
    candidates, then a skeleton scan for *head*) and prints the chosen name to
    the UEVR console — once known, pin it as `CANDIDATES[1]` in the script.
  - **Paint mode:** releases the pin so the game's own paint camera frames the
    canvas and you paint like the flat game (head tracking still works; the
    canvas stops riding your head). Controlled by `is_paint_mode()`.
  Two values are discovered at runtime and PRINTED to the UEVR console, then
  locked into the script: (1) the head bone name → `CANDIDATES[1]`; (2) the
  paint-mode signal → set `PAINT.property` (the pawn/controller field that
  turns true / equals a value while painting; `PAINT.on` picks pawn vs
  controller; `PAINT.equals` for enum modes; set `PROBE_STATE=true` to have the
  field's value printed on change). **Until `PAINT.property` is set,
  `is_paint_mode()` returns false and the pin stays ON always — identical to the
  earlier always-on behavior, so this is a safe, no-regression upgrade.**
  Replaces the earlier static VR_CameraForwardOffset/UpOffset seed (static
  offsets can't track a moving character; the bone pin can). UNVERIFIED until
  injection is stable. Product standard: profiles default to FIRST-PERSON.
  HOW TO FIND the paint-mode signal in-headset: inject, open UEVR's UObjectHook
  browser, inspect the local pawn (and its Controller), start painting, and
  watch which boolean/enum flips — set that field name as `PAINT.property`.
- UObjectHook attachments (brush model on hand) — requires runtime object
  paths; captured automatically by `uevr-profile-export` after in-headset
  setup.

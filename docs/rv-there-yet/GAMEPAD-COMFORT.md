# RV There Yet? gamepad and comfort matrix

Status: awaiting headset execution.

For each supported runtime, record pass/fail for world scale, seated height,
recenter, snap turn, smooth turn, vignette, head/body decoupling, UI distance,
on-foot controls, driving, winch, item handling, menus, pause, death/restart,
and controller reconnect. Note any keyboard/mouse-only blocker.

Only export the smallest settings delta after every required action passes from
a clean import. Bind the resulting hashes to the tested game build and UEVR
nightly. Do not copy diagnostic logs or `_EXTRAS.zip` into the shipped profile.

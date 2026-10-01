# RV There Yet? native fallback decision

Decision: continue with the bounded UEVR route; native implementation is not
authorized by current evidence.

An exact config-only community profile exists and targets the correct shipping
process. No headset rung has yet shown a blocker that requires ownership of the
renderer, camera, or input bridge. Building a native Unreal adapter now would
add substantial update and safety risk without a measured gap.

Reopen this decision only if the headset/gamepad gate fails reproducibly on a
fingerprinted build, a minimal reviewed UEVR setting or Lua extension cannot
resolve it, or release performance is unacceptable. Any native path then needs
a separate plan with measurable stereo, camera, input, teardown, update, and
co-op success criteria.

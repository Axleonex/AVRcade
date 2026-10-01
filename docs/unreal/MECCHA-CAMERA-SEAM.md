# Meccha native camera/stereo seam

**Status:** live observation-only provider and continuous orientation-only
camera response are verified. An experimental side-by-side true-stereo payload
is controlled-complete and awaiting its first live Meccha/headset gate. It must
not be described as live stereo yet. Target build
`24517175`, shipping SHA-256
`2192bea467070e9ac051d587a5b2d3afd9fd525580692c361db67a371f0883ce`.

## Evidence and decision

The changed binary contains UE reflection names including
`PlayerCameraManager`, `CameraComponent`, `GetCameraView`, and `LocalPlayer`.
They live in Unreal name/reflection data and have no direct RIP-relative code
references, so treating the ASCII strings as function signatures would be an
unsafe false shortcut.

UEVR is used only as a public-source reference oracle. Its current native
stereo implementation initializes Unreal name/object discovery, hooks
`UGameEngine::Tick`, injects a stereo device, and can hook the
`FSceneView::FSceneView` constructor. The underlying `praydog/UESDK` discovery
submodule is not publicly readable, so VRClient must own an independent
version-pinned discovery path rather than depending on or loading UEVR.

Reference sources:

- https://github.com/praydog/UEVR/blob/master/src/mods/vr/FFakeStereoRenderingHook.cpp
- https://github.com/praydog/UEVR/blob/master/src/mods/vr/IXRTrackingSystemHook.cpp
- https://github.com/praydog/UEVR

## Implemented fail-closed boundary

- `unreal_camera_contract.{h,cpp}` validates exact camera identity, sample
  freshness, finite UE transform, and projection bounds.
- It composes a validated Unreal camera anchor with OpenXR reference/eye poses,
  converting the coordinate systems and producing distinct IPD-scaled per-eye
  camera transforms.
- `unreal_camera_observer.{h,cpp}` rejects missing, ambiguous, invalid, stale,
  or non-monotonic candidates and requires one camera identity across a
  configurable number of consecutive samples.
- Unreal adapter service ABI version 1 carries engine-space camera snapshots.
  The Meccha adapter now stays `SKIPPED` until graphics and a valid fresh camera
  are both ready.

Controlled tests prove neutral IPD translation, 100 UE units per meter,
OpenXR-to-Unreal yaw conversion, stale/non-finite rejection, ambiguous-camera
rejection, monotonic sample enforcement, and adapter pending/ready behavior.

## Live build-pinned provider

`vrclient_meccha_camera_observer_payload.dll` observes Meccha's real
`FSceneView::FSceneView` constructor without patching code or camera memory.
The build pin requires all of the following before one debug register is armed:

- exact shipping SHA-256;
- constructor RVA `0x041A9610`;
- an exact 64-byte constructor prologue;
- both `r.TranslucentSortPolicy` and `vr.InstancedStereo` anchors; and
- exactly 12 direct constructor callers.

The hardware-breakpoint callback copies only the constructor's RDX input into a
fixed atomic ring. A normal worker decodes the build-specific UE 5.6 layout,
requires plausible view-family and scene-view-state pointers, finite transform
and projection data, one recent identity, advancing samples, and three
consecutive valid observations before publishing `camera_ready`.

Meccha build `24508135` inserts additional fields between its two view
rectangles. Constructor reads and a raw capture prove the exact live identity
offsets are view family `+0x158` and scene view state `+0x160`. A regression test
explicitly rejects the packed `1920x1080` rectangle value that exposed the
initial SDK-layout mismatch.

Steam updated the game to build `24517175` during the first stereo launch
attempt on 2026-08-02. No payload attached. Independent file analysis found the
constructor moved by `-0x5C0` to `0x041A9610`; its unique 64-byte instruction
shape retained the `RDX+0x158` and `RDX+0x160` identity reads and changed only two relocation
displacements. Both anchors remain unique and exactly 12 direct callers still
target the constructor. This supports the controlled re-pin, while the existing
build `24508135` live provider evidence remains historical until a read-only
flat run verifies the new build.

Automated live evidence:
`artifacts/meccha-camera-observer/20260801-180847/session-evidence.json`.
The run reached `camera_ready=true`, one recent identity, 13,360 decoded
samples, zero rejected samples, zero capture faults, and zero ambiguous
observations.
It reported a 58.7155-degree vertical FOV, 16:9 aspect, 1 UU near plane, and the
live scene origin. The user confirmed normal flat output and closed Meccha
normally; that human gate is recorded in
`artifacts/meccha-camera-observer/20260801-180847/flat-confirmation.md`.

## Orientation-only payload

`vrclient_meccha_orientation_payload.dll` is a separate payload built on the
proven monoscopic scene-relay transport. It does not change the read-only camera
observer. Before installing its constructor detour, it repeats the exact
shipping hash, RVA, 64-byte prologue, two-anchor, and 12-caller checks.

The payload waits for 30 consecutive decodable samples from one
scene-view-state identity and 60 continuously fresh OpenXR poses before enabling
the first write. A dedicated nominal-100 Hz sampler converts the current Windows
performance counter to `XrTime` with
`XR_KHR_win32_convert_performance_counter_time`, then locates the view space
relative to the local space. Pose publication is therefore independent of eye
swapchain waits and scene-relay rendering. The render callback no longer drives
camera-pose freshness. The first valid OpenXR head orientation becomes the
reference; later poses publish a lock-free relative orientation snapshot. At
the pinned constructor boundary it changes only the nine doubles in the 3x3
view-rotation matrix. Camera location, projection, position tracking, IPD, and
gameplay state are untouched.

Missing, non-finite, future, or older-than-100-ms pose data leaves the complete
incoming scene-view options unchanged. No orientation write occurs during pose
warm-up. After activation, any missing/stale pose permanently latches
orientation writes off for the rest of that game launch;
later poses cannot silently re-enable them. Any identity change, decode failure,
or write-protection mismatch also skips the write. The live runner rejects a
latched tracking fault and still requires 60 consecutive orientation
applications in addition to the existing OpenXR and scene-relay gates.

Controlled tests prove neutral orientation preserves the view, runtime frame
zero is accepted, missing/stale pose does not touch output, position validity is
irrelevant, OpenXR yaw maps to the pinned Meccha view convention, and every byte
outside the rotation matrix remains unchanged. The diagnostic, scene-relay, and
orientation payload variants all build from the shared transport source.

A live camera write ran on 2026-08-01 but failed the human flat-monitor gate due
to blinking caused by alternating fresh writes and stale-pose skips. The run is
recorded at `artifacts/meccha-orientation/20260801-215700`; it must not promote
head tracking. A hardened retest at
`artifacts/meccha-orientation/20260801-221129` held camera writes at zero and the
user confirmed normal flat output, proving the fail-safe but not head tracking.
The artificial freshness gaps were traced to pose publication occurring only
after both eye swapchain waits. An independent current-time sampler corrected
that cadence. The later `20260801-224652` run passed automated readiness and the
user confirmed that head rotation moved the live Meccha camera, but the view
reset after roughly three seconds and desktop movement occurred in intervals.
Final evidence showed 13,223 successful pose samples and zero missing/stale
events, yet the tracking fault was latched. This uniquely identifies the camera
hook's failed tracking-lock `TryAcquireSRWLockExclusive` branch: ordinary
sampler/camera concurrency was misclassified as permanent tracking loss. The
camera hot path now uses `MecchaOrientationTrackingState`: the sampler alone owns
the warm-up gate, while camera threads consume atomically published readiness
and can irreversibly latch a fault without sharing a lock. A concurrency test
proves later sampler publications cannot clear a camera-latched fault, and the
native test passes repeatedly under stress.

The normal-evidence run `artifacts/meccha-orientation/20260802-020822` then kept
8,373 consecutive camera applications with zero missing/stale/fault/rejection
telemetry while the user still saw a roughly five-second reset. The otherwise
identical local-evidence isolation run
`artifacts/meccha-orientation/20260802-021633` sustained 13,701 consecutive
applications and 22,316 successful pose samples with zero faults; the user
confirmed that motion stayed continuous. Periodic synchronous flushes to the
SMB-mounted G: evidence path were therefore the interruption source. Evidence
persistence now uses a process-lifetime, generation-acknowledged dispatcher with
bounded retry and terminal flush; render producers do no file/network I/O. The
dispatcher regression passed 50 repeated runs and GSD convergence is clean.

The normal G:-evidence run
`artifacts/meccha-orientation/20260802-061542` sustained 7,150 consecutive
applications with zero skips/faults/rejections, and the user confirmed seamless
motion with no reset. This closes the persistence-stability gate. The same gate
found yaw direction inverted: turning left moved the start-screen world farther
left. The original zero-base test hid a composition-order ambiguity. The
controlled correction now post-composes the delta in view-permuted space as
`V * (A^T * D * A)`, preserving `R * D * A` for nonzero base pitch/roll. Tests
cover OpenXR +X/+Y/+Z mapping to Unreal +pitch/-yaw/-roll and two noncommuting
base rotations.

The follow-up diagnostic
`artifacts/meccha-orientation/20260802-065232` recorded center near -4 degrees,
a comfortable right hold near +32 degrees, and a comfortable left hold clearly
negative. With broad tolerance for human positioning, this proves correct
left-negative/right-positive runtime and camera signs and rules out another yaw
inversion. The user reported good
pitch and no positional translation, but weaker horizontal quality and UI too
far toward the Quest 2 edges. That matches the current relay shader: it samples
the entire 1920x1080 capture across both complete eye targets and supplies the
same monoscopic image to two different OpenXR projection views. This confirmed
architectural limitation is consistent with, and likely dominates, the symptoms,
but it is not a proven exclusive cause. The next controlled comparison is
FOV-correct, genuinely distinct eye rendering. If residual drift remains,
rotation gain, sampling timing, reference drift, projection mismatch, and lens
behavior remain possible contributors. The
extended run also latched the stale-pose fail-safe after one stale skip at 1,316
applications; that threshold must be reassessed without weakening missing/stale
safety before head-tracking capability promotion.

## Experimental side-by-side stereo payload

`vrclient_meccha_stereo_payload.dll` composes the proven D3D12/OpenXR relay and
orientation seam with Unreal's built-in fake stereo bootstrap. Its Steam launch
uses only per-launch `-emulatestereo`; no persistent game configuration is
changed. The packaged build ignored both correctly formed Game-config settings
and an Engine `r.StereoEmulationFOV` A/B, so the exact-build `FSceneView`
constructor seam now normalizes the known stock projection after stereo identity
readiness. It validates a private copy first and changes only the `m00`/`m11`
angular scales to 110 degrees/1.0. The stock fake-stereo eye separation remains
unmodified and must be judged from live depth/scale evidence.

The payload requires exactly one stable pair of scene-view-state identities;
one identity never becomes stereo-ready and a third identity fails closed. It
also requires the source to match the repeatedly observed
90.2548-degree/1.0468 stock projection (or already be 110/1.0), decodes the
normalized copy, and refuses camera writes/readiness unless the result is within
two degrees of 110 and its projection aspect is within 0.01 of 1.0. Unknown,
non-finite, or unwritable projections fail closed. Existing
missing/stale pose and exact-binary seam checks remain active.

At Present, the relay snapshots the complete engine-generated side-by-side
backbuffer. The left OpenXR target samples only the left half and the right
target only the right half, with half-texel seam clamps. The source-eye aspect is
the decoded/configured Unreal angular projection (1.0), not the 960x1080 packed
texture region's pixel aspect. Raw asymmetric OpenXR FOV angles are mapped into
the symmetric 110-degree source; an eye frustum outside that source fails
closed. After snapshotting, the left source eye is stretched over the game
backbuffer so the physical monitor remains a single view.

The D3D12 GPU regression reads back distinct left/right eye colors and proves
both monitor edges contain the restored left-eye image. Native tests cover the
packed-eye UV contract, projection-aspect preservation, FOV
mapping/containment, exactly-two
identity gate, and projection mismatch rejection. A delayed-attach payload test
also passes. Live questions remain: depth and world scale, stock fake-stereo
eye-separation suitability,
culling/temporal/post-process behavior, HUD/paint mode, and flat stability.

The first build-24517175 live attachment on 2026-08-02 failed closed with two
stable identities but a 90.25-degree/1.0468 projection and zero camera writes.
The Steam command line was intact. A corrected repeated-section Game-config
launch and a separate Engine-console-variable A/B produced the identical
projection and zero camera writes, proving those packaged startup overrides are
ignored. Binary analysis confirmed where the fake device reads its settings,
but late attachment makes its startup constructor the wrong control point. The
per-view normalization fix instead runs at the already pinned, continuously
executed seam, is unit-verified to touch only 16 projection bytes, and re-passed
the complete 14/14 controlled gate. Run
`artifacts/meccha-stereo/20260802-194036` subsequently proved the fix in the live
shipping game with two stable identities, exactly 110-degree/1.0 projection, 80
camera applications, and zero projection/write rejections. The run stopped
before a human headset verdict because a single stale pose irreversibly latched
tracking even though all 859 sampler attempts succeeded.

That evidence distinguishes a recoverable scheduler gap from missing tracking.
The publisher/camera policy now skips stale, future, or missing pose frames,
resets readiness, and requires the complete 60-fresh-sample warm-up before
writes resume. It still permanently latches explicit structural snapshot faults
and preserves the concurrent-fault-wins invariant. Native recovery tests, the
service static guard, and the full 14/14 Meccha suite pass; the human stereo/depth
retest remains pending.

## Preserved observation boundary

The live provider payload now:

1. make no camera or render writes;
2. record the located function/address relative to the module and the exact
   executable hash;
3. publish every candidate identity and reject zero or multiple active views;
4. require consecutive, advancing, finite samples before `camera_ready`;
5. preserve normal flat output and Steam authentication; and
6. fail closed if the binary, signature, calling convention, structure layout,
   or sample cadence differs.

The flat-output, continuous-response, asynchronous normal-evidence, and yaw-sign
gates have passed live checks for the orientation-only lane. The stereo payload
is controlled-complete only; its guarded flat/headset run is the next gate.

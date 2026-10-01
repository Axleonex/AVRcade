# Phase 1 — Core VR Runtime: Troubleshooting & Bug-Checking

Covers `src/native/runtime/`: `openxr/` (session + Vulkan binding), `frame/`
(stereo math, frame loop, test-scene renderer, shaders), `perf/` (profile,
dynamic resolution, foveation, frame pacing), `public/` (the C ABI).

Read the [cross-cutting index](../TROUBLESHOOTING.md) first.

## Real bugs hit here

### Projection-convention mismatch (fixed in `464796c`) — the headline trap
The shader applied `clip = transpose(viewProjection) * v` (row-vector
convention) against `stereo_math`'s **column-vector** matrices, and a prior
commit (B3) had stored the **transpose of the view matrix** in
`makeViewMatrix()` to compensate — which also corrupted the column-vector `V`
handed to the external adapter callback. **Headless Vulkan validation passed the
whole time** (it only checks API usage, not whether geometry lands where it
should). Wrong on a real headset only.
- **Root cause:** matrix convention is a contract spanning `makeViewMatrix` →
  renderer upload → shader. Changing one side without the others (or "fixing" it
  by transposing in one place) silently breaks the others and the external API.
- **End-state (correct):** `makeViewMatrix` returns true `V = [R^T | -R^T p]`
  (rotation block `R^T`, translation `-R^T p` in column 3 = `m[12..14]`, bottom
  row `0,0,0,1`); renderer uploads `multiply(projection, view) = P*V`; shader
  does `clip = viewProjection * v` with no transpose, keeping the GL→Vulkan
  corrections. Net `clip = P*V*v`.
- **Regression test:** `tests/native/runtime/stereo_math_projection_tests.cpp`
  (CTest `vr_stereo_math_tests`). Uses a **non-identity** pose (90° yaw, eye
  `(1,2,-3)`) so identity can't accidentally satisfy it; pins eye→origin,
  dead-ahead→NDC `(0,0)`, and has two guards that fail if the transpose **or**
  the wrong multiply order returns. Math was re-derived from the quaternion
  ground truth independently of the source.
- **How to check a projection change:** pick a non-identity pose, hand-derive
  where a dead-ahead point and an off-axis point must land in NDC, and assert
  the full renderer+shader chain — not just the matrix in isolation.

### Vulkan descriptor-pool re-prepare (fixed in `9311d46`)
Re-running `prepareEyeTargets` (e.g. a resolution change) failed because the
descriptor pool lacked `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT` and
`destroyEye` didn't `vkFreeDescriptorSets`.
- **Check:** any Vulkan object recreated on re-prepare must be freed with a pool
  flag that permits individual frees, or the pool must be reset wholesale. Call
  `prepareEyeTargets` twice in a test/probe to exercise the re-prepare path.

### D3D11 → Vulkan port gotchas (commit `f98733b`)
- `<unknwn.h>` **must** precede `<openxr/openxr_platform.h>` under
  `XR_USE_PLATFORM_WIN32` (the platform header's MSFT Win32 extension decls
  reference `IUnknown`, which `<d3d11.h>` used to pull in transitively). This
  only fails on the **OpenXR-ON** leg.
- Public-API ABI: `VR_RUNTIME_GRAPHICS_BACKEND_VULKAN=2` was added but `_D3D11=1`
  was kept as a reserved value so the ABI doesn't shrink. Don't renumber/remove
  enum values — mirror this when evolving the ABI.

### `assert()` → NDEBUG (converted)
`runtime_profile_tests.cpp` and `state_transition_tests.cpp` (both in the
`vr_runtime_unit_tests` binary) previously asserted via `assert()` → vacuous
under Release. Now use throwing `expect()`. See cross-cutting trap #1.

## Known never-run / watch-items (honest deferrals)

These are **compile/link-checked and/or headless-validated only** — first real
exercise is the user's headset run:

1. **Swapchain image layout transition for present (watch-item #1).** A
   runtime-owned swapchain image has a runtime-defined layout after
   `xrAcquireSwapchainImage`, unlike the self-owned `UNDEFINED`-layout image the
   headless render-validation uses. The render pass `finalLayout`
   (`COLOR_ATTACHMENT_OPTIMAL`) may mismatch what OpenXR expects at release. **#1
   suspect if the first headset run renders wrong or errors at present.**
2. `renderFrame` may release an acquired-but-not-waited swapchain image on the
   wait-failure error path (never-executed code).
3. Headless validation never exercises `image_index > 0` (the dynamic-offset
   uniform path) or multi-eye `prepareEyeTargets`.
4. `createVulkanDeviceForOpenXr` clamps `api_version` to the minimum only
   (ignores `maxApiVersionSupported`); the graphics queue family is chosen by
   Vulkan introspection, not OpenXR designation (acceptable under `enable2`).
5. `predicted_display_period` from `XrFrameState` is not yet populated for the
   refresh budget — the smoke harness derives refresh from the profile instead.

## Hot-path rules (enforced by `hot_path_audit`)

`renderEye`'s HOT PATH section must be allocation-free: command buffers and
uniform memory are pre-allocated in `prepareEyeTargets` (persistently-mapped
HOST_COHERENT uniform via a dynamic-offset descriptor); the per-image fence
wait/reset and the constants `memcpy` live **outside** the HOT PATH markers. If
you add work to `renderEye`, keep allocations/locks/logging/file/blocking out of
the marked region or `hot_path_audit` fails.

## Verification

```powershell
# OFF gate (includes vr_runtime_unit_tests, vr_stereo_math_tests, runtime_profile_schema,
# public_api_contract, hot_path_audit, vr_runtime_hash_tests, runtime_binary_hash_crosscheck)
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\ci\build-and-test.ps1 -BuildDir build\ci
# OpenXR-ON compile+link (the only leg that catches include-order/binding breaks)
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\ci\build-and-test.ps1 -OpenXR on -BuildDir build\ci-on
# Headless Vulkan render-path validation (dev-only, validation layers active)
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run-vulkan-render-validate.ps1
# Just the projection regression:
ctest --test-dir build\ci -R vr_stereo_math_tests --output-on-failure
```

## Bug-checking checklist

- [ ] Any matrix/convention change tested through the **full** renderer+shader
      chain with a non-identity pose, not just the matrix.
- [ ] Both build legs (OFF gate + OpenXR-ON) green.
- [ ] New tests use throwing `expect()`, proven fail-able under Release/NDEBUG.
- [ ] New `renderEye` work stays outside the hot-path markers.
- [ ] No `CORE-*` box ticked without a real headset run + the user's eyes.
- [ ] Re-prepare / resolution-change paths exercised if you touched eye targets.

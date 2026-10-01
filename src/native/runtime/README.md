# Native Runtime Boundary

The native runtime is the only layer that talks to OpenXR. Upper layers consume
`public/vr_runtime_api.h` and must not receive OpenXR handles, OpenXR structs, or
vendor headset SDK objects.

Vendor-specific SDKs are intentionally out of bounds for this module. If a
headset needs support, it must arrive through the system OpenXR runtime and the
standard OpenXR loader path.

Directory responsibilities:

- `public/` - stable C ABI for lifecycle, pose, timing, state, and frame hooks.
- `openxr/` - OpenXR instance/session/spaces/swapchains and Vulkan binding code.
- `frame/` - stereo math, lifecycle transition helpers, and test-scene renderer.
- `perf/` - runtime profile loading, dynamic resolution, foveation, and pacing.

`vr_runtime_sample_head_pose` is the current-time pose path for clients whose
camera update cadence must not depend on swapchain acquisition or rendering. On
Windows it uses the OpenXR performance-counter time-conversion extension and may
run concurrently with `vr_runtime_run_frame` after start. The caller must stop
and join its sampler before `vr_runtime_stop` or `vr_runtime_destroy`.

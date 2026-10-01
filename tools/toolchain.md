# VRClient Pinned Toolchain Manifest (Bolt-on B1, BUILD-01)

Pinned 2026-06-12 from direct probes of this machine (Windows 11 Home 10.0.26200) and the
CMakeCache.txt of the proven validation builds (`build/phase4-validation`,
`build/phase5-validation`, `build/phase6-repo-validation`). Source recon:
`.planning/bolt-on/recon/env.md`.

`tools/setup-build-env.ps1` resolves these tools in this order: vswhere -> the pinned paths
below -> PATH fallback. It imports the MSVC environment via `vcvars64.bat` and exposes
`VRCLIENT_CMAKE_EXE` / `VRCLIENT_CTEST_EXE` / `VRCLIENT_NINJA_EXE` / `VRCLIENT_PYTHON_EXE`.

## Compilers: clang-cl (open compiler) and MSVC — both CI-verified

VRClient builds green under **two** compilers, both proven by the evidence ledger (OpenXR OFF,
36/36 CTest + 22/22 validators each):

- **clang-cl (LLVM 22.1.7)** — the documented **open compiler** (the open-toolchain step). It is a
  first-class, CI-verified, opt-in alternative; `lld-link` is its linker. The build scripts do
  **not** select it by default (they default to `msvc`); pass `-Compiler clang-cl` / `-Compiler auto`.
- **MSVC (`cl.exe`)** — the original toolchain, unchanged, still fully supported, and the script default.

**Latest CI verification (commit `c4508718`, OpenXR OFF, both fresh per-compiler build dirs):**
clang-cl `runs/20260614-050945-c4508718.json` (fresh `build/ci-clangcl-verify`, `-Compiler clang-cl`:
configure/build/ctest exit 0, CTest 36/36, validators 22/22; records `compiler.resolved=clang-cl`,
exe `C:\Program Files\LLVM\bin\clang-cl.exe`, linker `lld-link.exe`,
`toolchain.clang_cl="clang version 22.1.7 (a255c1ed36a1)"`, `lld_link="LLD 22.1.7"`) and MSVC
regression `runs/20260614-051220-c4508718.json` (fresh `build/ci-msvc-verify`, `-Compiler msvc`:
CTest 36/36, validators 22/22, no compiler/linker flags — legacy path, `compiler.resolved=msvc`,
cl 19.50.35727, `clang_cl=unavailable`). The two dirs are isolated and each used its intended
compiler (proven by each `CMakeCache.txt` / `CMakeCXXCompiler.cmake`); no stale-cache or MSVC
fallback on the clang-cl run.

Select the compiler with `-Compiler {msvc|clang-cl|auto}` on **both**
`tools/setup-build-env.ps1` and `scripts/ci/build-and-test.ps1`:

| `-Compiler` | setup-build-env.ps1 default | build-and-test.ps1 default | Behavior |
|---|---|---|---|
| `msvc` | **yes** | **yes** | force MSVC `cl.exe`; byte-identical to the legacy path |
| `clang-cl` | — | — | force clang-cl (the open compiler); fail loudly if LLVM is missing |
| `auto` | — | — | opt-in: clang-cl if a portable LLVM install is found, else msvc |

**Both scripts default to `msvc`** so a bare invocation always uses the established MSVC toolchain
(predictable, non-breaking). clang-cl is the documented **open compiler** and a first-class
CI-verified alternative, but it is **opt-in** — you select it with `-Compiler clang-cl` (force) or
`-Compiler auto` (clang-cl-when-present, else msvc). `build-and-test.ps1` auto-picks a per-compiler
binary dir when `-BuildDir` is omitted (`build/ci` for msvc, `build/ci-clang` for clang-cl) because
CMake forbids changing the compiler of an existing CMakeCache, so the two toolchains must never
share a binary dir.

One command, each compiler:

```
[PowerShell] scripts\ci\build-and-test.ps1 -Compiler clang-cl -BuildDir build/ci-clang
[PowerShell] scripts\ci\build-and-test.ps1 -Compiler msvc     -BuildDir build/ci-msvc
```

### clang-cl still rides on the Windows SDK + MSVC CRT (it does NOT replace them)

clang-cl runs in **MSVC-compatible mode** (`Target: x86_64-pc-windows-msvc`) and **requires** the
Windows SDK and MSVC CRT headers/libs. So the clang-cl path **still imports the MSVC environment**
via `vcvars64.bat` (the source of `INCLUDE` / `LIB` / the SDK) — `setup-build-env.ps1` does this for
*all* compilers — and additionally prepends LLVM's `bin\` to PATH and exposes
`VRCLIENT_CLANG_CL_EXE` / `VRCLIENT_LLD_LINK_EXE` / `VRCLIENT_COMPILER`. The configure then passes
`-DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl` (absolute, forward-slashed) plus the LLD
linker selection (below) with the same `Ninja` generator; the linker is **lld-link**. Escaping the
Windows SDK entirely is the future **MinGW** path (see the bottom of this file) — **not** built here.

#### Forcing lld-link across the documented CMake floor (3.24+)

`CMAKE_LINKER_TYPE=LLD` is the clean native way to force LLD, but it was **introduced in CMake
3.29** — on a conforming CMake in `[3.24, 3.29)` (the floor this manifest documents) it is an
unknown cache variable that CMake stores but **never reads**, so it is *silently ignored* and the
linker falls back to clang-cl's default (which may not be lld-link). To guarantee lld-link on the
**entire** documented floor, `build-and-test.ps1` selects the linker version-robustly when the
compiler resolves to clang-cl:

- It **always** appends `-fuse-ld=lld-link` to the **linker**-flags variables
  (`CMAKE_EXE_LINKER_FLAGS` / `CMAKE_SHARED_LINKER_FLAGS` / `CMAKE_MODULE_LINKER_FLAGS`).
  `-fuse-ld=lld-link` is a clang driver *link* flag honored on **all** CMake versions, so it
  forces lld-link even on CMake 3.24..3.28. It is passed via the linker-flags vars and **not**
  via `CMAKE_C/CXX_FLAGS`: setting `CMAKE_CXX_FLAGS` on the command line would *replace* the
  default init flags CMake seeds for clang-cl's MSVC-compat mode (notably `/EHsc`), which
  disables exceptions and breaks the build (`cannot use 'try' with exceptions disabled`). The
  linker-flags variables only affect the link step (driven by the clang-cl driver), so they
  force lld-link without disturbing compilation.
- It **additionally** passes `-DCMAKE_LINKER_TYPE=LLD` **only when the running CMake is >= 3.29**
  (probed at runtime). On older CMake it is skipped (the portable `-fuse-ld` flag carries it
  alone) so no misleading unread cache entry is left behind.

On this machine CMake is 3.31.6 (>= 3.29) so both mechanisms agree and the CMakeCache records
`CMAKE_LINKER=lld-link.exe`. Bottom line: **if you install a standalone CMake for the clang-cl
path, 3.29+ is recommended** for the native `CMAKE_LINKER_TYPE` mechanism, but the build forces
lld-link on 3.24+ regardless via `-fuse-ld=lld-link`.

### Portable clang-cl detection order (no machine-specific hardcoding)

`setup-build-env.ps1` resolves clang-cl in this portable order (PATH first so an on-PATH LLVM wins):

1. `PATH` (`Get-Command clang-cl.exe`)
2. `C:\Program Files\LLVM\bin`
3. VS-bundled LLVM: `...\VC\Tools\Llvm\x64\bin` (and the non-`x64` `...\VC\Tools\Llvm\bin`)
4. per-user installs: `%USERPROFILE%\LLVM\bin` and `%LOCALAPPDATA%\Programs\LLVM\bin`

`lld-link.exe` is the sibling of the chosen `clang-cl.exe` (same `bin\`). On THIS machine the
latest CI-verified runs at commit `c4508718` resolved the install to `C:\Program Files\LLVM\bin`
(candidate #2 above); an earlier per-user install at `%USERPROFILE%\LLVM\bin` (candidate #4)
also resolves cleanly. Either is one *candidate*, never the only option; the order above is what
makes detection portable, and the run JSON always records the exact resolved `compiler.exe`.

### clang-cl warning parity (the one CMake change)

The clang-cl porting pass surfaced exactly one class of difference: clang-cl's **default** warning
set emits the CRT secure-API deprecation (`-Wdeprecated-declarations` on `std::getenv`) that MSVC
`cl.exe` does **not** emit at this project's default `/W1` level. There is no `/WX` in the project,
so this never failed the build under either compiler — it is pure compiler-default-warning-level
noise, not a portability defect (the `std::getenv` calls are correct, standard, and null-checked).
The fix is the MSVC-documented opt-out `_CRT_SECURE_NO_WARNINGS`, applied **only under clang-cl**
via a guarded generator expression in `CMakeLists.txt`:
`add_compile_definitions($<$<CXX_COMPILER_ID:Clang>:_CRT_SECURE_NO_WARNINGS>)`. This is
compiler-scoped (clang-cl reports `CXX_COMPILER_ID == "Clang"`, `SIMULATE_ID == "MSVC"`), changes
**no** MSVC behavior, touches **no** target source list, and is **not** a blanket warning disable.
No `src/` change was needed: the whole default suite ports to clang-cl with zero source edits.

## Exact toolchain on this machine (proven by prior green builds)

| Tool | Version | Path |
|---|---|---|
| MSVC compiler (`cl.exe`, x64) | 19.50.35727.0 (VC toolset 14.50.35717) | `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.50.35717\bin\Hostx64\x64\cl.exe` |
| MSVC env script (`vcvars64.bat`) | VS Build Tools 2026 (18.4.11612.150) | `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat` |
| clang-cl (LLVM, **open compiler**, opt-in) | clang 22.1.7 (`a255c1ed36a1`), Target `x86_64-pc-windows-msvc` | `C:\Program Files\LLVM\bin\clang-cl.exe` (resolved value in the latest verified runs at commit `c4508718`; resolved via the portable order above — one candidate, not a pin) |
| lld-link (clang-cl's linker) | LLD 22.1.7 | `C:\Program Files\LLVM\bin\lld-link.exe` (sibling of clang-cl) |
| CMake (primary, used by prior builds) | 3.31.6-msvc6 | `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` |
| ctest | matches CMake | same directory as cmake.exe |
| Ninja (primary, used by prior builds) | 1.12.1 | `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe` |
| CMake (alternate bundle) | 4.2.3-msvc3 | `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` |
| Ninja (alternate bundle) | 1.12.1 | `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe` |
| Windows SDK (`rc.exe`) | 10.0.26100.0 | `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\rc.exe` |
| Python 3 | 3.14.3 | `C:\Python314\python.exe` (on PATH as `python`) |
| Python package `jsonschema` | 4.26.0 | needed by 5 of the 15 `tests/native/**` validators |
| vswhere | (VS Installer bundle) | `C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe` |
| Visual Studio Community 2022 | 17.14.37111.16 | `C:\Program Files\Microsoft Visual Studio\2022\Community` (supplies the primary CMake/Ninja bundle) |
| Visual Studio Build Tools 2026 | 18.4.11612.150 | `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools` (supplies the compiler) |

Notes:

- **Cross-install mix is intentional and proven**: compiler from VS Build Tools 2026 (v18),
  CMake + Ninja from VS 2022 Community. All three prior validation caches agree on it, and
  `setup-build-env.ps1` reproduces exactly this pairing when both installs are present.
- None of cmake/ninja/cl are on the default PATH on this machine; always go through
  `tools/setup-build-env.ps1` (or `scripts/ci/build-and-test.ps1`, which imports it).
- `vcvars64.bat` from Build Tools 2026 emits a harmless `'vswhere.exe' is not recognized`
  warning; the environment is still set correctly.
- Generator: `Ninja`, build type `Debug`, configured with `-DVRCLIENT_BUILD_TESTS=ON`.
- **OpenXR SDK: installable open-source, built from source** (see the section below). The
  default build (`scripts/ci/build-and-test.ps1`, `-OpenXR auto`) still keeps
  `VRCLIENT_BUILD_OPENXR_RUNTIME=OFF` and green — the project-local prefix is wired into
  `CMAKE_PREFIX_PATH` **only** for explicit `-OpenXR on`, so `auto` does not discover it.

## OpenXR SDK (open, built from source)

The Khronos **OpenXR-SDK loader** is installable from source, reproducibly, into a project-local
prefix by `tools/build-openxr.ps1`. This unblocks `find_package(OpenXR)` for the runtime graphics
layer (`VRCLIENT_BUILD_OPENXR_RUNTIME=ON`) without adding any binaries to the repo.

| Item | Value |
|---|---|
| Bootstrap script | `tools/build-openxr.ps1` (reproducible; idempotent; **network only** for the one-time `git clone`, all later builds offline) |
| Upstream | Khronos **OpenXR-SDK** (`https://github.com/KhronosGroup/OpenXR-SDK`), **Apache-2.0** |
| Pinned tag | **`release-1.1.43`** (`-Ref`, default), clone commit `781f2eab3698d653c804ecbd11e0aed47eaad1c6` |
| Install prefix | `external/openxr` (**gitignored** — `external/` is in `.gitignore`; only the bootstrap script is committed, never the SDK source/build/install artifacts) |
| Source / build scratch | `external/_openxr-src`, `external/_openxr-build` (gitignored) |
| Compiler | mirrors `build-and-test.ps1`: `-Compiler {msvc|clang-cl|auto}`, same toolchain detection (vcvars + clang-cl/lld-link) |

The loader is the **open, Apache-2.0 Khronos loader**. It is **graphics-API-agnostic** — the OpenXR
loader itself binds no specific graphics API. The runtime's graphics binding is now **Vulkan** (the
open, cross-platform renderer; the D3D11 binding was removed in the D3D11->Vulkan port — git history
preserves it), which keeps the loader install equally valid on Windows today and on SteamOS/Linux for
Phase 7.5.

### How it wires into the build (default stays OFF)

- `find_package(OpenXR)` resolves via `OpenXRConfig.cmake` under `external/openxr` **only** when
  `scripts/ci/build-and-test.ps1` is run with **`-OpenXR on`** — that path (and only that path) adds
  `external/openxr` to `CMAKE_PREFIX_PATH`. `external/openxr` is a non-standard, project-local
  location CMake does not search unless told, so a bare `build-and-test.ps1` (`-OpenXR auto`)
  **does NOT** discover the prefix: `auto` stays OpenXR **OFF** and **green** even with
  `external/openxr` present (verified by execution — ledger `runs/20260614-102953-3197381a.json`,
  `runs/20260614-102156-3197381a.json`, OpenXR OFF (auto), 36/36 CTest, 22/22 validators).
- Under `-OpenXR on`, `find_package(OpenXR)` resolves and the `vr_runtime` graphics layer
  (`src/native/runtime/openxr/{openxr_runtime,vulkan_device}.cpp`,
  `runtime/frame/test_scene_renderer.cpp` (now a Vulkan renderer),
  `runtime/public/vr_runtime_api.cpp`) **compiles and links** against the OpenXR loader plus the
  bootstrapped Vulkan stack in `external/vulkan` (`find_package(VulkanHeaders/VulkanLoader)` CONFIG;
  test-scene GLSL compiled to SPIR-V at build time via `glslang`). The bootstrapped loader is **Release-built**, so the runtime links **in Release**
  (`-OpenXR on -BuildType Release`: fully green, `vr_runtime.lib` + `vr_runtime_harness.exe`,
  ledger `runs/20260614-103033-3197381a.json`, `runs/20260614-102225-3197381a.json`). An explicit
  `-BuildType Debug` with `-OpenXR on` hard-errors fast by design (the loader is Release-only — see
  ledger `runs/20260614-103117-3197381a.json`); `-OpenXR on` with no `-BuildType` auto-promotes to
  Release.

### Bootstrap (one-time, networked clone) then offline

```
[PowerShell] powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-openxr.ps1
             # default -Ref release-1.1.43, -Compiler auto; clones, builds, installs to external/openxr
[PowerShell] powershell -NoProfile -ExecutionPolicy Bypass -File scripts\ci\build-and-test.ps1 -OpenXR on -BuildType Release
             # find_package(OpenXR) resolves via external/openxr; vr_runtime compiles+links (Release)
```

Note: the bootstrap script's idempotency probe currently keys on `OpenXRConfig.cmake` only (not the
loader `.lib`), so a partial/interrupted install could be treated as "already installed" — delete
`external/` and re-run to force a clean rebuild if a prior run was interrupted.

## Vulkan SDK (open, built from source)

A full, self-contained **Vulkan SDK** is buildable from source, reproducibly, into a project-local
prefix by `tools/build-vulkan.ps1` (mirrors `tools/build-openxr.ps1`'s conventions: pinned ref,
idempotency early-exit, modest parallelism, clone-is-the-only-network-step, clean intermediates by
default with `-KeepIntermediate`). This stack is **consumed by the Vulkan renderer port** (the open +
cross-platform/SteamOS-Linux renderer that replaced the former D3D11 graphics binding) — `vr_runtime`
links `external/vulkan` via CONFIG `find_package` under `-OpenXR on`, and the test-scene shaders are
compiled GLSL->SPIR-V at build time with `glslang`. The SDK is built here; the renderer that uses it
lives in `src/native/runtime/`.

| Item | Value |
|---|---|
| Bootstrap script | `tools/build-vulkan.ps1` (reproducible; idempotent; **network only** for the one-time `git clone`s, all configure/build/install offline) |
| Pinned tag (lockstep) | **`vulkan-sdk-1.4.350.0`** — every Khronos repo is cloned at the SAME tag so headers/loader/layers/SPIRV stay in lockstep (no floating refs) |
| Install prefix | `external/vulkan` (**gitignored** — `external/` is in `.gitignore`; only the bootstrap script + probe source + docs are committed, never the SDK source/build/install artifacts) |
| Source / build scratch | under `external/` (gitignored), cleaned by default after a successful install (`-KeepIntermediate` to retain) |
| Compiler | mirrors `build-and-test.ps1`: `-Compiler {msvc\|clang-cl\|auto}`, same toolchain detection (vcvars + clang-cl/lld-link) |
| Upstream | Khronos repos, all **Apache-2.0 / MIT** open source |

### Components installed (all at `vulkan-sdk-1.4.350.0`, pinned commits)

| Component | Pinned commit | Result |
|---|---|---|
| **Vulkan-Headers** (Khronos) | `8864cdc896bbc2a9b6eb36b3218fc9ef57908d77` | installed — `external/vulkan/include/vulkan/{vulkan_core.h,vulkan.h}`; config `external/vulkan/share/cmake/VulkanHeaders/VulkanHeadersConfig.cmake` |
| **Vulkan-Loader** (Khronos) | `a9e72c66d5cb79911eb9a9063bf4016dd0a3a123` | built + installed the `vulkan-1` loader — `external/vulkan/lib/vulkan-1.lib` + `external/vulkan/bin/vulkan-1.dll`; config `external/vulkan/lib/cmake/VulkanLoader/VulkanLoaderConfig.cmake` (so `find_package(Vulkan)`/linking works) |
| **glslang** (shader compiler, `ENABLE_OPT=OFF`) | `275822a6261ee689aadb1da5f09a0ec2f058685c` | built + installed — `external/vulkan/bin/glslang.exe` (+ `glslangValidator.exe`), libs `external/vulkan/lib/glslang*.lib`; **functionally verified** compiling GLSL -> SPIR-V |
| **SPIRV-Headers** | `ad9184e76a66b1001c29db9b0a3e87f646c64de0` | installed — config under `external/vulkan/share/cmake/SPIRV-Headers` |
| **SPIRV-Tools** | `0539c81f69a3daeb706fd3477dca61435b475156` | built + installed — `external/vulkan/bin/SPIRV-Tools-shared.dll` + `external/vulkan/lib/SPIRV-Tools.lib` + per-tool cmake configs |
| **Vulkan-Utility-Libraries** | `a96bd4933c6adfc21329d15e9999791a0153bbf3` | built + installed (provides `Vulkan::LayerSettings`/`UtilityHeaders` that VVL links) — config `external/vulkan/lib/cmake/VulkanUtilityLibraries/VulkanUtilityLibrariesConfig.cmake` |
| **Vulkan-ValidationLayers** (VVL, **DEV-ONLY**, parallelism 1) | `b9d4f9ead8d97c1bb0d174ea07d8aed8273818a5` | built + installed `VK_LAYER_KHRONOS_validation` — `external/vulkan/bin/VkLayer_khronos_validation.dll` (25.8 MB) + `external/vulkan/bin/VkLayer_khronos_validation.json` |

**Essentials (Vulkan-Headers + Vulkan-Loader + shader compiler): installed. Validation layers: BUILT
(not deferred).** The bootstrap builds essentials first and confirms they install, then attempts the
heavy VVL; on this run VVL built successfully under host RAM pressure at parallelism 1, so all seven
Khronos repos are present at the lockstep tag.

### Validation layers are DEV-ONLY (runtime-loaded, never shipped)

`VK_LAYER_KHRONOS_validation` is **dev-only runtime tooling**: it is loaded at runtime via
`VK_LAYER_PATH` / `VK_INSTANCE_LAYERS`, **never linked into or shipped with any product target and
never wired into the default build**. The bootstrap does not add it to any product/default CMake
target; the SDK binding is CONFIG-mode (`find_package(VulkanLoader/VulkanHeaders CONFIG)`) to the
bootstrapped stack only.

### Headless probe + validation result (CLEAN)

A tiny, additive probe target `vrclient_vulkan_probe` links the **bootstrapped** loader (not the
system `vulkan-1`) and is **OFF by default** (additive-only — the default OpenXR-OFF build is
unaffected: ledger `runs/20260615-031922-bc0f3537.json`, OpenXR OFF, CTest 40/40, validators 23/23,
GREEN, "default build unaffected by additive Vulkan probe — probe target OFF by default").

The headless `vrclient_vulkan_probe --validate` run with
`VK_LAYER_PATH=<repo>/external/vulkan/bin` produced a **CLEAN** result: the
`VK_LAYER_KHRONOS_validation` layer is **genuinely active** (loaded from the bootstrapped prefix) and
reported **zero validation errors** — a real pass, not a layer-missing false pass. The layer remains
runtime-only and is not shipped.

### Bootstrap (one-time, networked clone) then offline

```
[PowerShell] powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-vulkan.ps1
             # default -Ref vulkan-sdk-1.4.350.0, -Compiler auto; clones each Khronos repo at the
             # lockstep tag, builds essentials first, then VVL; installs to external/vulkan
```

Re-running is **idempotent**: it skips when `external/vulkan` already has the loader + (if built) the
validation layers, unless `-Force`. The pinned tag and per-repo commits above make it reproducible.

### Graceful degradation if VVL is ever deferred (it was NOT this run)

By design, if Vulkan-ValidationLayers fails to build under host RAM pressure (OOM / heap) after a
retry at parallelism 1, the bootstrap does **not** fail the whole run — it completes with the
**essentials** (Headers + Loader + shader compiler), marks the validation layers **DEFERRED** in the
report + script output, and says so honestly (the renderer port can proceed on essentials; validation
is added later). On **this** run VVL **BUILT** successfully, so nothing was deferred. To add the
layers later if a future run defers them, simply re-run `tools/build-vulkan.ps1` (idempotent; it
re-attempts VVL when the layer DLL is absent) on a host with enough free RAM, optionally forcing
parallelism 1 for the VVL step. (Low note: the OOM retry re-runs at the same parallelism (1) with no
memory-relief delay — effectively a clean rebuild, not a softer attempt — and the probe driver
hard-fails configure via `find_package(VulkanLoader/VulkanHeaders CONFIG REQUIRED)` if essentials
succeeded but VVL was DEFERRED, so build the probe only when the full stack is present.)

## What a fresh machine needs

1. **Visual Studio Build Tools** (2022 v17+ or 2026 v18) with:
   - workload *Desktop development with C++*
   - component `Microsoft.VisualStudio.Component.VC.Tools.x86.x64` (MSVC x64 toolset)
   - a Windows 11 SDK component (e.g. 10.0.26100)
   - component *C++ CMake tools for Windows* (bundles cmake.exe >= 3.24 and ninja.exe), or
     standalone CMake >= 3.24 (`winget install Kitware.CMake`) + Ninja
     (`winget install Ninja-build.Ninja`). **For the clang-cl path, CMake >= 3.29 is
     recommended** so the native `CMAKE_LINKER_TYPE=LLD` mechanism is honored; on 3.24..3.28
     the build still forces lld-link via `-fuse-ld=lld-link` (see "Forcing lld-link across the
     documented CMake floor" above).
   - `[Either] winget install --id Microsoft.VisualStudio.2022.BuildTools` then add the
     components via the VS Installer.
2. **Python 3.x** on PATH (`winget install Python.Python.3.14`) plus
   `python -m pip install jsonschema`.
3. **Git** on PATH. If the repo lives on a filesystem that does not record ownership
   (e.g. this G: drive), add the safe.directory exception once:
   `[Either] git config --global --add safe.directory <repo-root>` — otherwise every git
   call fails with "detected dubious ownership" (the CI script also retries with
   `git -c safe.directory=<repo-root>` as a fallback).
4. **(open compiler, opt-in) LLVM / clang-cl + lld-link** — needed for the clang-cl toolchain
   (`-Compiler clang-cl` or `-Compiler auto` when LLVM is present):
   `[Either] winget install --id LLVM.LLVM`, or add the VS Installer components
   `Microsoft.VisualStudio.Component.VC.Llvm.Clang` +
   `Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset`. clang-cl still rides on the
   MSVC SDK/CRT from step 1 — install both. (MSVC-only builds do **not** need this.)
5. Run `[PowerShell] . .\tools\setup-build-env.ps1` (add `-Install` to print these
   instructions when something is missing — nothing is ever auto-downloaded). Pass
   `-Compiler clang-cl` to also resolve/verify LLVM.
6. One-command build + test + validate:
   `[PowerShell] powershell -NoProfile -ExecutionPolicy Bypass -File scripts\ci\build-and-test.ps1`
   (defaults to `-Compiler msvc`). Opt into the open compiler with `-Compiler clang-cl` (force) or
   `-Compiler auto` (clang-cl when LLVM is present, else msvc), each with a distinct `-BuildDir`.

## Future: MinGW (fully SDK-free / no-Windows-SDK) — NOT built here

clang-cl is the *open compiler*, but it is **not** fully toolchain-independent: it runs in
MSVC-compatible mode and still depends on the proprietary **Windows SDK + MSVC CRT** (imported via
`vcvars64.bat`). The future, fully-open path that escapes the Windows SDK entirely is a **MinGW-w64
/ GNU** toolchain (its own libc/CRT and headers, no `vcvars`). That is a larger port (different CRT,
different `<windows.h>` provenance, Win32 ABI nuances, `*.dll` import-lib handling) and is **out of
scope for this increment** — documented here as the next milestone, not implemented. Today the two
CI-verified toolchains are clang-cl (open compiler, opt-in) and MSVC (default), both on the Windows SDK.

cmake_minimum_required(VERSION 3.24)

project(VRConversionManager
  VERSION 0.1.0
  DESCRIPTION "VR Conversion Manager native runtime foundation"
  LANGUAGES CXX)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VRCLIENT_BUILD_OPENXR_RUNTIME "Build the OpenXR runtime and headset harness" ON)
option(VRCLIENT_BUILD_TESTS "Build native unit tests" ON)
option(VRCLIENT_BUILD_RED4EXT_BRIDGE
  "Build the Cyberpunk 2077 RED4ext bridge (requires external/red4ext-sdk)." OFF)
option(VRCLIENT_BUILD_RDR2_BRIDGE
  "Build the optional RDR2 ScriptHookRDR2/D3D12/OpenXR bridge (requires Detours)." OFF)
option(VRCLIENT_ENABLE_DETOURS_SMOKE
  "Enable optional user-supplied Detours controlled smoke test. OFF by default." OFF)
set(VRCLIENT_DETOURS_WITHDLL "" CACHE FILEPATH
  "Path to user-built Microsoft Detours withdll.exe for the optional controlled smoke proof.")
option(VRCLIENT_ENABLE_EASYHOOK_ATTACH_SMOKE
  "Enable optional user-supplied EasyHook attach-to-running controlled smoke test. OFF by default." OFF)
set(VRCLIENT_EASYHOOK_ATTACH_LOADER "" CACHE FILEPATH
  "Path to user-built EasyHook-compatible AttachLoader.exe for the optional attach proof.")

if(VRCLIENT_ENABLE_DETOURS_SMOKE)
  if(NOT VRCLIENT_DETOURS_WITHDLL)
    set(_vrclient_default_withdll
      "${CMAKE_CURRENT_SOURCE_DIR}/Detours-4.0.1/Detours-4.0.1/bin.X64/withdll.exe")
    if(EXISTS "${_vrclient_default_withdll}")
      set(VRCLIENT_DETOURS_WITHDLL "${_vrclient_default_withdll}" CACHE FILEPATH
        "Path to user-built Microsoft Detours withdll.exe for the optional controlled smoke proof." FORCE)
    endif()
  endif()
  if(NOT EXISTS "${VRCLIENT_DETOURS_WITHDLL}")
    message(FATAL_ERROR
      "VRCLIENT_ENABLE_DETOURS_SMOKE=ON requires VRCLIENT_DETOURS_WITHDLL to point "
      "to a user-built Detours withdll.exe. Detours is not vendored by VRClient.")
  endif()
endif()

if(VRCLIENT_ENABLE_EASYHOOK_ATTACH_SMOKE)
  if(NOT EXISTS "${VRCLIENT_EASYHOOK_ATTACH_LOADER}")
    message(FATAL_ERROR
      "VRCLIENT_ENABLE_EASYHOOK_ATTACH_SMOKE=ON requires "
      "VRCLIENT_EASYHOOK_ATTACH_LOADER to point to a user-built EasyHook-compatible "
      "AttachLoader.exe. EasyHook and the attach primitive are not vendored by VRClient.")
  endif()
endif()

# clang-cl warning-parity (additive, compiler-scoped — see tools/toolchain.md).
# clang-cl is a CI-verified build toolchain alongside MSVC (the documented "open
# default"). Running in MSVC-compatible mode it shares the MSVC CRT headers, but its
# DEFAULT warning set surfaces the CRT secure-API deprecation (-Wdeprecated-declarations
# on std::getenv etc.) that MSVC cl.exe does NOT emit at this project's default /W1 level.
# That is pure compiler-default-warning-level noise, not a portability defect: the
# std::getenv calls are correct, standard, and null-checked. Opt out of the CRT secure
# warnings the SAME way MSVC documents (_CRT_SECURE_NO_WARNINGS — the warning text itself
# names it), but ONLY under clang-cl, via a guarded compiler-id generator expression. This
# does NOT change the MSVC build (MSVC never emitted these here), does NOT touch any target
# source list, and is NOT a blanket warning disable — it scopes one CRT class on one
# compiler so both toolchains build clean. (clang-cl reports CXX_COMPILER_ID == "Clang".)
add_compile_definitions($<$<CXX_COMPILER_ID:Clang>:_CRT_SECURE_NO_WARNINGS>)

add_library(vr_diagnostics STATIC
  src/native/diagnostics/diagnostics_profile.cpp
  src/native/diagnostics/diagnostics_system.cpp
  src/native/diagnostics/logging/diagnostic_logger.cpp
  src/native/diagnostics/crash/crash_capture.cpp
  src/native/diagnostics/overlay/diagnostics_overlay.cpp
  src/native/diagnostics/export/diagnostics_exporter.cpp)
target_include_directories(vr_diagnostics
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime)
target_compile_definitions(vr_diagnostics
  PUBLIC
    WIN32_LEAN_AND_MEAN
    NOMINMAX)

add_library(vr_runtime_perf STATIC
  src/native/runtime/perf/dynamic_resolution.cpp
  src/native/runtime/perf/foveation.cpp
  src/native/runtime/perf/frame_pacing.cpp
  src/native/runtime/perf/runtime_profile.cpp)
target_include_directories(vr_runtime_perf
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime)

add_library(vr_runtime_frame STATIC
  src/native/runtime/frame/frame_loop.cpp
  src/native/runtime/frame/stereo_math.cpp)
target_include_directories(vr_runtime_frame
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime)

if(WIN32)
  add_executable(vrclient_dll_attach_loader
    src/native/injector/runtime_loader/dll_attach_loader.cpp)

  add_library(vr_d3d12_binding STATIC
    src/native/runtime/openxr/d3d12_binding.cpp)
  target_include_directories(vr_d3d12_binding
    PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime)
  target_compile_definitions(vr_d3d12_binding
    PUBLIC
      WIN32_LEAN_AND_MEAN
      NOMINMAX)
  target_link_libraries(vr_d3d12_binding PUBLIC d3d12 dxgi)

  add_library(vr_unreal_native STATIC
    src/native/adapters/unreal/meccha_camera_pin.cpp
    src/native/adapters/unreal/meccha_camera_identity_gate.cpp
    src/native/adapters/unreal/meccha_orientation_tracking_gate.cpp
    src/native/adapters/unreal/meccha_stereo_pose_pair.cpp
    src/native/adapters/unreal/meccha_stereo_projection.cpp
    src/native/adapters/unreal/native_unreal_contract.cpp
    src/native/adapters/unreal/unreal_camera_contract.cpp
    src/native/adapters/unreal/unreal_camera_observer.cpp
    src/native/adapters/unreal/unreal_scene_view_decoder.cpp
    src/native/adapters/unreal/unreal_view_seam_locator.cpp
    src/native/adapters/unreal/unreal_view_seam_pe.cpp
    src/native/adapters/unreal/unreal_frame_contract.cpp
    src/native/adapters/unreal/unreal_openxr_bridge.cpp
    src/native/adapters/unreal/unreal_orientation_only.cpp
    src/native/adapters/unreal/d3d12_render_util.cpp
    src/native/adapters/unreal/d3d12_scene_relay_renderer.cpp
    src/native/adapters/unreal/d3d12_stereo_diagnostic_renderer.cpp
    src/native/adapters/unreal/d3d12_observer.cpp)
  target_include_directories(vr_unreal_native
    PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}/src/native
      ${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime)
  target_link_libraries(vr_unreal_native PUBLIC
    vr_d3d12_binding d3d12 d3dcompiler dxgi)
endif()

add_library(vr_injector_process STATIC
  src/native/injector/process/process_discovery.cpp)
target_include_directories(vr_injector_process
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime)
target_link_libraries(vr_injector_process PUBLIC vr_diagnostics)

add_library(vr_versioning STATIC
  src/native/versioning/game_fingerprint.cpp)
target_include_directories(vr_versioning
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime)
target_link_libraries(vr_versioning PUBLIC vr_diagnostics vr_injector_process)

add_library(vr_plugin_host STATIC
  src/native/plugins/host/plugin_host.cpp
  src/native/plugins/host/plugin_library.cpp)
target_include_directories(vr_plugin_host
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime)
target_link_libraries(vr_plugin_host PUBLIC vr_diagnostics ${CMAKE_DL_LIBS})

add_library(vr_plugin_validation STATIC
  src/native/plugins/validation/adapter_validator.cpp)
target_include_directories(vr_plugin_validation
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime)
target_link_libraries(vr_plugin_validation PUBLIC vr_plugin_host)

add_library(vr_shared_systems STATIC
  src/native/shared/game_profile.cpp
  src/native/shared/input/input_system.cpp
  src/native/shared/comfort/comfort_system.cpp
  src/native/shared/hud/hud_system.cpp)
target_include_directories(vr_shared_systems
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime)

add_executable(vr_adapter_validator
  src/native/plugins/validation/validator_main.cpp)
target_link_libraries(vr_adapter_validator PRIVATE vr_plugin_validation)

add_library(vrclient_template_adapter SHARED
  adapters/_template/template_adapter.cpp)
target_include_directories(vrclient_template_adapter
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native)
target_compile_definitions(vrclient_template_adapter
  PRIVATE
    VRCLIENT_ADAPTER_EXPORTS)

add_library(vrclient_repo_adapter SHARED
  adapters/repo/repo_adapter.cpp)
target_include_directories(vrclient_repo_adapter
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native)
target_compile_definitions(vrclient_repo_adapter
  PRIVATE
    VRCLIENT_ADAPTER_EXPORTS)

add_library(vrclient_cyberpunk_2077_adapter SHARED
  adapters/cyberpunk_2077/native_adapter.cpp)
target_include_directories(vrclient_cyberpunk_2077_adapter
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native)
target_compile_definitions(vrclient_cyberpunk_2077_adapter
  PRIVATE
    VRCLIENT_ADAPTER_EXPORTS)

add_library(vrclient_rdr2_adapter SHARED
  adapters/rdr2/native_adapter.cpp)
target_include_directories(vrclient_rdr2_adapter
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native)
target_compile_definitions(vrclient_rdr2_adapter
  PRIVATE
    VRCLIENT_ADAPTER_EXPORTS)

if(WIN32 AND VRCLIENT_BUILD_RED4EXT_BRIDGE)
  add_library(vr_redengine_vrcam_selector STATIC
    src/native/adapters/redengine/redengine_vrcam_selector.cpp)
  target_include_directories(vr_redengine_vrcam_selector
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/native)
  set(VRCLIENT_RED4EXT_SDK_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/external/red4ext-sdk-complete/RED4ext.SDK-a4a781088a92a8efa890d94fde4efd8985d497c7"
    CACHE PATH "Pinned RED4ext.SDK source directory")
  if(NOT EXISTS "${VRCLIENT_RED4EXT_SDK_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
      "VRCLIENT_BUILD_RED4EXT_BRIDGE=ON requires RED4ext.SDK 1.0.0 at "
      "${VRCLIENT_RED4EXT_SDK_DIR}. See docs/redengine/CYBERPUNK-2077-VR-ARCHITECTURE.md.")
  endif()

  set(RED4EXT_HEADER_ONLY ON CACHE BOOL "Use header-only RED4ext.SDK" FORCE)
  set(RED4EXT_INSTALL OFF CACHE BOOL "Do not install RED4ext.SDK" FORCE)
  add_subdirectory("${VRCLIENT_RED4EXT_SDK_DIR}" external/red4ext-sdk EXCLUDE_FROM_ALL)

  add_library(vrclient_redengine_bridge SHARED
    src/native/adapters/redengine/red4ext_bridge.cpp
    src/native/adapters/redengine/redengine_detour_transaction.cpp
    src/native/adapters/redengine/redengine_graphics_hooks.cpp
    src/native/adapters/redengine/redengine_graphics_observer.cpp)
  set(_vrclient_redengine_detours_root
    "${CMAKE_CURRENT_SOURCE_DIR}/Detours-4.0.1/Detours-4.0.1")
  set(_vrclient_redengine_detours_include
    "${_vrclient_redengine_detours_root}/include")
  set(_vrclient_redengine_detours_library
    "${_vrclient_redengine_detours_root}/lib.X64/detours.lib")
  if(NOT EXISTS "${_vrclient_redengine_detours_include}/detours.h" OR
     NOT EXISTS "${_vrclient_redengine_detours_library}")
    message(FATAL_ERROR
      "VRCLIENT_BUILD_RED4EXT_BRIDGE=ON requires the locally built, MIT-licensed "
      "Microsoft Detours headers and x64 library under ${_vrclient_redengine_detours_root}.")
  endif()
  target_include_directories(vrclient_redengine_bridge
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src/native
      ${_vrclient_redengine_detours_include})
  target_compile_definitions(vrclient_redengine_bridge
    PRIVATE
      WIN32_LEAN_AND_MEAN
      NOMINMAX)
  target_link_libraries(vrclient_redengine_bridge
    PRIVATE
      RED4ext::SDK
      vr_redengine_vrcam_selector
      vr_unreal_native
      "${_vrclient_redengine_detours_library}"
      d3d12
      dxguid
      dxgi)
  set_target_properties(vrclient_redengine_bridge PROPERTIES
    OUTPUT_NAME "VRClient.REDengine")
endif()

if(WIN32)
  add_library(vrclient_meccha_native_adapter SHARED
    adapters/meccha_chameleon/native_adapter.cpp)
  target_include_directories(vrclient_meccha_native_adapter
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src/native)
  target_compile_definitions(vrclient_meccha_native_adapter
    PRIVATE
      VRCLIENT_ADAPTER_EXPORTS)
endif()

add_library(vr_injector_safety STATIC
  src/native/injector/safety/safety_preflight.cpp)
target_include_directories(vr_injector_safety
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime)
target_link_libraries(vr_injector_safety PUBLIC vr_diagnostics vr_injector_process vr_versioning)

# Phase 7 (Safety & Compliance): the independent pre-injection safety verdict
# service. It WRAPS (does not replace) the Phase 3 preflight spine via
# vr_injector_safety, adds the four-verdict model + config-backed rule set +
# DEFENSIVE detection inputs. It contains NO process-injection or anti-cheat
# bypass code; it only computes a verdict that gates the injector. Declared
# BEFORE vr_injector_bootstrap because the bootstrap path now consumes the
# verdict as a pre-injection precondition (Phase 7 T03).
add_library(vr_safety STATIC
  src/native/safety/safety_verdict.cpp
  src/native/safety/detection_inputs.cpp)
target_include_directories(vr_safety
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime)
target_link_libraries(vr_safety PUBLIC vr_diagnostics vr_injector_safety vr_versioning)

# M5 UNIFY-03: injection-free rules-level cross-check CLI (additive).
add_executable(vrclient_safety_cli src/native/safety/cli/vrclient_safety_cli.cpp)
target_link_libraries(vrclient_safety_cli PRIVATE vr_safety vr_config_versioning)

add_library(vr_injector_bootstrap STATIC
  src/native/injector/bootstrap/bootstrap_smoke.cpp)
target_include_directories(vr_injector_bootstrap
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime)
# Phase 7 T03: the bootstrap path gates on the Phase 7 safety verdict before the
# runtime loader (the only process touch), so it links vr_safety. This is
# acyclic — vr_safety never depends on the bootstrap library.
target_link_libraries(vr_injector_bootstrap PUBLIC vr_diagnostics vr_injector_safety vr_safety)

add_executable(vrclient_smoke_host
  src/native/injector/bootstrap/smoke_host_main.cpp)

if(VRCLIENT_ENABLE_DETOURS_SMOKE OR VRCLIENT_ENABLE_EASYHOOK_ATTACH_SMOKE)
  add_library(vrclient_proof_payload SHARED
    src/native/injector/proof_payload/proof_payload_main.cpp
    src/native/injector/proof_payload/proof_payload.def)
  target_compile_definitions(vrclient_proof_payload PRIVATE WIN32_LEAN_AND_MEAN)
  set_target_properties(vrclient_proof_payload PROPERTIES
    OUTPUT_NAME "vrclient_proof_payload")
endif()

if(WIN32 AND VRCLIENT_ENABLE_DETOURS_SMOKE)
  set(_vrclient_detours_root
    "${CMAKE_CURRENT_SOURCE_DIR}/Detours-4.0.1/Detours-4.0.1")
  set(_vrclient_detours_include "${_vrclient_detours_root}/include")
  set(_vrclient_detours_library "${_vrclient_detours_root}/lib.X64/detours.lib")
  if(NOT EXISTS "${_vrclient_detours_include}/detours.h" OR
     NOT EXISTS "${_vrclient_detours_library}")
    message(FATAL_ERROR
      "Meccha observer payload requires the user-built Detours headers/library "
      "beside the configured withdll.exe.")
  endif()

  add_library(vrclient_meccha_observer_payload SHARED
    src/native/adapters/unreal/meccha_observer_payload.cpp
    src/native/adapters/unreal/meccha_detour_transaction.cpp
    src/native/adapters/unreal/meccha_observer_payload.def)
  target_include_directories(vrclient_meccha_observer_payload
    PRIVATE
      ${_vrclient_detours_include})
  target_compile_definitions(vrclient_meccha_observer_payload
    PRIVATE
      WIN32_LEAN_AND_MEAN
      NOMINMAX)
  target_link_libraries(vrclient_meccha_observer_payload
    PRIVATE
      vr_unreal_native
      "${_vrclient_detours_library}"
      d3d12
      dxgi)
  set_target_properties(vrclient_meccha_observer_payload PROPERTIES
    OUTPUT_NAME "vrclient_meccha_observer_payload")

endif()

if(VRCLIENT_BUILD_TESTS AND WIN32)
  add_executable(vrclient_meccha_observer_host WIN32
    tests/native/adapters/meccha_observer_host.cpp)
  target_link_libraries(vrclient_meccha_observer_host PRIVATE d3d12 dxgi)

  add_executable(vrclient_easyhook_attach_smoke
    tests/native/injector/easyhook_attach_smoke_main.cpp)
  target_compile_definitions(vrclient_easyhook_attach_smoke
    PRIVATE
      VRCLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
      WIN32_LEAN_AND_MEAN
      NOMINMAX)
  target_link_libraries(vrclient_easyhook_attach_smoke
    PRIVATE
      vr_injector_bootstrap
      vr_safety
      vr_versioning
      vr_injector_process)

  add_executable(vrclient_d3d12_stereo_probe
    tests/native/runtime/d3d12_stereo_probe.cpp)
  target_link_libraries(vrclient_d3d12_stereo_probe
    PRIVATE
      vr_unreal_native
      d3d12
      dxgi)

  add_executable(vrclient_unreal_view_seam_probe
    tools/native/unreal_view_seam_probe.cpp)
  target_link_libraries(vrclient_unreal_view_seam_probe
    PRIVATE vr_unreal_native)
endif()

# ---------------------------------------------------------------------------
# Shared Vulkan backend wiring (used by the OpenXR runtime AND the dev-only
# headless render-validation target). Resolves the BOOTSTRAPPED Vulkan SDK from
# the project-local prefix via CONFIG packages (never the system FindVulkan
# module) and compiles the test-scene GLSL -> SPIR-V -> embedded C-array header.
# Entered ONLY when the runtime or the probe is built; the default OpenXR-OFF
# build (probe OFF) never enters it, so it is byte-for-byte unchanged.
# ---------------------------------------------------------------------------
if(VRCLIENT_BUILD_OPENXR_RUNTIME OR VRCLIENT_BUILD_VULKAN_PROBE)
  if(NOT DEFINED VRCLIENT_VULKAN_PREFIX)
    set(VRCLIENT_VULKAN_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/external/vulkan")
  endif()
  message(STATUS "VRClient Vulkan backend: SDK prefix = ${VRCLIENT_VULKAN_PREFIX}")
  list(APPEND CMAKE_PREFIX_PATH "${VRCLIENT_VULKAN_PREFIX}")

  find_package(VulkanHeaders CONFIG REQUIRED)  # Vulkan::Headers (INTERFACE)
  find_package(VulkanLoader  CONFIG REQUIRED)  # Vulkan::Loader  (vulkan-1)
  find_package(glslang       CONFIG REQUIRED)  # glslang::glslang-standalone (glslang.exe)

  # ---- Compile the test-scene GLSL -> SPIR-V (build time) and EMBED as C arrays.
  # No FXC/d3dcompiler; no run-time file I/O. The embedded header is generated into
  # the build tree and consumed by test_scene_renderer.cpp.
  set(_shader_src_dir ${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime/frame/shaders)
  # The renderer does `#include "shaders/test_scene_shaders.h"`, so the generated
  # header lives under <gen_root>/shaders/ and <gen_root> is the include dir.
  set(_shader_gen_root ${CMAKE_CURRENT_BINARY_DIR}/generated)
  set(_shader_gen_dir ${_shader_gen_root}/shaders)
  file(MAKE_DIRECTORY ${_shader_gen_dir})

  set(_test_scene_shaders_header ${_shader_gen_dir}/test_scene_shaders.h)
  set(_overlay_shaders_header ${_shader_gen_dir}/overlay_shaders.h)

  # vert -> spv
  add_custom_command(
    OUTPUT ${_shader_gen_dir}/test_scene.vert.spv
    COMMAND glslang::glslang-standalone -V
            ${_shader_src_dir}/test_scene.vert
            -o ${_shader_gen_dir}/test_scene.vert.spv
    DEPENDS ${_shader_src_dir}/test_scene.vert
    COMMENT "glslang: test_scene.vert -> SPIR-V"
    VERBATIM)
  # frag -> spv
  add_custom_command(
    OUTPUT ${_shader_gen_dir}/test_scene.frag.spv
    COMMAND glslang::glslang-standalone -V
            ${_shader_src_dir}/test_scene.frag
            -o ${_shader_gen_dir}/test_scene.frag.spv
    DEPENDS ${_shader_src_dir}/test_scene.frag
    COMMENT "glslang: test_scene.frag -> SPIR-V"
    VERBATIM)
  # overlay vert -> spv (lightweight in-headset debug overlay, DIAG-03 partial)
  add_custom_command(
    OUTPUT ${_shader_gen_dir}/overlay.vert.spv
    COMMAND glslang::glslang-standalone -V
            ${_shader_src_dir}/overlay.vert
            -o ${_shader_gen_dir}/overlay.vert.spv
    DEPENDS ${_shader_src_dir}/overlay.vert
    COMMENT "glslang: overlay.vert -> SPIR-V"
    VERBATIM)
  # overlay frag -> spv
  add_custom_command(
    OUTPUT ${_shader_gen_dir}/overlay.frag.spv
    COMMAND glslang::glslang-standalone -V
            ${_shader_src_dir}/overlay.frag
            -o ${_shader_gen_dir}/overlay.frag.spv
    DEPENDS ${_shader_src_dir}/overlay.frag
    COMMENT "glslang: overlay.frag -> SPIR-V"
    VERBATIM)
  # spv (vert+frag) -> single embedded C-array header
  add_custom_command(
    OUTPUT ${_test_scene_shaders_header}
    COMMAND ${CMAKE_COMMAND}
            -DSPV_IN=${_shader_gen_dir}/test_scene.vert.spv
            -DHEADER_OUT=${_shader_gen_dir}/test_scene.vert.spv.h
            -DSYMBOL=kTestSceneVertSpv
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spirv.cmake
    COMMAND ${CMAKE_COMMAND}
            -DSPV_IN=${_shader_gen_dir}/test_scene.frag.spv
            -DHEADER_OUT=${_shader_gen_dir}/test_scene.frag.spv.h
            -DSYMBOL=kTestSceneFragSpv
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spirv.cmake
    COMMAND ${CMAKE_COMMAND}
            -DOUT=${_test_scene_shaders_header}
            -DVERT_H=${_shader_gen_dir}/test_scene.vert.spv.h
            -DFRAG_H=${_shader_gen_dir}/test_scene.frag.spv.h
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/combine_spirv_headers.cmake
    DEPENDS ${_shader_gen_dir}/test_scene.vert.spv
            ${_shader_gen_dir}/test_scene.frag.spv
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spirv.cmake
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/combine_spirv_headers.cmake
    COMMENT "embed_spirv: test_scene SPIR-V -> ${_test_scene_shaders_header}"
    VERBATIM)

  # spv (overlay vert+frag) -> single embedded C-array header. Parallel to the
  # test_scene combine step; reuses embed_spirv.cmake + combine_spirv_headers.cmake
  # verbatim so the kOverlayVertSpv/_size symbol names stay consistent and the
  # test_scene header stays byte-identical.
  add_custom_command(
    OUTPUT ${_overlay_shaders_header}
    COMMAND ${CMAKE_COMMAND}
            -DSPV_IN=${_shader_gen_dir}/overlay.vert.spv
            -DHEADER_OUT=${_shader_gen_dir}/overlay.vert.spv.h
            -DSYMBOL=kOverlayVertSpv
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spirv.cmake
    COMMAND ${CMAKE_COMMAND}
            -DSPV_IN=${_shader_gen_dir}/overlay.frag.spv
            -DHEADER_OUT=${_shader_gen_dir}/overlay.frag.spv.h
            -DSYMBOL=kOverlayFragSpv
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spirv.cmake
    COMMAND ${CMAKE_COMMAND}
            -DOUT=${_overlay_shaders_header}
            -DVERT_H=${_shader_gen_dir}/overlay.vert.spv.h
            -DFRAG_H=${_shader_gen_dir}/overlay.frag.spv.h
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/combine_spirv_headers.cmake
    DEPENDS ${_shader_gen_dir}/overlay.vert.spv
            ${_shader_gen_dir}/overlay.frag.spv
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spirv.cmake
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/combine_spirv_headers.cmake
    COMMENT "embed_spirv: overlay SPIR-V -> ${_overlay_shaders_header}"
    VERBATIM)

  add_custom_target(vr_runtime_shaders
    DEPENDS ${_test_scene_shaders_header} ${_overlay_shaders_header})
endif()

if(VRCLIENT_BUILD_OPENXR_RUNTIME)
  find_package(OpenXR REQUIRED)

  add_library(vr_runtime STATIC
    src/native/runtime/public/vr_runtime_api.cpp
    src/native/runtime/openxr/d3d12_device.cpp
    src/native/runtime/openxr/vulkan_device.cpp
    src/native/runtime/openxr/openxr_runtime.cpp
    src/native/runtime/frame/test_scene_renderer.cpp)

  add_dependencies(vr_runtime vr_runtime_shaders)

  target_include_directories(vr_runtime
    PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime/public
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime
      ${_shader_gen_root})

  target_compile_definitions(vr_runtime
    PRIVATE
      WIN32_LEAN_AND_MEAN
      NOMINMAX
      XR_USE_PLATFORM_WIN32
      XR_USE_GRAPHICS_API_VULKAN
      XR_USE_GRAPHICS_API_D3D12)

  target_link_libraries(vr_runtime
    PRIVATE
      OpenXR::openxr_loader
      vr_diagnostics
      vr_runtime_frame
      vr_runtime_perf
      vr_d3d12_binding
      Vulkan::Headers
      Vulkan::Loader
      d3d12
      dxgi)

  if(TARGET vrclient_redengine_bridge)
    target_sources(vrclient_redengine_bridge PRIVATE
      src/native/adapters/redengine/redengine_openxr_bridge.cpp)
    target_compile_definitions(vrclient_redengine_bridge PRIVATE
      VRCLIENT_REDENGINE_OPENXR=1)
    target_link_libraries(vrclient_redengine_bridge PRIVATE vr_runtime)
    add_custom_command(TARGET vrclient_redengine_bridge POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Vulkan::Loader>
        $<TARGET_FILE_DIR:vrclient_redengine_bridge>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:OpenXR::openxr_loader>
        $<TARGET_FILE_DIR:vrclient_redengine_bridge>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config/defaults/runtime-profile.json
        $<TARGET_FILE_DIR:vrclient_redengine_bridge>/runtime-profile.json
      COMMENT "Cyberpunk REDengine bridge: stage OpenXR/Vulkan loaders and runtime profile")
  endif()

  if(WIN32 AND VRCLIENT_BUILD_RDR2_BRIDGE)
    set(_vrclient_rdr2_detours_root
      "${CMAKE_CURRENT_SOURCE_DIR}/Detours-4.0.1/Detours-4.0.1")
    set(_vrclient_rdr2_detours_include "${_vrclient_rdr2_detours_root}/include")
    set(_vrclient_rdr2_detours_library "${_vrclient_rdr2_detours_root}/lib.X64/detours.lib")
    if(NOT EXISTS "${_vrclient_rdr2_detours_include}/detours.h" OR
       NOT EXISTS "${_vrclient_rdr2_detours_library}")
      message(FATAL_ERROR
        "VRCLIENT_BUILD_RDR2_BRIDGE=ON requires the locally built, MIT-licensed "
        "Microsoft Detours headers and x64 library under ${_vrclient_rdr2_detours_root}.")
    endif()
    add_library(vrclient_rdr2_bridge SHARED
      src/native/adapters/rdr2/rdr2_bridge.cpp
      adapters/rdr2/native_adapter.cpp
      src/native/adapters/redengine/redengine_detour_transaction.cpp)
    target_include_directories(vrclient_rdr2_bridge
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/native
        ${_vrclient_rdr2_detours_include})
    target_compile_definitions(vrclient_rdr2_bridge
      PRIVATE
        VRCLIENT_ADAPTER_EXPORTS
        VRCLIENT_RDR2_BRIDGE=1
        WIN32_LEAN_AND_MEAN
        NOMINMAX)
    target_link_libraries(vrclient_rdr2_bridge
      PRIVATE
        vr_runtime
        vr_unreal_native
        "${_vrclient_rdr2_detours_library}"
        d3d12
        dxgi
        shell32)
    set_target_properties(vrclient_rdr2_bridge PROPERTIES
      OUTPUT_NAME "vrclient_rdr2_bridge")
    add_custom_command(TARGET vrclient_rdr2_bridge POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Vulkan::Loader>
        $<TARGET_FILE_DIR:vrclient_rdr2_bridge>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:OpenXR::openxr_loader>
        $<TARGET_FILE_DIR:vrclient_rdr2_bridge>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config/defaults/runtime-profile.json
        $<TARGET_FILE_DIR:vrclient_rdr2_bridge>/runtime-profile.json
      COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/artifacts/adapters/rdr2
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:vrclient_rdr2_bridge>
        ${CMAKE_CURRENT_SOURCE_DIR}/artifacts/adapters/rdr2/vrclient_rdr2_bridge.dll
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Vulkan::Loader>
        ${CMAKE_CURRENT_SOURCE_DIR}/artifacts/adapters/rdr2/vulkan-1.dll
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config/defaults/runtime-profile.json
        ${CMAKE_CURRENT_SOURCE_DIR}/artifacts/adapters/rdr2/runtime-profile.json
      COMMENT "RDR2 bridge: stage OpenXR/Vulkan loaders, runtime profile, and smoke artifact")
  endif()

  if(WIN32 AND VRCLIENT_ENABLE_DETOURS_SMOKE)
    add_library(vrclient_meccha_headset_payload SHARED
      src/native/adapters/unreal/meccha_observer_payload.cpp
      src/native/adapters/unreal/meccha_detour_transaction.cpp
      src/native/adapters/unreal/meccha_headset_payload.def)
    target_include_directories(vrclient_meccha_headset_payload
      PRIVATE
        ${_vrclient_detours_include})
    target_compile_definitions(vrclient_meccha_headset_payload
      PRIVATE
        VRCLIENT_MECCHA_HEADSET_BRIDGE=1
        WIN32_LEAN_AND_MEAN
        NOMINMAX)
    target_link_libraries(vrclient_meccha_headset_payload
      PRIVATE
        vr_unreal_native
        vr_runtime
        "${_vrclient_detours_library}"
        d3d12
        dxgi)
    set_target_properties(vrclient_meccha_headset_payload PROPERTIES
      OUTPUT_NAME "vrclient_meccha_headset_payload")
    add_custom_command(TARGET vrclient_meccha_headset_payload POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Vulkan::Loader>
        $<TARGET_FILE_DIR:vrclient_meccha_headset_payload>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:OpenXR::openxr_loader>
        $<TARGET_FILE_DIR:vrclient_meccha_headset_payload>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config/defaults/runtime-profile.json
        $<TARGET_FILE_DIR:vrclient_meccha_headset_payload>/runtime-profile.json
      COMMENT "meccha headset payload: stage OpenXR/Vulkan loaders and runtime profile")

    add_library(vrclient_meccha_scene_relay_payload SHARED
      src/native/adapters/unreal/meccha_observer_payload.cpp
      src/native/adapters/unreal/meccha_detour_transaction.cpp
      src/native/adapters/unreal/meccha_scene_relay_payload.def)
    target_include_directories(vrclient_meccha_scene_relay_payload
      PRIVATE
        ${_vrclient_detours_include})
    target_compile_definitions(vrclient_meccha_scene_relay_payload
      PRIVATE
        VRCLIENT_MECCHA_SCENE_RELAY=1
        WIN32_LEAN_AND_MEAN
        NOMINMAX)
    target_link_libraries(vrclient_meccha_scene_relay_payload
      PRIVATE
        vr_unreal_native
        vr_runtime
        "${_vrclient_detours_library}"
        d3d12
        dxgi)
    set_target_properties(vrclient_meccha_scene_relay_payload PROPERTIES
      OUTPUT_NAME "vrclient_meccha_scene_relay_payload")
    add_custom_command(TARGET vrclient_meccha_scene_relay_payload POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Vulkan::Loader>
        $<TARGET_FILE_DIR:vrclient_meccha_scene_relay_payload>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:OpenXR::openxr_loader>
        $<TARGET_FILE_DIR:vrclient_meccha_scene_relay_payload>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config/defaults/runtime-profile.json
        $<TARGET_FILE_DIR:vrclient_meccha_scene_relay_payload>/runtime-profile.json
      COMMENT "meccha scene relay: stage OpenXR/Vulkan loaders and runtime profile")

    add_library(vrclient_meccha_orientation_payload SHARED
      src/native/adapters/unreal/meccha_observer_payload.cpp
      src/native/adapters/unreal/meccha_detour_transaction.cpp
      src/native/adapters/unreal/meccha_orientation_service.cpp
      src/native/adapters/unreal/meccha_orientation_payload.def)
    target_include_directories(vrclient_meccha_orientation_payload
      PRIVATE
        ${_vrclient_detours_include})
    target_compile_definitions(vrclient_meccha_orientation_payload
      PRIVATE
        VRCLIENT_MECCHA_SCENE_RELAY=1
        VRCLIENT_MECCHA_ORIENTATION_ONLY=1
        WIN32_LEAN_AND_MEAN
        NOMINMAX)
    target_link_libraries(vrclient_meccha_orientation_payload
      PRIVATE
        vr_unreal_native
        vr_runtime
        "${_vrclient_detours_library}"
        bcrypt
        d3d12
        dxgi)
    set_target_properties(vrclient_meccha_orientation_payload PROPERTIES
      OUTPUT_NAME "vrclient_meccha_orientation_payload")
    add_custom_command(TARGET vrclient_meccha_orientation_payload POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Vulkan::Loader>
        $<TARGET_FILE_DIR:vrclient_meccha_orientation_payload>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:OpenXR::openxr_loader>
        $<TARGET_FILE_DIR:vrclient_meccha_orientation_payload>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config/defaults/runtime-profile.json
        $<TARGET_FILE_DIR:vrclient_meccha_orientation_payload>/runtime-profile.json
      COMMENT "meccha orientation-only payload: stage OpenXR/Vulkan loaders and runtime profile")

    add_library(vrclient_meccha_stereo_payload SHARED
      src/native/adapters/unreal/meccha_observer_payload.cpp
      src/native/adapters/unreal/meccha_detour_transaction.cpp
      src/native/adapters/unreal/meccha_orientation_service.cpp
      src/native/adapters/unreal/meccha_stereo_payload.def)
    target_include_directories(vrclient_meccha_stereo_payload
      PRIVATE
        ${_vrclient_detours_include})
    target_compile_definitions(vrclient_meccha_stereo_payload
      PRIVATE
        VRCLIENT_MECCHA_SCENE_RELAY=1
        VRCLIENT_MECCHA_STEREO_RELAY=1
        VRCLIENT_MECCHA_ORIENTATION_ONLY=1
        WIN32_LEAN_AND_MEAN
        NOMINMAX)
    target_link_libraries(vrclient_meccha_stereo_payload
      PRIVATE
        vr_unreal_native
        vr_runtime
        "${_vrclient_detours_library}"
        bcrypt
        d3d12
        dxgi)
    set_target_properties(vrclient_meccha_stereo_payload PROPERTIES
      OUTPUT_NAME "vrclient_meccha_stereo_payload")
    add_custom_command(TARGET vrclient_meccha_stereo_payload POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Vulkan::Loader>
        $<TARGET_FILE_DIR:vrclient_meccha_stereo_payload>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:OpenXR::openxr_loader>
        $<TARGET_FILE_DIR:vrclient_meccha_stereo_payload>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config/defaults/runtime-profile.json
        $<TARGET_FILE_DIR:vrclient_meccha_stereo_payload>/runtime-profile.json
      COMMENT "meccha stereo relay: stage OpenXR/Vulkan loaders and runtime profile")
  endif()

  add_executable(vr_runtime_harness
    src/native/runtime/harness/main.cpp)
  target_link_libraries(vr_runtime_harness PRIVATE vr_runtime vr_diagnostics)

  # Stage the bootstrapped vulkan-1.dll next to the harness so it loads OUR loader
  # (Vulkan::Loader is SHARED IMPORTED -> vulkan-1.dll). Mirrors the probe block.
  add_custom_command(TARGET vr_runtime_harness POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:Vulkan::Loader>
      $<TARGET_FILE_DIR:vr_runtime_harness>
    COMMENT "vr_runtime_harness: stage bootstrapped vulkan-1.dll next to the exe")

  # ---- B3 Headset Validation Harness (smoke mode) ----------------------------
  # Sibling target to vr_runtime_harness: one bounded run of the full OpenXR
  # session lifecycle driving the Vulkan test scene to both eyes, emitting a
  # structured session-evidence JSON. On a host with NO runtime/headset it
  # records a graceful blocker and exits nonzero (never crash/hang). Gated behind
  # VRCLIENT_BUILD_OPENXR_RUNTIME (this whole block), so the default OpenXR-OFF
  # build never compiles it and the CTest gate is unchanged. Deliberately NO
  # add_test: this target is user-hardware-only (needs a Quest + active OpenXR
  # runtime) and must not enter the CTest 44/44 gate.
  add_executable(vrclient_headset_smoke
    src/native/runtime/harness/headset_smoke_main.cpp)
  target_link_libraries(vrclient_headset_smoke PRIVATE vr_runtime vr_diagnostics)

  # Stage the bootstrapped vulkan-1.dll next to the smoke exe (mirrors the
  # harness): the smoke harness loads OUR Release loader, not a system one.
  add_custom_command(TARGET vrclient_headset_smoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:Vulkan::Loader>
      $<TARGET_FILE_DIR:vrclient_headset_smoke>
    COMMENT "vrclient_headset_smoke: stage bootstrapped vulkan-1.dll next to the exe")

  if(VRCLIENT_ENABLE_DETOURS_SMOKE)
    add_library(vrclient_runtime_smoke_payload SHARED
      src/native/injector/runtime_smoke_payload/runtime_smoke_payload_main.cpp
      src/native/injector/runtime_smoke_payload/runtime_smoke_payload.def)
    target_compile_definitions(vrclient_runtime_smoke_payload
      PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX)
    target_include_directories(vrclient_runtime_smoke_payload
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime
        ${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime/public)
    target_link_libraries(vrclient_runtime_smoke_payload PRIVATE vr_runtime)
    set_target_properties(vrclient_runtime_smoke_payload PROPERTIES
      OUTPUT_NAME "vrclient_runtime_smoke_payload")
    add_custom_command(TARGET vrclient_runtime_smoke_payload POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Vulkan::Loader>
        $<TARGET_FILE_DIR:vrclient_runtime_smoke_payload>
      COMMENT "runtime_smoke_payload: stage bootstrapped vulkan-1.dll next to the DLL")
    add_custom_command(TARGET vrclient_runtime_smoke_payload POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:OpenXR::openxr_loader>
        $<TARGET_FILE_DIR:vrclient_runtime_smoke_payload>
      COMMENT "runtime_smoke_payload: stage bootstrapped OpenXR loader next to the DLL")

    add_executable(vrclient_detours_runtime_smoke
      tests/native/injector/detours_runtime_smoke_main.cpp)
    target_compile_definitions(vrclient_detours_runtime_smoke
      PRIVATE
        VRCLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        WIN32_LEAN_AND_MEAN
        NOMINMAX)
    target_link_libraries(vrclient_detours_runtime_smoke
      PRIVATE
        vr_injector_bootstrap
        vr_safety
        vr_versioning)
  endif()
endif()

if(VRCLIENT_BUILD_TESTS)
  add_executable(vr_runtime_unit_tests
    tests/native/runtime/runtime_profile_tests.cpp
    tests/native/runtime/state_transition_tests.cpp)
  target_compile_definitions(vr_runtime_unit_tests
    PRIVATE
      VRCLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(vr_runtime_unit_tests
    PRIVATE
      vr_runtime_frame
      vr_runtime_perf)

  # MVP projection-convention regression (end-state A: honest column-vector).
  # Pure CPU math over stereo_math; runs in the DEFAULT (OpenXR-OFF) build.
  add_executable(vr_stereo_math_tests
    tests/native/runtime/stereo_math_projection_tests.cpp)
  target_compile_definitions(vr_stereo_math_tests
    PRIVATE
      VRCLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(vr_stereo_math_tests
    PRIVATE
      vr_runtime_frame)

  # B7 / PERF-01: deterministic perf-controller frame-budget regression gate.
  # Drives dynamic_resolution / frame_pacing / foveation over SYNTHETIC frame-time
  # sequences (numbers fed directly, never measured) and asserts GOLDEN decisions
  # (the real regression detector), plus ONE loose per-update overhead ceiling
  # (a gross-regression tripwire — the only timing-dependent assertion). Pure CPU
  # over the OpenXR-free vr_runtime_perf lib, so it runs in the DEFAULT
  # (OpenXR-OFF) build and the build-and-test.ps1 gate picks it up automatically.
  add_executable(vr_perf_regression_tests
    tests/native/runtime/perf_regression_tests.cpp)
  target_link_libraries(vr_perf_regression_tests
    PRIVATE
      vr_runtime_perf)

  add_executable(vr_diagnostics_unit_tests
    tests/native/diagnostics/diagnostics_tests.cpp)
  target_compile_definitions(vr_diagnostics_unit_tests
    PRIVATE
      VRCLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(vr_diagnostics_unit_tests
    PRIVATE
      vr_diagnostics)

  add_executable(vr_versioning_unit_tests
    tests/native/versioning/versioning_tests.cpp)
  target_compile_definitions(vr_versioning_unit_tests
    PRIVATE
      VRCLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(vr_versioning_unit_tests
    PRIVATE
      vr_versioning)

  add_executable(vr_injector_unit_tests
    tests/native/injector/injector_tests.cpp)
  target_compile_definitions(vr_injector_unit_tests
    PRIVATE
      VRCLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(vr_injector_unit_tests
    PRIVATE
      vr_injector_process
      vr_injector_safety
      vr_injector_bootstrap
      vr_safety
      vr_versioning)

  add_executable(vr_safety_unit_tests
    tests/native/safety/safety_tests.cpp)
  target_compile_definitions(vr_safety_unit_tests
    PRIVATE
      VRCLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(vr_safety_unit_tests
    PRIVATE
      vr_safety
      vr_injector_safety
      vr_injector_process
      vr_versioning)

  add_executable(vr_plugins_unit_tests
    tests/native/plugins/plugin_tests.cpp)
  target_compile_definitions(vr_plugins_unit_tests
    PRIVATE
      VRCLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(vr_plugins_unit_tests
    PRIVATE
      vr_plugin_host
      vr_plugin_validation)

  add_executable(vr_shared_unit_tests
    tests/native/shared/shared_system_tests.cpp)
  target_compile_definitions(vr_shared_unit_tests
    PRIVATE
      VRCLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(vr_shared_unit_tests
    PRIVATE
      vr_shared_systems)

  add_executable(vr_template_adapter_load_tests
    tests/native/plugins/template_adapter_load_tests.cpp)
  target_link_libraries(vr_template_adapter_load_tests
    PRIVATE
      vr_plugin_host
      vr_shared_systems)
  add_dependencies(vr_template_adapter_load_tests vrclient_template_adapter)

  add_executable(vr_repo_adapter_tests
    tests/native/adapters/repo_adapter_tests.cpp)
  target_compile_definitions(vr_repo_adapter_tests
    PRIVATE
      VRCLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(vr_repo_adapter_tests
    PRIVATE
      vr_plugin_host
      vr_shared_systems)
  add_dependencies(vr_repo_adapter_tests vrclient_repo_adapter)

  add_executable(vr_cyberpunk_adapter_tests
    tests/native/adapters/cyberpunk_adapter_tests.cpp)
  target_link_libraries(vr_cyberpunk_adapter_tests
    PRIVATE
      vr_plugin_host)
  add_dependencies(
    vr_cyberpunk_adapter_tests
    vrclient_cyberpunk_2077_adapter)

  if(TARGET vrclient_redengine_bridge)
    add_executable(vr_red4ext_bridge_tests
      tests/native/adapters/red4ext_bridge_tests.cpp)
    target_include_directories(vr_red4ext_bridge_tests
      PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/native)
    target_compile_definitions(vr_red4ext_bridge_tests
      PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(vr_red4ext_bridge_tests
      PRIVATE RED4ext::SDK vr_redengine_vrcam_selector vr_unreal_native d3d12 dxgi)
    add_dependencies(vr_red4ext_bridge_tests vrclient_redengine_bridge)
  endif()

  if(WIN32)
    add_executable(vr_native_unreal_tests
      tests/native/adapters/native_unreal_tests.cpp)
    target_link_libraries(vr_native_unreal_tests
      PRIVATE
        vr_unreal_native
        vr_d3d12_binding
        d3d12
        dxgi)

    add_executable(vr_meccha_evidence_dispatch_tests
      tests/native/runtime/meccha_evidence_dispatch_tests.cpp)
    target_include_directories(vr_meccha_evidence_dispatch_tests
      PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/native)

    add_executable(vr_meccha_native_adapter_tests
      tests/native/adapters/meccha_native_adapter_tests.cpp)
    target_link_libraries(vr_meccha_native_adapter_tests
      PRIVATE
        vr_plugin_host)
    add_dependencies(
      vr_meccha_native_adapter_tests
      vrclient_meccha_native_adapter)
  endif()

  # -------------------------------------------------------------------------
  # Verification batch (Phase 1-5 autonomous gap closures) — ADDITIVE.
  # New harness sources + new targets/add_tests only; no existing module source
  # list or schema touched. All scratch lands in TEMP / the gitignored build tree.
  #   * DIAG-02: vr_crash_harness deliberately faults so the installed SEH crash
  #     filter writes its self-contained FALLBACK artifact (real crash, child
  #     process). Driven by crash_capture_artifact.ps1.
  #   * DIAG-06: vr_export_run_tests runs exportDiagnosticsBundle against a log
  #     seeded with the LIVE %USERPROFILE% value and asserts real-path redaction +
  #     user_triggered/no-automatic_upload manifest policy.
  #   * Phase 3: vr_runtime_hash_tests computes the SHA-256 of a REAL built
  #     artifact ($<TARGET_FILE:vrclient_smoke_host>) via BCrypt and identifies it
  #     through detectVersion(); cross-checked by validate_runtime_binary_hash.py.
  # -------------------------------------------------------------------------
  add_executable(vr_crash_harness
    tests/native/diagnostics/crash_harness_main.cpp)
  target_link_libraries(vr_crash_harness
    PRIVATE
      vr_diagnostics)

  add_executable(vr_export_run_tests
    tests/native/diagnostics/export_run_tests.cpp)
  target_compile_definitions(vr_export_run_tests
    PRIVATE
      VRCLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(vr_export_run_tests
    PRIVATE
      vr_diagnostics)

  add_executable(vr_runtime_hash_tests
    tests/native/versioning/runtime_hash_tests.cpp)
  target_compile_definitions(vr_runtime_hash_tests
    PRIVATE
      VRCLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(vr_runtime_hash_tests
    PRIVATE
      vr_versioning
      bcrypt)

  # -------------------------------------------------------------------------
  # Requirement-reconcile batch (ADDITIVE — new harness sources + add_tests
  # only; no existing module source list or schema touched). Each closes a
  # specific requirement box with a real, fail-able assertion:
  #   * DIAG-04/DIAG-05: vr_diag_session_log_tests drives the real AsyncLogger
  #     and asserts all six session fields (incl. headset-absent = stopped/none)
  #     and bounded non-blocking enqueue (drop-and-count, never block).
  #   * INJ-05: vr_discovery_sources_tests covers Epic launch + custom path +
  #     direct launch Ready descriptors (Steam/multiple/manual already covered).
  #   * SDK-02: vr_lifecycle_full_tests dispatches ALL seven lifecycle stages
  #     (load/validate/init/tick/suspend/resume/shutdown + reload_config).
  # -------------------------------------------------------------------------
  add_executable(vr_diag_session_log_tests
    tests/native/diagnostics/session_log_fields_tests.cpp)
  target_link_libraries(vr_diag_session_log_tests
    PRIVATE
      vr_diagnostics)

  add_executable(vr_discovery_sources_tests
    tests/native/injector/discovery_sources_tests.cpp)
  target_link_libraries(vr_discovery_sources_tests
    PRIVATE
      vr_injector_process)

  add_executable(vr_lifecycle_full_tests
    tests/native/plugins/lifecycle_full_tests.cpp)
  target_link_libraries(vr_lifecycle_full_tests
    PRIVATE
      vr_plugin_host)

  enable_testing()
  if(TARGET vrclient_d3d12_stereo_probe)
    add_test(NAME vrclient_d3d12_stereo_probe COMMAND vrclient_d3d12_stereo_probe)
    set_tests_properties(vrclient_d3d12_stereo_probe PROPERTIES TIMEOUT 30)
  endif()
  add_test(NAME vr_runtime_unit_tests COMMAND vr_runtime_unit_tests)
  add_test(NAME vr_stereo_math_tests COMMAND vr_stereo_math_tests)
  # B7 / PERF-01: golden perf-controller decisions + loose overhead ceiling.
  # TIMEOUT is a hard backstop for the Part 2 timing loop (the in-test ceiling is
  # the real assertion; this only fires on a true hang).
  add_test(NAME vr_perf_regression_tests COMMAND vr_perf_regression_tests)
  set_tests_properties(vr_perf_regression_tests PROPERTIES TIMEOUT 120)
  add_test(NAME vr_diagnostics_unit_tests COMMAND vr_diagnostics_unit_tests)
  add_test(NAME vr_versioning_unit_tests COMMAND vr_versioning_unit_tests)
  add_test(NAME vr_injector_unit_tests COMMAND vr_injector_unit_tests)
  add_test(NAME vr_safety_unit_tests COMMAND vr_safety_unit_tests)
  add_test(NAME vr_plugins_unit_tests COMMAND vr_plugins_unit_tests)
  add_test(NAME vr_shared_unit_tests COMMAND vr_shared_unit_tests)
  add_test(NAME vr_template_adapter_load_tests
    COMMAND vr_template_adapter_load_tests $<TARGET_FILE:vrclient_template_adapter>)
  add_test(NAME vr_repo_adapter_tests
    COMMAND vr_repo_adapter_tests $<TARGET_FILE:vrclient_repo_adapter>)
  add_test(NAME vr_cyberpunk_adapter_tests
    COMMAND vr_cyberpunk_adapter_tests
      $<TARGET_FILE:vrclient_cyberpunk_2077_adapter>)
  if(TARGET vr_red4ext_bridge_tests)
    add_test(NAME vr_red4ext_bridge_tests
      COMMAND vr_red4ext_bridge_tests $<TARGET_FILE:vrclient_redengine_bridge>)
  endif()
  if(WIN32)
    add_test(NAME vr_native_unreal_tests COMMAND vr_native_unreal_tests)
    set_tests_properties(vr_native_unreal_tests PROPERTIES TIMEOUT 30)
    add_test(NAME vr_meccha_evidence_dispatch_tests
      COMMAND vr_meccha_evidence_dispatch_tests)
    set_tests_properties(vr_meccha_evidence_dispatch_tests PROPERTIES TIMEOUT 30)
    add_test(NAME vr_meccha_native_adapter_tests
      COMMAND vr_meccha_native_adapter_tests
        $<TARGET_FILE:vrclient_meccha_native_adapter>)
    set_tests_properties(vr_meccha_native_adapter_tests PROPERTIES TIMEOUT 30)
  endif()
  add_test(NAME vrclient_smoke_host_once COMMAND vrclient_smoke_host --once)
  if(VRCLIENT_ENABLE_DETOURS_SMOKE)
    add_test(NAME vrclient_meccha_observer_capture
      COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/adapters/meccha_observer_capture.ps1
        -WithDll ${VRCLIENT_DETOURS_WITHDLL}
        -HostExe $<TARGET_FILE:vrclient_meccha_observer_host>
        -PayloadDll $<TARGET_FILE:vrclient_meccha_observer_payload>
        -EvidenceFile ${CMAKE_CURRENT_BINARY_DIR}/meccha-observer/evidence.json)
    set_tests_properties(vrclient_meccha_observer_capture PROPERTIES TIMEOUT 60)

    add_test(NAME vrclient_meccha_observer_late_attach_capture
      COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/adapters/meccha_observer_late_attach_capture.ps1
        -AttachLoader $<TARGET_FILE:vrclient_dll_attach_loader>
        -HostExe $<TARGET_FILE:vrclient_meccha_observer_host>
        -PayloadDll $<TARGET_FILE:vrclient_meccha_observer_payload>
        -EvidenceFile ${CMAKE_CURRENT_BINARY_DIR}/meccha-observer-late/evidence.json)
    set_tests_properties(vrclient_meccha_observer_late_attach_capture
      PROPERTIES TIMEOUT 60)

    if(TARGET vrclient_meccha_headset_payload)
      add_test(NAME vrclient_meccha_headset_payload_settled_attach
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
          -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/adapters/meccha_observer_late_attach_capture.ps1
          -AttachLoader $<TARGET_FILE:vrclient_dll_attach_loader>
          -HostExe $<TARGET_FILE:vrclient_meccha_observer_host>
          -PayloadDll $<TARGET_FILE:vrclient_meccha_headset_payload>
          -EvidenceFile ${CMAKE_CURRENT_BINARY_DIR}/meccha-headset-controlled/evidence.json
          -AttachDelayMilliseconds 6000
          -RequireHeadsetAttempt)
      set_tests_properties(vrclient_meccha_headset_payload_settled_attach
        PROPERTIES TIMEOUT 60)
    endif()
    if(TARGET vrclient_meccha_scene_relay_payload)
      add_test(NAME vrclient_meccha_scene_relay_payload_settled_attach
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
          -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/adapters/meccha_observer_late_attach_capture.ps1
          -AttachLoader $<TARGET_FILE:vrclient_dll_attach_loader>
          -HostExe $<TARGET_FILE:vrclient_meccha_observer_host>
          -PayloadDll $<TARGET_FILE:vrclient_meccha_scene_relay_payload>
          -EvidenceFile ${CMAKE_CURRENT_BINARY_DIR}/meccha-scene-relay-controlled/evidence.json
          -AttachDelayMilliseconds 3000
          -RequireHeadsetAttempt)
      set_tests_properties(vrclient_meccha_scene_relay_payload_settled_attach
        PROPERTIES TIMEOUT 60)
    endif()
    if(TARGET vrclient_meccha_stereo_payload)
      add_test(NAME vrclient_meccha_stereo_payload_settled_attach
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
          -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/adapters/meccha_observer_late_attach_capture.ps1
          -AttachLoader $<TARGET_FILE:vrclient_dll_attach_loader>
          -HostExe $<TARGET_FILE:vrclient_meccha_observer_host>
          -PayloadDll $<TARGET_FILE:vrclient_meccha_stereo_payload>
          -EvidenceFile ${CMAKE_CURRENT_BINARY_DIR}/meccha-stereo-controlled/evidence.json
          -AttachDelayMilliseconds 3000
          -RequireHeadsetAttempt)
      set_tests_properties(vrclient_meccha_stereo_payload_settled_attach
        PROPERTIES TIMEOUT 60)
    endif()

    add_test(NAME vrclient_detours_smoke_capture
      COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/injector/detours_smoke_capture.ps1
        -WithDll ${VRCLIENT_DETOURS_WITHDLL}
        -SmokeHost $<TARGET_FILE:vrclient_smoke_host>
        -PayloadDll $<TARGET_FILE:vrclient_proof_payload>
        -SentinelFile ${CMAKE_CURRENT_BINARY_DIR}/detours-smoke/sentinel.txt)
    set_tests_properties(vrclient_detours_smoke_capture PROPERTIES TIMEOUT 60)
    if(VRCLIENT_BUILD_OPENXR_RUNTIME)
      add_test(NAME vrclient_detours_runtime_smoke_capture
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
          -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/injector/detours_runtime_smoke_capture.ps1
          -Driver $<TARGET_FILE:vrclient_detours_runtime_smoke>
          -WithDll ${VRCLIENT_DETOURS_WITHDLL}
          -SmokeHost $<TARGET_FILE:vrclient_smoke_host>
          -PayloadDll $<TARGET_FILE:vrclient_runtime_smoke_payload>
          -SentinelFile ${CMAKE_CURRENT_BINARY_DIR}/detours-runtime-smoke/sentinel.txt
          -BlockedSentinelFile ${CMAKE_CURRENT_BINARY_DIR}/detours-runtime-smoke/blocked-sentinel.txt
          -RuntimeProfile ${CMAKE_CURRENT_SOURCE_DIR}/config/defaults/runtime-profile.json
          -GameConfig ${CMAKE_CURRENT_SOURCE_DIR}/config/games/sample-game.json
          -SafetyRules ${CMAKE_CURRENT_SOURCE_DIR}/config/safety/default-rules.json
          -LogDir ${CMAKE_CURRENT_BINARY_DIR}/detours-runtime-smoke/logs)
      set_tests_properties(vrclient_detours_runtime_smoke_capture PROPERTIES TIMEOUT 120)
    endif()
  endif()
  if(VRCLIENT_ENABLE_EASYHOOK_ATTACH_SMOKE AND TARGET vrclient_easyhook_attach_smoke)
    add_test(NAME vrclient_easyhook_attach_smoke_capture
      COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/injector/easyhook_attach_smoke_capture.ps1
        -Driver $<TARGET_FILE:vrclient_easyhook_attach_smoke>
        -AttachLoader ${VRCLIENT_EASYHOOK_ATTACH_LOADER}
        -SmokeHost $<TARGET_FILE:vrclient_smoke_host>
        -PayloadDll $<TARGET_FILE:vrclient_proof_payload>
        -SentinelFile ${CMAKE_CURRENT_BINARY_DIR}/easyhook-attach-smoke/sentinel.txt
        -BlockedSentinelFile ${CMAKE_CURRENT_BINARY_DIR}/easyhook-attach-smoke/blocked-sentinel.txt
        -GameConfig ${CMAKE_CURRENT_SOURCE_DIR}/config/games/sample-game.json
        -SafetyRules ${CMAKE_CURRENT_SOURCE_DIR}/config/safety/default-rules.json
        -LogDir ${CMAKE_CURRENT_BINARY_DIR}/easyhook-attach-smoke/logs)
    set_tests_properties(vrclient_easyhook_attach_smoke_capture PROPERTIES TIMEOUT 120)
  endif()

  # Verification batch (Phase 1-5 autonomous gap closures).
  # DIAG-02: PowerShell driver launches the crash harness (real fault) and asserts
  # the SEH-written fallback artifact + final-log-flush leg. Mirrors the
  # vrclient_hookdisc_obs_capture driver pattern; CTest TIMEOUT guards a hang.
  add_test(NAME vr_crash_capture_artifact
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
      -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/diagnostics/crash_capture_artifact.ps1
      -Harness $<TARGET_FILE:vr_crash_harness>
      -WorkDir ${CMAKE_BINARY_DIR}/crash-capture)
  set_tests_properties(vr_crash_capture_artifact PROPERTIES TIMEOUT 180)
  # DIAG-06: in-process diagnostics export run (real %USERPROFILE% redaction).
  add_test(NAME vr_export_run_tests COMMAND vr_export_run_tests)
  set_tests_properties(vr_export_run_tests PROPERTIES TIMEOUT 120)
  # Phase 3: hash a REAL built artifact and identify it through detectVersion().
  add_test(NAME vr_runtime_hash_tests
    COMMAND vr_runtime_hash_tests $<TARGET_FILE:vrclient_smoke_host>)
  set_tests_properties(vr_runtime_hash_tests PROPERTIES TIMEOUT 120)
  # Independent SHA-256 cross-check (Python hashlib) bound to the SAME artifact and
  # the SAME C++ test exe — asserts BCrypt == hashlib byte-for-byte.
  add_test(NAME runtime_binary_hash_crosscheck
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/versioning/validate_runtime_binary_hash.py
      $<TARGET_FILE:vrclient_smoke_host>
      $<TARGET_FILE:vr_runtime_hash_tests>)
  set_tests_properties(runtime_binary_hash_crosscheck PROPERTIES TIMEOUT 120)

  # Requirement-reconcile batch tests.
  # DIAG-04/DIAG-05: real session-log field + non-blocking buffer assertions.
  add_test(NAME vr_diag_session_log_tests COMMAND vr_diag_session_log_tests)
  set_tests_properties(vr_diag_session_log_tests PROPERTIES TIMEOUT 120)
  # INJ-05: Epic / custom-path / direct discovery descriptors.
  add_test(NAME vr_discovery_sources_tests COMMAND vr_discovery_sources_tests)
  # SDK-02: full seven-stage adapter lifecycle dispatch.
  add_test(NAME vr_lifecycle_full_tests COMMAND vr_lifecycle_full_tests)

  add_test(NAME runtime_profile_schema
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/runtime/validate_runtime_profile.py)
  add_test(NAME diagnostics_profile_schema
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/diagnostics/validate_diagnostics_profile.py)
  add_test(NAME game_fingerprint_schema
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/versioning/validate_game_fingerprint.py)
  add_test(NAME repo_fingerprint_schema
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/versioning/validate_game_fingerprint.py config/games/repo.json)
  add_test(NAME game_profile_schema
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/shared/validate_game_profile.py)
  add_test(NAME orchestration_catalog_schema
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/tooling/validate_orchestration_catalog.py)
  add_test(NAME repo_game_profile_schema
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/adapters/validate_repo_profile.py)
  add_test(NAME cyberpunk_redengine_profile_schema
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/adapters/validate_cyberpunk_profile.py)
  add_test(NAME cyberpunk_native_preflight
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/adapters/cyberpunk_native_preflight_tests.py)
  add_test(NAME cyberpunk_adapter_static_checks
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/adapters/cyberpunk_adapter_static_tests.py)
  add_test(NAME rdr2_adapter_static_checks
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/adapters/rdr2_adapter_static_tests.py)
  add_test(NAME cyberpunk_openxr_readiness_patch
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/adapters/cyberpunk_openxr_readiness_patch_tests.py)
  add_test(NAME cyberpunk_vr_hands_install
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/adapters/cyberpunk_vr_hands_install_tests.py)
  add_test(NAME cyberpunk_stereo_arm_safety
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/adapters/cyberpunk_stereo_arm_safety_tests.py)
  add_test(NAME unreal_native_profile_schema
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/adapters/validate_unreal_native_profile.py)
  add_test(NAME meccha_native_preflight
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/adapters/meccha_native_preflight_tests.py)
  add_test(NAME meccha_detour_transaction_static_checks
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/adapters/meccha_detour_transaction_static_tests.py)
  add_test(NAME shared_static_checks
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/shared/shared_static_tests.py)
  add_test(NAME public_api_contract
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/runtime/public_api_contract.py)
  add_test(NAME openxr_pose_sampling_static_checks
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/runtime/openxr_pose_sampling_static_tests.py)
  add_test(NAME hot_path_audit
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/runtime/hot_path_audit.py)
  add_test(NAME diagnostics_static_checks
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/diagnostics/diagnostics_static_tests.py)
  # DIAG-01: single structured logging pipeline across all native runtime layers.
  add_test(NAME diagnostics_single_pipeline
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/diagnostics/single_pipeline_tests.py)
  add_test(NAME versioning_static_checks
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/versioning/versioning_static_tests.py)
  add_test(NAME injector_static_checks
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/injector/injector_static_tests.py)
  add_test(NAME easyhook_attach_static_checks
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/injector/easyhook_attach_static_tests.py)
  add_test(NAME injector_refusal_matrix
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/injector/refusal_matrix_tests.py)
  add_test(NAME safety_rules_schema
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/safety/validate_safety_rules.py)
  add_test(NAME plugin_static_checks
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/plugins/plugin_static_tests.py)
  add_test(NAME plugin_validation_matrix
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/plugins/plugin_validation_tests.py)
  add_test(NAME repo_adapter_static_checks
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/adapters/repo_adapter_static_tests.py)
  # B4 / ADKIT-01: the new-adapter scaffolding CLI test. Pure file-gen + jsonschema
  # into a tempdir (no compile, no in-tree pollution); deterministic. Reuses the
  # existing fingerprint validator function + the game-profile schema to confirm
  # generated configs validate, and asserts the generated adapter declares the 4
  # ABI exports + correct ABI version.
  add_test(NAME new_adapter_cli
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/tooling/new_adapter_cli_tests.py)
  set_tests_properties(new_adapter_cli PROPERTIES TIMEOUT 120)
endif()

# ---------------------------------------------------------------------------
# Bolt-on Phase B2 (additive): hook-discovery & validation toolkit.
# RE-01 inspection, RE-02 read-only observation sandbox, RE-05 safety gate, and
# the RE-04 gating half (hook-surface evidence documents). New targets only — no
# existing target's source list is modified. The byte-patching detour stays
# compile-time disabled (VRCLIENT_HOOKDISC_ENABLE_BYTE_PATCH_DETOUR is never set).
# ---------------------------------------------------------------------------
add_library(vr_tooling_hookdisc STATIC
  src/native/tooling/hookdisc/hook_surface_doc.cpp
  src/native/tooling/hookdisc/inspection.cpp
  src/native/tooling/hookdisc/observe.cpp
  src/native/tooling/hookdisc/safety_gate.cpp
  src/native/tooling/hookdisc/managed_locators.cpp)  # M6 Half A: read-only managed enumeration
target_include_directories(vr_tooling_hookdisc
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime)
target_link_libraries(vr_tooling_hookdisc
  PUBLIC
    vr_injector_safety
    vr_versioning
    vr_injector_process
    vr_diagnostics)

if(WIN32)
  add_library(vrclient_meccha_camera_observer_payload SHARED
    src/native/adapters/unreal/meccha_camera_observer_payload.cpp
    src/native/adapters/unreal/meccha_camera_observer_payload.def)
  target_include_directories(vrclient_meccha_camera_observer_payload
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/native)
  target_compile_definitions(vrclient_meccha_camera_observer_payload
    PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(vrclient_meccha_camera_observer_payload
    PRIVATE vr_unreal_native vr_tooling_hookdisc bcrypt)
  set_target_properties(vrclient_meccha_camera_observer_payload PROPERTIES
    OUTPUT_NAME "vrclient_meccha_camera_observer_payload")
endif()

add_executable(vrclient_hookdisc
  src/native/tooling/hookdisc/hookdisc_cli.cpp)
target_compile_definitions(vrclient_hookdisc
  PRIVATE
    VRCLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vrclient_hookdisc
  PRIVATE
    vr_tooling_hookdisc)

# Cooperative in-target capture host (additive). A SEPARATE, approved process
# that links the hookdisc harness at build time, self-gates via
# runHookSurfaceSafetyGate, runs read-only inspection + HW-breakpoint observation
# on its OWN known function from inside its own process, and emits a schema-valid
# hook-surface JSON. It builds NO cross-process injection primitive (no
# CreateRemoteThread/LoadLibrary); uncooperative delivery remains the gated
# injector's job. The byte-patch detour stays disabled (macro never defined).
add_executable(vrclient_hookdisc_obs_host
  src/native/tooling/hookdisc/obs_host_main.cpp)
target_compile_definitions(vrclient_hookdisc_obs_host
  PRIVATE
    VRCLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vrclient_hookdisc_obs_host
  PRIVATE
    vr_tooling_hookdisc)

if(VRCLIENT_BUILD_TESTS)
  add_executable(vr_hookdisc_unit_tests
    tests/native/tooling/hookdisc_tests.cpp)
  target_compile_definitions(vr_hookdisc_unit_tests
    PRIVATE
      VRCLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(vr_hookdisc_unit_tests
    PRIVATE
      vr_tooling_hookdisc)

  add_test(NAME vr_hookdisc_unit_tests COMMAND vr_hookdisc_unit_tests)

  # M6 Half A (AAA-01/03): read-only managed-locator enumeration over a controlled fixture.
  add_executable(vr_managed_locator_tests
    tests/native/tooling/managed_locator_tests.cpp)
  target_link_libraries(vr_managed_locator_tests PRIVATE vr_tooling_hookdisc)
  add_test(NAME vr_managed_locator_tests
    COMMAND vr_managed_locator_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/tooling/managed_locator_fixture/ManagedLocatorFixture.dll
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/tooling/managed_locator_fixture/global-metadata.fixture.dat)
  add_test(NAME hook_surface_schema
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/tooling/validate_hook_surface.py)
  add_test(NAME hook_surface_schema_repo
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/tooling/validate_hook_surface.py config/hooks/repo.json)
  add_test(NAME hookdisc_static_checks
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/tooling/hookdisc_static_tests.py)

  # In-target cooperative capture proof: launch the gated observation host in a
  # separate process, assert it emits a schema-valid hook-surface JSON with its
  # own function promoted candidate->validated, re-validate via the existing
  # validators, and regression-protect the `inspect repo` refusal. A PowerShell
  # driver is the clean process driver on Windows (timeout guard on hang). The
  # test depends on both the host and the CLI being built first (target-file
  # generator expressions, mirroring vr_repo_adapter_tests).
  add_test(NAME vrclient_hookdisc_obs_capture
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
      -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/tooling/hookdisc_obs_capture.ps1
      -ObsHost $<TARGET_FILE:vrclient_hookdisc_obs_host>
      -HookdiscCli $<TARGET_FILE:vrclient_hookdisc>
      -OutFile ${CMAKE_BINARY_DIR}/hookdisc-obs/captured-smoke.json
      -PythonExe ${Python3_EXECUTABLE}
      -ValidateScript ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/tooling/validate_hook_surface.py)
  # Belt-and-suspenders: the driver's own 60s WaitForExit only guards the obs_host
  # child. If the powershell.exe driver process ITSELF wedges (e.g. blocked reading
  # a redirect file whose handle a killed child still holds), CTest must still reap
  # it. This is the one test in the suite that launches a separate process and arms
  # hardware breakpoints, so it is the single-point-of-failure on the in-driver
  # timeout — cap it at the CTest level too. 180s >> the host's expected sub-second
  # run plus the 60s child guard, so it only fires on a genuine driver hang.
  set_tests_properties(vrclient_hookdisc_obs_capture PROPERTIES TIMEOUT 180)
endif()

# ---------------------------------------------------------------------------
# Bolt-on Phase B5 (additive): config schema versioning & migration.
# CFGV-01 negotiation (LOAD/MIGRATE/REFUSE), CFGV-02 migration framework + fixture
# corpus, CFGV-03 no-rename/no-remove guard, CFGV-04 accepted-version range emitted
# through the existing Phase 2 AsyncLogger. NEW targets only — no existing schema,
# loader, or target source list is touched. The library carries its own
# dependency-free JSON DOM (no JSON library is added).
# ---------------------------------------------------------------------------
add_library(vr_config_versioning STATIC
  src/native/config/config_versioning.cpp)
target_include_directories(vr_config_versioning
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime)
target_link_libraries(vr_config_versioning PUBLIC vr_diagnostics)

if(VRCLIENT_BUILD_TESTS)
  add_executable(vr_config_versioning_unit_tests
    tests/native/config/config_versioning_tests.cpp)
  target_compile_definitions(vr_config_versioning_unit_tests
    PRIVATE
      VRCLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(vr_config_versioning_unit_tests
    PRIVATE
      vr_config_versioning
      vr_diagnostics)

  add_test(NAME vr_config_versioning_unit_tests COMMAND vr_config_versioning_unit_tests)
  add_test(NAME config_versioning_schema
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/config/validate_config_versioning.py)
  add_test(NAME config_versioning_static_checks
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/config/config_versioning_static_tests.py)
  add_test(NAME config_field_guard
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/config/schema_field_guard.py)
endif()

# ---------------------------------------------------------------------------
# Bolt-on Phase B6 (additive): supply-chain integrity and signing.
# SIGN-01..04: schema-guarded trust root / manifest / revocation data, client-side
# RSA/SHA-256 verification layered over SHA-256 hashes, and a Phase 7 safety
# trust observation so unsigned/revoked artifacts block before launch/injection.
# Phase 8 distribution is absent, so this is standalone over test keys/fixtures.
# ---------------------------------------------------------------------------
add_library(vr_supply_chain STATIC
  src/native/supply_chain/artifact_signing.cpp)
target_include_directories(vr_supply_chain
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime)
target_link_libraries(vr_supply_chain PUBLIC vr_config_versioning bcrypt)

# M5 UNIFY-05: verify-only pack-signature CLI (additive).
add_executable(vrclient_sign_cli src/native/supply_chain/cli/vrclient_sign_cli.cpp)
target_link_libraries(vrclient_sign_cli PRIVATE vr_supply_chain)

if(VRCLIENT_BUILD_TESTS)
  add_executable(vr_supply_chain_unit_tests
    tests/native/supply_chain/supply_chain_tests.cpp)
  target_compile_definitions(vr_supply_chain_unit_tests
    PRIVATE
      VRCLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(vr_supply_chain_unit_tests
    PRIVATE
      vr_supply_chain
      vr_safety
      vr_injector_safety
      vr_injector_process
      vr_versioning)

  add_test(NAME vr_supply_chain_unit_tests COMMAND vr_supply_chain_unit_tests)
  add_test(NAME supply_chain_config_schema
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/supply_chain/validate_supply_chain_config.py)
endif()

# ---------------------------------------------------------------------------
# Bolt-on Phase B8 (additive): anti-cheat-aware launch-only routing.
# AC-01..05: reuse Phase 7 data-only detection inputs and represent the B8
# launch-only path as schema-guarded config. This target contains pure routing
# logic only: no process launch, injector attach, hook, patch, or AC interaction.
# ---------------------------------------------------------------------------
add_library(vr_anti_cheat_compat STATIC
  src/native/safety/anti_cheat_launch_router.cpp)
target_include_directories(vr_anti_cheat_compat
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime)
target_link_libraries(vr_anti_cheat_compat PUBLIC vr_safety vr_config_versioning)

if(VRCLIENT_BUILD_TESTS)
  add_executable(vr_anti_cheat_launch_router_tests
    tests/native/safety/anti_cheat_launch_router_tests.cpp)
  target_compile_definitions(vr_anti_cheat_launch_router_tests
    PRIVATE
      VRCLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(vr_anti_cheat_launch_router_tests
    PRIVATE
      vr_anti_cheat_compat
      vr_safety
      vr_injector_safety
      vr_injector_process
      vr_versioning)

  add_test(NAME vr_anti_cheat_launch_router_tests COMMAND vr_anti_cheat_launch_router_tests)
  add_test(NAME anti_cheat_compatibility_schema
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/safety/validate_anti_cheat_compatibility.py)
endif()

# ---------------------------------------------------------------------------
# Vulkan SDK probe (ADDITIVE, dev-only, OFF BY DEFAULT). Renderer-port PREREQUISITE
# verification only — NOT part of the product, the default build, or the CTest 40/40
# suite. It proves the from-source Vulkan SDK bootstrapped by tools/build-vulkan.ps1
# into the gitignored external/vulkan prefix is usable from C++: it links the
# bootstrapped vulkan-1 loader (Vulkan::Loader) + headers (Vulkan::Headers) and runs a
# headless VkInstance -> enumerate physical devices -> VkDevice(one queue) -> teardown.
#
# HARD CONSTRAINTS honored:
#   * Guarded behind VRCLIENT_BUILD_VULKAN_PROBE, which DEFAULTS OFF. The default
#     build (scripts/ci/build-and-test.ps1, OpenXR OFF) never sets it, so this entire
#     block is skipped at configure time and the default build is byte-for-byte
#     unchanged (no new product target, no schema/renderer touched).
#   * The validation layer is DEV-ONLY: it is runtime-loaded by the probe via
#     VK_LAYER_PATH at run time, NEVER linked here and NEVER wired into a product
#     target. Nothing below links VkLayer_khronos_validation.
#   * Self-contained: the only new source is tests/native/tooling/vulkan_probe_main.cpp.
#
# Enable explicitly (separate build dir from the default CI cache):
#   cmake -S . -B build/vk-probe -G Ninja -DVRCLIENT_BUILD_VULKAN_PROBE=ON \
#         -DCMAKE_PREFIX_PATH=<repo>/external/vulkan
# ---------------------------------------------------------------------------
option(VRCLIENT_BUILD_VULKAN_PROBE
  "Build the headless Vulkan SDK probe (dev-only; links external/vulkan loader). OFF by default." OFF)

if(VRCLIENT_BUILD_VULKAN_PROBE)
  # Vulkan::Headers / Vulkan::Loader / glslang were already resolved above (the
  # shared Vulkan-backend block runs when the probe is ON), along with the
  # embedded test-scene SPIR-V header (vr_runtime_shaders / _shader_gen_dir).

  add_executable(vrclient_vulkan_probe
    tests/native/tooling/vulkan_probe_main.cpp)
  target_link_libraries(vrclient_vulkan_probe
    PRIVATE
      Vulkan::Headers
      Vulkan::Loader)

  # The bootstrapped loader is a DLL (Vulkan::Loader is SHARED IMPORTED -> vulkan-1.dll).
  # Copy it next to the probe exe so the headless run finds OUR loader, not the system
  # one. POST_BUILD keeps this self-contained in the build tree (gitignored).
  add_custom_command(TARGET vrclient_vulkan_probe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:Vulkan::Loader>
      $<TARGET_FILE_DIR:vrclient_vulkan_probe>
    COMMENT "vulkan_probe: stage bootstrapped vulkan-1.dll next to the probe exe")

  # -------------------------------------------------------------------------
  # Headless render-path validation (HARD CONSTRAINT #4). Exercises the SAME
  # VulkanTestSceneRenderer the OpenXR runtime uses (initialize ->
  # prepareEyeTargets -> renderEye) over an OFFSCREEN VkImage this exe owns — NO
  # XrSession, NO swapchain. Built only under the (OFF-by-default) probe option,
  # so the default build is unaffected. Compiles the renderer + stereo_math
  # directly (both OpenXR-free); needs the embedded SPIR-V header. Run with
  # VK_LAYER_KHRONOS_validation active -> ZERO validation findings (exit 4 if any).
  # -------------------------------------------------------------------------
  add_executable(vrclient_vulkan_render_validate
    tests/native/tooling/vulkan_render_validate_main.cpp
    src/native/runtime/frame/test_scene_renderer.cpp
    src/native/runtime/frame/stereo_math.cpp)
  add_dependencies(vrclient_vulkan_render_validate vr_runtime_shaders)
  target_include_directories(vrclient_vulkan_render_validate
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime
      ${CMAKE_CURRENT_SOURCE_DIR}/src/native/runtime/public
      ${_shader_gen_root})
  target_compile_definitions(vrclient_vulkan_render_validate
    PRIVATE
      WIN32_LEAN_AND_MEAN
      NOMINMAX)
  target_link_libraries(vrclient_vulkan_render_validate
    PRIVATE
      Vulkan::Headers
      Vulkan::Loader)
  add_custom_command(TARGET vrclient_vulkan_render_validate POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:Vulkan::Loader>
      $<TARGET_FILE_DIR:vrclient_vulkan_render_validate>
    COMMENT "render_validate: stage bootstrapped vulkan-1.dll next to the exe")
endif()

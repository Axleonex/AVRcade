cmake_minimum_required(VERSION 3.24)
project(VRClientFallout3Native LANGUAGES C CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Fallout 3 is a PE32 title; configure from an MSVC x86 environment.")
endif()

set(VRCLIENT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
if(NOT EXISTS "${VRCLIENT_ROOT}/external/openxr/include/openxr/openxr.h")
  message(FATAL_ERROR "OpenXR headers are missing. Bootstrap the pinned project-local OpenXR SDK first.")
endif()

add_library(gamebryo-dx9-openxr STATIC
  "${VRCLIENT_ROOT}/src/native/gamebryo_dx9_openxr/d3d9_openxr_bridge.cpp"
  "${VRCLIENT_ROOT}/src/native/runtime/openxr/legacy_stereo.cpp")
target_compile_features(gamebryo-dx9-openxr PUBLIC cxx_std_20)
target_include_directories(gamebryo-dx9-openxr PUBLIC
  "${VRCLIENT_ROOT}/src/native"
  "${VRCLIENT_ROOT}/external/openxr/include")
target_link_libraries(gamebryo-dx9-openxr PUBLIC d3d9 d3d11 dxgi)
set_property(TARGET gamebryo-dx9-openxr PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(MINHOOK_ROOT "${VRCLIENT_ROOT}/external/cyberpunk-vr-port-reference/externals/minhook")
if(NOT EXISTS "${MINHOOK_ROOT}/CMakeLists.txt")
  message(FATAL_ERROR "The pinned project-local MinHook source is missing.")
endif()
add_subdirectory("${MINHOOK_ROOT}" minhook)
set_property(TARGET minhook PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(vrclient_fallout3_native SHARED native_adapter.cpp camera_stereo.h fallout3_hook_profile.h)
target_compile_features(vrclient_fallout3_native PRIVATE cxx_std_20)
target_link_libraries(vrclient_fallout3_native PRIVATE gamebryo-dx9-openxr minhook bcrypt version)
set_property(TARGET vrclient_fallout3_native PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(MSVC)
  target_compile_options(vrclient_fallout3_native PRIVATE /W4 /WX)
endif()

add_executable(gamebryo-bridge-state-tests
  "${VRCLIENT_ROOT}/src/native/gamebryo_dx9_openxr/d3d9_openxr_bridge.cpp"
  "${VRCLIENT_ROOT}/src/native/runtime/openxr/legacy_stereo.cpp")
target_compile_features(gamebryo-bridge-state-tests PRIVATE cxx_std_20)
target_compile_definitions(gamebryo-bridge-state-tests PRIVATE VRCLIENT_BRIDGE_SELF_TEST_MAIN=1)
target_include_directories(gamebryo-bridge-state-tests PRIVATE
  "${VRCLIENT_ROOT}/src/native"
  "${VRCLIENT_ROOT}/external/openxr/include")
target_link_libraries(gamebryo-bridge-state-tests PRIVATE d3d9 d3d11 dxgi)
set_property(TARGET gamebryo-bridge-state-tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

enable_testing()
add_test(NAME gamebryo-bridge-device-loss-and-stereo-truth COMMAND gamebryo-bridge-state-tests)

add_executable(fallout3-camera-stereo-tests camera_stereo_tests.cpp camera_stereo.h)
target_compile_features(fallout3-camera-stereo-tests PRIVATE cxx_std_20)
set_property(TARGET fallout3-camera-stereo-tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(MSVC)
  target_compile_options(fallout3-camera-stereo-tests PRIVATE /W4 /WX)
endif()
add_test(NAME fallout3-camera-layout-transform-and-independent-eyes COMMAND fallout3-camera-stereo-tests)

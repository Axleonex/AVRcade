cmake_minimum_required(VERSION 3.24)

# GTA San Andreas is a 32-bit process.  This is intentionally a standalone
# project: the main VRClient build is x64 and its OpenXR import library is x64.
# The bridge resolves the x86 OpenXR loader at runtime instead of accidentally
# linking an incompatible loader into the ASI.
project(VRClientGtaSanAndreasBridge LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "The GTA San Andreas bridge is Windows-only.")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR
    "The GTA San Andreas bridge must be configured with the x86 MSVC environment "
    "(vcvars32.bat), not the x64 environment.")
endif()

set(VRCLIENT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

add_library(vrclient_gtasa_theater SHARED
  vrclient_gtasa_theater.cpp)

target_compile_features(vrclient_gtasa_theater PRIVATE cxx_std_20)
target_include_directories(vrclient_gtasa_theater PRIVATE
  "${VRCLIENT_ROOT}/external/openxr/include")
target_compile_definitions(vrclient_gtasa_theater PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX)
target_link_libraries(vrclient_gtasa_theater PRIVATE
  d3d9
  d3d11
  dxgi
  dxguid
  bcrypt)

set_target_properties(vrclient_gtasa_theater PROPERTIES
  OUTPUT_NAME "vrclient_gtasa_theater"
  SUFFIX ".asi"
  WINDOWS_EXPORT_ALL_SYMBOLS OFF)

if(MSVC)
  # The ASI is loaded inside an older x86 game process.  Keep the bridge
  # self-contained instead of requiring the user's system to provide the
  # matching 32-bit MSVC runtime DLLs.
  set_property(TARGET vrclient_gtasa_theater PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

message(STATUS "VRClient GTA SA bridge: x86 ASI experimental stereo mode")

enable_testing()
add_executable(gtasa_vr_behavior_tests
  "${VRCLIENT_ROOT}/tests/native/adapters/gtasa_vr_behavior_tests.cpp")
target_include_directories(gtasa_vr_behavior_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_compile_features(gtasa_vr_behavior_tests PRIVATE cxx_std_20)
add_test(NAME gtasa_vr_behavior COMMAND gtasa_vr_behavior_tests)

add_executable(gtasa_bridge_integration_tests
  "${VRCLIENT_ROOT}/tests/native/adapters/gtasa_bridge_integration_tests.cpp")
target_compile_features(gtasa_bridge_integration_tests PRIVATE cxx_std_20)
target_compile_definitions(gtasa_bridge_integration_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(gtasa_bridge_integration_tests PRIVATE
  "${VRCLIENT_ROOT}/external/openxr/include")
target_link_libraries(gtasa_bridge_integration_tests PRIVATE d3d9 d3d11 dxgi dxguid bcrypt)
target_link_options(gtasa_bridge_integration_tests PRIVATE /BASE:0x20000000 /DYNAMICBASE:NO)
add_test(NAME gtasa_bridge_integration COMMAND gtasa_bridge_integration_tests
  "${VRCLIENT_ROOT}/build/gtasa-x86/openxr_loader.dll"
  "${VRCLIENT_ROOT}/build/gtasa-fake-openxr-x86/fake-openxr.json")

if(MSVC)
  foreach(target IN ITEMS
      vrclient_gtasa_theater
      gtasa_vr_behavior_tests
      gtasa_bridge_integration_tests)
    target_compile_options(${target} PRIVATE /W4 /WX)
  endforeach()
endif()

cmake_minimum_required(VERSION 3.24)

project(VRClientFakeOpenXrRuntime LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "The fake OpenXR runtime is Windows-only.")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "The fake runtime must be built with the x86 MSVC environment.")
endif()

set(VRCLIENT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")

add_library(fake_openxr_runtime SHARED
  fake_openxr_runtime.cpp
  fake_openxr_runtime.def)

add_executable(openxr_loader_probe
  openxr_loader_probe.cpp)

target_compile_features(fake_openxr_runtime PRIVATE cxx_std_20)
target_include_directories(fake_openxr_runtime PRIVATE
  "${VRCLIENT_ROOT}/external/openxr/include")
target_compile_definitions(fake_openxr_runtime PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  XR_NO_PROTOTYPES
  XR_USE_PLATFORM_WIN32
  XR_USE_GRAPHICS_API_D3D11)
target_compile_definitions(openxr_loader_probe PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  XR_NO_PROTOTYPES)
target_include_directories(openxr_loader_probe PRIVATE
  "${VRCLIENT_ROOT}/external/openxr/include")
target_link_libraries(fake_openxr_runtime PRIVATE
  d3d11
  dxgi)

set_target_properties(fake_openxr_runtime PROPERTIES
  OUTPUT_NAME "fake_openxr_runtime"
  SUFFIX ".dll"
  WINDOWS_EXPORT_ALL_SYMBOLS OFF)

set_target_properties(openxr_loader_probe PROPERTIES
  OUTPUT_NAME "openxr_loader_probe")

if(MSVC)
  set_property(TARGET fake_openxr_runtime PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set_property(TARGET openxr_loader_probe PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

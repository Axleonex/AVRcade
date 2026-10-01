add_library(vrclient_template_adapter SHARED
  template_adapter.cpp)

target_compile_definitions(vrclient_template_adapter
  PRIVATE
    VRCLIENT_ADAPTER_EXPORTS)

target_include_directories(vrclient_template_adapter
  PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../src/native)

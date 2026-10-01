#pragma once

#include "public/vr_runtime_api.h"

#include <cstdint>

namespace vrclient::adapters::redengine {

struct RedengineOpenXrStatus {
  bool worker_started = false;
  bool session_started = false;
  VrRuntimeResult start_result = VR_RUNTIME_SKIPPED;
  VrRuntimeResult frame_result = VR_RUNTIME_SKIPPED;
  std::uint64_t frame_count = 0;
  std::uint32_t eye_count = 0;
  bool orientation_valid = false;
  bool position_valid = false;
};

bool startRedengineOpenXrBridge();
void stopRedengineOpenXrBridge();
RedengineOpenXrStatus queryRedengineOpenXrStatus();

}  // namespace vrclient::adapters::redengine

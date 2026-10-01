#pragma once

#include "public/vr_runtime_api.h"

namespace vrclient::runtime::frame {

struct EyeFov {
  float angle_left = 0.0f;
  float angle_right = 0.0f;
  float angle_up = 0.0f;
  float angle_down = 0.0f;
};

VrRuntimeMatrix4 identityMatrix();
VrRuntimeMatrix4 makeProjectionMatrix(EyeFov fov, float near_z, float far_z);
VrRuntimeMatrix4 makeViewMatrix(const VrRuntimePose& pose);
VrRuntimeMatrix4 multiply(const VrRuntimeMatrix4& left, const VrRuntimeMatrix4& right);

}  // namespace vrclient::runtime::frame

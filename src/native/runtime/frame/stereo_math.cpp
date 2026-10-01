#include "frame/stereo_math.h"

#include <cmath>

namespace vrclient::runtime::frame {
namespace {

void zero(VrRuntimeMatrix4& matrix) {
  for (float& value : matrix.m) {
    value = 0.0f;
  }
}

}  // namespace

VrRuntimeMatrix4 identityMatrix() {
  VrRuntimeMatrix4 matrix{};
  matrix.m[0] = 1.0f;
  matrix.m[5] = 1.0f;
  matrix.m[10] = 1.0f;
  matrix.m[15] = 1.0f;
  return matrix;
}

VrRuntimeMatrix4 makeProjectionMatrix(EyeFov fov, float near_z, float far_z) {
  const float tan_left = std::tan(fov.angle_left);
  const float tan_right = std::tan(fov.angle_right);
  const float tan_down = std::tan(fov.angle_down);
  const float tan_up = std::tan(fov.angle_up);
  const float width = tan_right - tan_left;
  const float height = tan_up - tan_down;

  VrRuntimeMatrix4 matrix{};
  zero(matrix);

  matrix.m[0] = 2.0f / width;
  matrix.m[5] = 2.0f / height;
  matrix.m[8] = (tan_right + tan_left) / width;
  matrix.m[9] = (tan_up + tan_down) / height;
  matrix.m[10] = -(far_z + near_z) / (far_z - near_z);
  matrix.m[11] = -1.0f;
  matrix.m[14] = -(2.0f * far_z * near_z) / (far_z - near_z);
  return matrix;
}

VrRuntimeMatrix4 makeViewMatrix(const VrRuntimePose& pose) {
  const float x = pose.orientation.x;
  const float y = pose.orientation.y;
  const float z = pose.orientation.z;
  const float w = pose.orientation.w;

  const float xx = x * x;
  const float yy = y * y;
  const float zz = z * z;
  const float xy = x * y;
  const float xz = x * z;
  const float yz = y * z;
  const float wx = w * x;
  const float wy = w * y;
  const float wz = w * z;

  VrRuntimeMatrix4 matrix = identityMatrix();

  // World->eye view matrix V = [R^T | -R^T p], stored as an honest COLUMN-vector
  // matrix in the m[col*4 + row] layout (the universal convention every consumer
  // now assumes; the renderer multiplies P*V and the shader applies it directly
  // with no transpose -- see test_scene_renderer.cpp / shaders/test_scene.vert).
  //
  // The eye pose from xrLocateViews is camera-to-world: R (= body->world
  // rotation) and p (= eye position in LOCAL space). The view is its inverse.
  // Below we store the 3x3 ROTATION block as R^T directly. R^T equals the
  // quaternion rotation with the imaginary part negated, i.e. the cross terms
  // (wx, wy, wz) flip sign relative to R; the diagonal/quadratic terms are
  // unchanged. (Equivalently: stored[col*4+row] = R[col][row].)
  matrix.m[0] = 1.0f - 2.0f * (yy + zz);
  matrix.m[1] = 2.0f * (xy - wz);
  matrix.m[2] = 2.0f * (xz + wy);

  matrix.m[4] = 2.0f * (xy + wz);
  matrix.m[5] = 1.0f - 2.0f * (xx + zz);
  matrix.m[6] = 2.0f * (yz - wx);

  matrix.m[8] = 2.0f * (xz - wy);
  matrix.m[9] = 2.0f * (yz + wx);
  matrix.m[10] = 1.0f - 2.0f * (xx + yy);

  // Translation -R^T p lives in COLUMN 3 (m[12], m[13], m[14]) for a column-
  // vector matrix. Each component dots the eye position with a ROW of R^T
  // (= a COLUMN of R), giving -(R^T p). Verified by the regression test
  // (stereo_math_projection_tests.cpp): eye pos -> origin (0,0,0,1),
  // eye+forward -> (0,0,-1), translation == (-R^T p) in m[12..14].
  matrix.m[12] = -(pose.position.x * matrix.m[0] +
                   pose.position.y * matrix.m[4] +
                   pose.position.z * matrix.m[8]);
  matrix.m[13] = -(pose.position.x * matrix.m[1] +
                   pose.position.y * matrix.m[5] +
                   pose.position.z * matrix.m[9]);
  matrix.m[14] = -(pose.position.x * matrix.m[2] +
                   pose.position.y * matrix.m[6] +
                   pose.position.z * matrix.m[10]);

  return matrix;
}

VrRuntimeMatrix4 multiply(const VrRuntimeMatrix4& left, const VrRuntimeMatrix4& right) {
  VrRuntimeMatrix4 out{};
  zero(out);

  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      float sum = 0.0f;
      for (int k = 0; k < 4; ++k) {
        sum += left.m[row + k * 4] * right.m[k + col * 4];
      }
      out.m[row + col * 4] = sum;
    }
  }

  return out;
}

}  // namespace vrclient::runtime::frame

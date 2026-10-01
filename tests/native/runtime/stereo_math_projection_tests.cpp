// MVP projection-convention regression test (end-state A: honest column-vector).
//
// Hardened against NDEBUG: this binary may be built Release/NDEBUG (the CI
// helper auto-promotes to Release for the OpenXR-on leg, and this target is
// registered unconditionally). The C `assert()` macro compiles out under
// NDEBUG, turning every check into a vacuous pass -- a trap this project hit
// before (see runtime_profile_tests.cpp). So every check below uses a runtime
// expect() helper that THROWS; the throw escapes main -> std::terminate ->
// non-zero exit, which CTest records as a failure in BOTH Debug and Release.
//
// What this pins (independently derived from first principles, NOT from the
// implementation -- see .planning/bolt-on/mvp-projection-fix-brief.md):
//   1. makeViewMatrix() returns the TRUE column-vector world->eye view V =
//      [R^T | -R^T p]: the eye position maps to the origin, and the translation
//      lives in COLUMN 3 (m[12..14]), not the bottom row (the reverted B3 hack).
//   2. The renderer+shader MVP pipeline, replicated exactly in C++
//      (vp = multiply(projection, view) = P*V; clip = vp * v with NO transpose;
//      clip.y = -clip.y; clip.z = (clip.z + clip.w)*0.5), maps a dead-ahead
//      world point to NDC (0,0) and an off-axis point to its derived NDC.
//   3. GUARDS: reintroducing the transpose, or swapping the multiply order back
//      to V*P, must NOT yield NDC (0,0) for the dead-ahead point. These document
//      and lock out the exact two ways the B3 bug could come back.
//
// Pure CPU math: no Vulkan, no OpenXR. Runs in the DEFAULT (OpenXR-OFF) build.

#include "frame/stereo_math.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace {

using vrclient::runtime::frame::EyeFov;
using vrclient::runtime::frame::makeProjectionMatrix;
using vrclient::runtime::frame::makeViewMatrix;
using vrclient::runtime::frame::multiply;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void expectNear(float actual, float expected, float tol, const std::string& m) {
  if (std::fabs(actual - expected) > tol) {
    throw std::runtime_error(m + " (expected " + std::to_string(expected) +
                             ", got " + std::to_string(actual) + ")");
  }
}

struct Vec4 {
  float x, y, z, w;
};

// Column-major mat4 (the VrRuntimeMatrix4 storage) times a column vector.
Vec4 mulVec(const VrRuntimeMatrix4& mtx, const Vec4& v) {
  Vec4 out{};
  out.x = mtx.m[0] * v.x + mtx.m[4] * v.y + mtx.m[8] * v.z + mtx.m[12] * v.w;
  out.y = mtx.m[1] * v.x + mtx.m[5] * v.y + mtx.m[9] * v.z + mtx.m[13] * v.w;
  out.z = mtx.m[2] * v.x + mtx.m[6] * v.y + mtx.m[10] * v.z + mtx.m[14] * v.w;
  out.w = mtx.m[3] * v.x + mtx.m[7] * v.y + mtx.m[11] * v.z + mtx.m[15] * v.w;
  return out;
}

VrRuntimeMatrix4 transposeMatrix(const VrRuntimeMatrix4& mtx) {
  VrRuntimeMatrix4 out{};
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      out.m[col + row * 4] = mtx.m[row + col * 4];
    }
  }
  return out;
}

// Replicate the shader's GL->Vulkan correction and the perspective divide,
// returning NDC = clip.xyz / clip.w.
Vec4 toNdcWithShaderCorrection(Vec4 clip) {
  clip.y = -clip.y;
  clip.z = (clip.z + clip.w) * 0.5f;
  return Vec4{clip.x / clip.w, clip.y / clip.w, clip.z / clip.w, clip.w};
}

}  // namespace

int runStereoMathProjectionTests() {
  // --- Non-trivial pose: 90deg yaw about +Y, eye at (1, 2, -3). -------------
  // A non-identity rotation + non-zero translation so identity can't sneak a
  // pass. quaternion(90deg yaw +Y) = (0, sin45, 0, cos45).
  VrRuntimePose pose{};
  pose.orientation.x = 0.0f;
  pose.orientation.y = 0.70710678f;
  pose.orientation.z = 0.0f;
  pose.orientation.w = 0.70710678f;
  pose.position.x = 1.0f;
  pose.position.y = 2.0f;
  pose.position.z = -3.0f;
  pose.orientation_valid = 1;
  pose.position_valid = 1;

  const VrRuntimeMatrix4 view = makeViewMatrix(pose);

  // (1) Eye position must map to the camera-space origin (the defining property
  //     of a world->eye view matrix). Vt (the reverted B3 hack) fails this.
  const Vec4 eye_cs = mulVec(view, Vec4{1.0f, 2.0f, -3.0f, 1.0f});
  expectNear(eye_cs.x, 0.0f, 1e-4f, "view*eye must map to origin x");
  expectNear(eye_cs.y, 0.0f, 1e-4f, "view*eye must map to origin y");
  expectNear(eye_cs.z, 0.0f, 1e-4f, "view*eye must map to origin z");
  expectNear(eye_cs.w, 1.0f, 1e-4f, "view*eye must map to origin w");

  // Translation must live in COLUMN 3 (m[12..14]) == (-3,-2,-1), NOT in the
  // bottom row (m[3],m[7],m[11]) which is the B3 transpose hack we reverted.
  expectNear(view.m[12], -3.0f, 1e-4f, "view translation in column 3 (m12)");
  expectNear(view.m[13], -2.0f, 1e-4f, "view translation in column 3 (m13)");
  expectNear(view.m[14], -1.0f, 1e-4f, "view translation in column 3 (m14)");
  expectNear(view.m[3], 0.0f, 1e-4f, "view bottom row must be 0 (no B3 hack)");
  expectNear(view.m[7], 0.0f, 1e-4f, "view bottom row must be 0 (no B3 hack)");
  expectNear(view.m[11], 0.0f, 1e-4f, "view bottom row must be 0 (no B3 hack)");

  // Camera forward (world) = -R[:,2] = (-1,0,0); eye + forward must land on the
  // -Z camera axis: (0, 0, -1).
  const Vec4 fwd_cs = mulVec(view, Vec4{0.0f, 2.0f, -3.0f, 1.0f});
  expectNear(fwd_cs.x, 0.0f, 1e-4f, "eye+forward -> x 0");
  expectNear(fwd_cs.y, 0.0f, 1e-4f, "eye+forward -> y 0");
  expectNear(fwd_cs.z, -1.0f, 1e-4f, "eye+forward -> -1 on camera -Z");

  // --- Symmetric +/-45deg projection, near 0.1 far 100. ---------------------
  EyeFov fov{};
  fov.angle_left = -0.785398163f;   // -45 deg
  fov.angle_right = 0.785398163f;   // +45 deg
  fov.angle_down = -0.785398163f;   // -45 deg
  fov.angle_up = 0.785398163f;      // +45 deg
  const VrRuntimeMatrix4 projection = makeProjectionMatrix(fov, 0.1f, 100.0f);

  // --- Exact renderer+shader MVP convention (end-state A). ------------------
  // Renderer: view_projection = multiply(projection, view) == P*V.
  // Shader:   clip = view_projection * vec4(pos,1) (column vector, NO transpose)
  //           then clip.y = -clip.y; clip.z = (clip.z + clip.w)*0.5.
  const VrRuntimeMatrix4 view_projection = multiply(projection, view);

  // (2a) Dead-ahead world point (eye + 5*forward) = (-4, 2, -3) -> NDC (0,0).
  const Vec4 dead_clip =
      mulVec(view_projection, Vec4{-4.0f, 2.0f, -3.0f, 1.0f});
  const Vec4 dead_ndc = toNdcWithShaderCorrection(dead_clip);
  expectNear(dead_ndc.x, 0.0f, 1e-4f, "dead-ahead NDC.x == 0 (centered)");
  expectNear(dead_ndc.y, 0.0f, 1e-4f, "dead-ahead NDC.y == 0 (centered)");
  expectNear(dead_ndc.z, 0.980981f, 1e-4f, "dead-ahead NDC.z (Vulkan [0,1])");
  expect(dead_ndc.z >= 0.0f && dead_ndc.z <= 1.0f,
         "dead-ahead depth must be inside the Vulkan [0,1] range");

  // (2b) Off-axis discriminating point (eye + 5*fwd + 1*right + 0.5*up)
  //      = (-4, 2.5, -4) -> NDC (0.2, -0.1, 0.980981). This is the value that
  //      separates the correct chain from the B3 bug chain (which gives
  //      ~(0.999, -0.4995, out-of-range)).
  const Vec4 off_clip =
      mulVec(view_projection, Vec4{-4.0f, 2.5f, -4.0f, 1.0f});
  const Vec4 off_ndc = toNdcWithShaderCorrection(off_clip);
  expectNear(off_ndc.x, 0.200000f, 1e-4f, "off-axis NDC.x");
  expectNear(off_ndc.y, -0.100000f, 1e-4f, "off-axis NDC.y (after y-flip)");
  expectNear(off_ndc.z, 0.980981f, 1e-4f, "off-axis NDC.z (Vulkan [0,1])");

  // --- (3) GUARDS: lock out the two ways the B3 bug could return. -----------
  // GUARD A: reintroducing the shader transpose. transpose(P*V)*v must NOT put
  // the dead-ahead point at NDC (0,0). (It lands near (0.304, -0.303).)
  const VrRuntimeMatrix4 transposed_vp = transposeMatrix(view_projection);
  const Vec4 guard_t_ndc = toNdcWithShaderCorrection(
      mulVec(transposed_vp, Vec4{-4.0f, 2.0f, -3.0f, 1.0f}));
  expect(std::fabs(guard_t_ndc.x) > 1e-2f || std::fabs(guard_t_ndc.y) > 1e-2f,
         "GUARD: transpose(viewProjection)*v must NOT centre dead-ahead "
         "(reintroducing the transpose is the B3 bug)");

  // GUARD B: swapping the multiply order back to V*P. multiply(view, projection)
  // then clip = vp*v must NOT centre the dead-ahead point either. (It lands far
  // off-screen, near (-3.94, 1.33).)
  const VrRuntimeMatrix4 wrong_order = multiply(view, projection);
  const Vec4 guard_o_ndc = toNdcWithShaderCorrection(
      mulVec(wrong_order, Vec4{-4.0f, 2.0f, -3.0f, 1.0f}));
  expect(std::fabs(guard_o_ndc.x) > 1e-2f || std::fabs(guard_o_ndc.y) > 1e-2f,
         "GUARD: multiply(view, projection) (wrong order) must NOT centre "
         "dead-ahead (swapping the multiply order is the B3 bug)");

  return 0;
}

int main() {
  // A failed expect() throws -> escapes main -> std::terminate -> non-zero
  // exit, recorded by CTest as a failure (works under Debug AND Release).
  return runStereoMathProjectionTests();
}

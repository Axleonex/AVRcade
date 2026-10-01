#include "camera_stereo.h"
#include <cstdlib>

int main() {
    using namespace vrclient::fallout3;
    Camera base{};
    base.world.rotation[0] = 1.0F;
    base.world.rotation[4] = 1.0F;
    base.world.rotation[8] = 1.0F;
    base.world.scale = 1.0F;
    base.frustum = {-1.0F, 1.0F, 1.0F, -1.0F, 0.1F, 10000.0F, 0, {}};

    VrRuntimePose reference{};
    reference.orientation = {0.0F, 0.0F, 0.0F, 1.0F};
    reference.position_valid = true;
    reference.orientation_valid = true;

    VrRuntimeEyeView left{};
    left.pose = reference;
    left.pose.position.x = -0.032F;
    left.fov_angle_left = -0.8F;
    left.fov_angle_right = 0.7F;
    left.fov_angle_up = 0.75F;
    left.fov_angle_down = -0.75F;
    VrRuntimeEyeView right = left;
    right.pose.position.x = 0.032F;
    right.fov_angle_left = -0.7F;
    right.fov_angle_right = 0.8F;

    Camera leftCamera{};
    Camera rightCamera{};
    if (!MakeEyeCamera(base, reference, left, 70.0F, leftCamera) ||
        !MakeEyeCamera(base, reference, right, 70.0F, rightCamera)) return EXIT_FAILURE;
    if (ViewProjectionSignature(leftCamera) == ViewProjectionSignature(rightCamera)) return EXIT_FAILURE;
    if (leftCamera.world.position[2] == rightCamera.world.position[2]) return EXIT_FAILURE;
    if (!SameRenderedCamera(leftCamera, leftCamera) || SameRenderedCamera(leftCamera, rightCamera))
        return EXIT_FAILURE;

    auto invalid = left;
    invalid.pose.orientation.w = 2.0F;
    Camera rejected{};
    return MakeEyeCamera(base, reference, invalid, 70.0F, rejected) ? EXIT_FAILURE : EXIT_SUCCESS;
}

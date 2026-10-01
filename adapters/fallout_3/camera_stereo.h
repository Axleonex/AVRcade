#pragma once

#include "../../src/native/runtime/public/vr_runtime_api.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace vrclient::fallout3 {

// FOSE's published Fallout 3 1.7.0.3 NiCamera layout. These are plain values:
// the adapter never copies the engine vtable, ownership fields, or refcount.
struct Transform {
    float rotation[9];
    float position[3];
    float scale;
};

struct Frustum {
    float left;
    float right;
    float top;
    float bottom;
    float nearPlane;
    float farPlane;
    std::uint8_t orthographic;
    std::uint8_t padding[3];
};

struct Camera {
    std::array<std::byte, 0x34> object;
    Transform local;
    Transform world;
    float worldToCamera[16];
    Frustum frustum;
    float minimumNearDistance;
    float maximumFarNearRatio;
    float viewport[4];
    float lodAdjust;
};

static_assert(sizeof(Transform) == 0x34);
static_assert(sizeof(Frustum) == 0x1C);
static_assert(offsetof(Camera, world) == 0x68);
static_assert(offsetof(Camera, worldToCamera) == 0x9C);
static_assert(offsetof(Camera, frustum) == 0xDC);
static_assert(sizeof(Camera) == 0x114);

inline bool IsPlausible(const Camera& camera) {
    for (float value : camera.world.rotation) if (!std::isfinite(value)) return false;
    for (float value : camera.world.position) if (!std::isfinite(value)) return false;
    if (!std::isfinite(camera.world.scale) || camera.world.scale <= 0.0F) return false;
    const auto& f = camera.frustum;
    return std::isfinite(f.left) && std::isfinite(f.right) &&
           std::isfinite(f.top) && std::isfinite(f.bottom) &&
           std::isfinite(f.nearPlane) && std::isfinite(f.farPlane) &&
           f.left < f.right && f.bottom < f.top && f.nearPlane > 0.0F &&
           f.farPlane > f.nearPlane && f.orthographic <= 1;
}

inline void CopyView(Camera& destination, const Camera& source) {
    destination.world = source.world;
    destination.frustum = source.frustum;
    std::memcpy(destination.worldToCamera, source.worldToCamera, sizeof(destination.worldToCamera));
}

inline bool ValidPose(const VrRuntimePose& pose) {
    if (!pose.position_valid || !pose.orientation_valid ||
        !std::isfinite(pose.position.x) || !std::isfinite(pose.position.y) ||
        !std::isfinite(pose.position.z)) return false;
    const auto& q = pose.orientation;
    const float norm = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    return std::isfinite(norm) && std::abs(norm - 1.0F) <= 0.01F;
}

inline VrRuntimeQuat Conjugate(VrRuntimeQuat q) { return {-q.x, -q.y, -q.z, q.w}; }

inline VrRuntimeQuat Multiply(VrRuntimeQuat a, VrRuntimeQuat b) {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    };
}

inline VrRuntimeVec3 Rotate(VrRuntimeQuat q, VrRuntimeVec3 v) {
    const auto result = Multiply(Multiply(q, {v.x, v.y, v.z, 0.0F}), Conjugate(q));
    return {result.x, result.y, result.z};
}

inline VrRuntimeVec3 ToWorld(const Transform& base, VrRuntimeVec3 value) {
    // NiCamera columns are direction (+X), up (+Y), right (+Z).
    // OpenXR axes are right (+X), up (+Y), backward (+Z).
    const auto* r = base.rotation;
    return {
        r[2] * value.x + r[1] * value.y - r[0] * value.z,
        r[5] * value.x + r[4] * value.y - r[3] * value.z,
        r[8] * value.x + r[7] * value.y - r[6] * value.z
    };
}

inline void UpdateViewProjection(Camera& camera) {
    const auto& f = camera.frustum;
    const auto* r = camera.world.rotation;
    const float scaleX = 2.0F / (f.right - f.left);
    const float scaleY = 2.0F / (f.top - f.bottom);
    const float offsetX = (f.right + f.left) / (f.right - f.left);
    const float offsetY = (f.top + f.bottom) / (f.top - f.bottom);
    const float depth = f.farPlane / (f.farPlane - f.nearPlane);
    auto* matrix = camera.worldToCamera;
    for (unsigned column = 0; column < 3; ++column) {
        matrix[column] = scaleX * r[column * 3 + 2] - offsetX * r[column * 3];
        matrix[4 + column] = scaleY * r[column * 3 + 1] - offsetY * r[column * 3];
        matrix[8 + column] = depth * r[column * 3];
        matrix[12 + column] = r[column * 3];
    }
    for (unsigned row = 0; row < 4; ++row) {
        matrix[row * 4 + 3] = 0.0F;
        for (unsigned column = 0; column < 3; ++column)
            matrix[row * 4 + 3] -= matrix[row * 4 + column] * camera.world.position[column];
    }
    matrix[11] -= f.nearPlane * depth;
}

inline bool MakeEyeCamera(const Camera& base, const VrRuntimePose& reference,
                          const VrRuntimeEyeView& eye, float unitsPerMeter, Camera& output) {
    if (!IsPlausible(base) || base.frustum.orthographic || !ValidPose(reference) ||
        !ValidPose(eye.pose) || !std::isfinite(unitsPerMeter) || unitsPerMeter <= 0.0F)
        return false;

    const auto inverseReference = Conjugate(reference.orientation);
    const auto delta = Multiply(inverseReference, eye.pose.orientation);
    const float norm = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z + delta.w * delta.w;
    if (!std::isfinite(norm) || std::abs(norm - 1.0F) > 0.01F) return false;

    output = base;
    const auto direction = ToWorld(base.world, Rotate(delta, {0.0F, 0.0F, -1.0F}));
    const auto up = ToWorld(base.world, Rotate(delta, {0.0F, 1.0F, 0.0F}));
    const auto right = ToWorld(base.world, Rotate(delta, {1.0F, 0.0F, 0.0F}));
    const float rotation[]{direction.x, up.x, right.x, direction.y, up.y, right.y,
                           direction.z, up.z, right.z};
    for (unsigned index = 0; index < 9; ++index) output.world.rotation[index] = rotation[index];

    const auto offset = ToWorld(base.world, Rotate(inverseReference,
        {eye.pose.position.x - reference.position.x,
         eye.pose.position.y - reference.position.y,
         eye.pose.position.z - reference.position.z}));
    output.world.position[0] += unitsPerMeter * offset.x;
    output.world.position[1] += unitsPerMeter * offset.y;
    output.world.position[2] += unitsPerMeter * offset.z;
    output.frustum.left = std::tan(eye.fov_angle_left);
    output.frustum.right = std::tan(eye.fov_angle_right);
    output.frustum.top = std::tan(eye.fov_angle_up);
    output.frustum.bottom = std::tan(eye.fov_angle_down);
    if (!IsPlausible(output)) return false;
    UpdateViewProjection(output);
    for (float value : output.worldToCamera) if (!std::isfinite(value)) return false;
    return true;
}

inline bool SameRenderedCamera(const Camera& expected, const Camera& actual) {
    if (!IsPlausible(expected) || !IsPlausible(actual)) return false;
    const auto same = [](float left, float right) {
        return std::isfinite(left) && std::isfinite(right) && std::abs(left - right) <= 0.001F;
    };
    for (unsigned index = 0; index < 3; ++index)
        if (!same(expected.world.position[index], actual.world.position[index])) return false;
    for (unsigned index = 0; index < 9; ++index)
        if (!same(expected.world.rotation[index], actual.world.rotation[index])) return false;
    for (unsigned index = 0; index < 16; ++index)
        if (!same(expected.worldToCamera[index], actual.worldToCamera[index])) return false;
    return true;
}

inline std::uint64_t ViewProjectionSignature(const Camera& camera) {
    // Stable FNV-1a over the values actually installed for this eye.
    std::uint64_t hash = 1469598103934665603ULL;
    const auto add = [&hash](const void* data, std::size_t size) {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        for (std::size_t index = 0; index < size; ++index) {
            hash ^= bytes[index];
            hash *= 1099511628211ULL;
        }
    };
    add(camera.world.rotation, sizeof(camera.world.rotation));
    add(camera.world.position, sizeof(camera.world.position));
    add(camera.worldToCamera, sizeof(camera.worldToCamera));
    add(&camera.frustum, sizeof(camera.frustum));
    return hash == 0 ? 1 : hash;
}

} // namespace vrclient::fallout3

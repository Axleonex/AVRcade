#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace vrclient::fnv {
// Retail 1.4.0.525 candidate ABI, corroborated by the live main-menu camera.
// Plain values only: no imported engine classes, vtables, or owning pointers.
struct Transform {
    float rotation[9];
    float position[3];
    float scale;
};
struct Frustum {
    float left, right, top, bottom, nearPlane, farPlane;
    uint8_t orthographic;
    uint8_t padding[3];
};
struct Camera {
    std::array<std::byte, 0x34> object;
    Transform local;
    Transform world;
    float worldToCamera[16];
    Frustum frustum;
    float minimumNearDistance, maximumFarNearRatio;
    float viewport[4];
    float lodAdjust;
};
static_assert(sizeof(Transform) == 0x34);
static_assert(sizeof(Frustum) == 0x1C);
static_assert(offsetof(Camera, world) == 0x68);
static_assert(offsetof(Camera, worldToCamera) == 0x9C);
static_assert(offsetof(Camera, frustum) == 0xDC);
static_assert(offsetof(Camera, viewport) == 0x100);
static_assert(sizeof(Camera) == 0x114);

// Readability alone doesn't establish camera validity. Reject malformed values
// before any future eye transform is applied. This is not an executable gate.
inline bool IsPlausible(const Camera& camera) {
    for (float v : camera.world.rotation) if (!std::isfinite(v)) return false;
    for (float v : camera.world.position) if (!std::isfinite(v)) return false;
    if (!std::isfinite(camera.world.scale) || camera.world.scale <= 0) return false;
    const auto& f = camera.frustum;
    return std::isfinite(f.left) && std::isfinite(f.right) &&
        std::isfinite(f.top) && std::isfinite(f.bottom) &&
        std::isfinite(f.nearPlane) && std::isfinite(f.farPlane) &&
        f.left < f.right && f.bottom < f.top && f.nearPlane > 0 &&
        f.farPlane > f.nearPlane && f.orthographic <= 1;
}
} // namespace vrclient::fnv

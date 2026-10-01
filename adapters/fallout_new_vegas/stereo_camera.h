#pragma once
#include "camera_layout.h"
#include "../../src/native/runtime/public/vr_runtime_api.h"

namespace vrclient::fnv {
inline bool SameRenderedCamera(const Camera& expected,const Camera& actual) {
    if(!IsPlausible(expected) || !IsPlausible(actual)) return false;
    auto same=[](float a,float b) {
        return std::isfinite(a) && std::isfinite(b) && std::abs(a-b)<=0.001f;
    };
    for(unsigned i=0;i<3;++i)
        if(!same(expected.world.position[i],actual.world.position[i])) return false;
    for(unsigned i=0;i<9;++i)
        if(!same(expected.world.rotation[i],actual.world.rotation[i])) return false;
    for(unsigned i=0;i<16;++i)
        if(!same(expected.worldToCamera[i],actual.worldToCamera[i])) return false;
    const auto& a=expected.frustum; const auto& b=actual.frustum;
    return same(expected.world.scale,actual.world.scale) &&
        same(a.left,b.left) && same(a.right,b.right) && same(a.top,b.top) &&
        same(a.bottom,b.bottom) && same(a.nearPlane,b.nearPlane) &&
        same(a.farPlane,b.farPlane) && a.orthographic==b.orthographic;
}
inline bool ValidPose(const VrRuntimePose& pose) {
    if(!pose.position_valid || !pose.orientation_valid ||
        !std::isfinite(pose.position.x) || !std::isfinite(pose.position.y) ||
        !std::isfinite(pose.position.z)) return false;
    const auto& q=pose.orientation;
    const float norm=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    return std::isfinite(norm) && std::abs(norm-1.0f)<=0.01f;
}
inline VrRuntimeQuat Conjugate(VrRuntimeQuat q) { return {-q.x,-q.y,-q.z,q.w}; }
inline VrRuntimeQuat Multiply(VrRuntimeQuat a, VrRuntimeQuat b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y, a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
        a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w, a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
inline VrRuntimeVec3 Rotate(VrRuntimeQuat q, VrRuntimeVec3 v) {
    auto r = Multiply(Multiply(q,{v.x,v.y,v.z,0}),Conjugate(q));
    return {r.x,r.y,r.z};
}
inline VrRuntimeVec3 ToWorld(const Transform& base, VrRuntimeVec3 v) {
    // NiCamera columns: direction (+X), up (+Y), right (+Z).
    // OpenXR: right (+X), up (+Y), backward (+Z).
    const auto* r = base.rotation;
    return {r[2]*v.x+r[1]*v.y-r[0]*v.z,
        r[5]*v.x+r[4]*v.y-r[3]*v.z, r[8]*v.x+r[7]*v.y-r[6]*v.z};
}
inline void UpdateViewProjection(Camera& camera) {
    const auto& f = camera.frustum;
    const auto* r = camera.world.rotation;
    const float sx = 2/(f.right-f.left), sy = 2/(f.top-f.bottom);
    const float ox = (f.right+f.left)/(f.right-f.left), oy = (f.top+f.bottom)/(f.top-f.bottom);
    const float depth = f.farPlane/(f.farPlane-f.nearPlane);
    auto* m = camera.worldToCamera;
    for (unsigned column=0;column<3;++column) {
        m[column]=sx*r[column*3+2]-ox*r[column*3];
        m[4+column]=sy*r[column*3+1]-oy*r[column*3];
        m[8+column]=depth*r[column*3];
        m[12+column]=r[column*3];
    }
    for (unsigned row=0;row<4;++row) {
        m[row*4+3]=0;
        for(unsigned column=0;column<3;++column) m[row*4+3]-=m[row*4+column]*camera.world.position[column];
    }
    m[11]-=f.nearPlane*depth;
}
inline bool MakeEyeCamera(const Camera& base, const VrRuntimePose& reference,
    const VrRuntimeEyeView& eye, float unitsPerMeter, Camera& output) {
    if (!IsPlausible(base) || base.frustum.orthographic || !ValidPose(reference) ||
        !ValidPose(eye.pose) || !std::isfinite(unitsPerMeter) || unitsPerMeter<=0) return false;
    const auto inverse = Conjugate(reference.orientation);
    const auto delta = Multiply(inverse,eye.pose.orientation);
    const float norm = delta.x*delta.x+delta.y*delta.y+delta.z*delta.z+delta.w*delta.w;
    if (!std::isfinite(norm) || std::abs(norm-1)>0.01f) return false;
    output=base;
    auto direction=ToWorld(base.world,Rotate(delta,{0,0,-1}));
    auto up=ToWorld(base.world,Rotate(delta,{0,1,0}));
    auto right=ToWorld(base.world,Rotate(delta,{1,0,0}));
    const float rotation[]{direction.x,up.x,right.x,direction.y,up.y,right.y,direction.z,up.z,right.z};
    for(unsigned i=0;i<9;++i) output.world.rotation[i]=rotation[i];
    auto offset=ToWorld(base.world,Rotate(inverse,{eye.pose.position.x-reference.position.x,
        eye.pose.position.y-reference.position.y,eye.pose.position.z-reference.position.z}));
    output.world.position[0]+=unitsPerMeter*offset.x;
    output.world.position[1]+=unitsPerMeter*offset.y;
    output.world.position[2]+=unitsPerMeter*offset.z;
    output.frustum.left=std::tan(eye.fov_angle_left); output.frustum.right=std::tan(eye.fov_angle_right);
    output.frustum.top=std::tan(eye.fov_angle_up); output.frustum.bottom=std::tan(eye.fov_angle_down);
    if (!IsPlausible(output)) return false;
    UpdateViewProjection(output);
    for(float v : output.worldToCamera) if(!std::isfinite(v)) return false;
    return true;
}
}

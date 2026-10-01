#include "../../src/native/runtime/public/legacy_stereo.h"
#include "../../adapters/fallout_new_vegas/stereo_camera.h"
#include <cstdio>
#include <string_view>

int wmain(int argc, wchar_t** argv) {
    if(argc==2 && std::wstring_view(argv[1])==L"--self-test") {
        using namespace vrclient::fnv;
        Camera base{};
        const float rotation[]{0,0,1,1,0,0,0,1,0};
        for(unsigned i=0;i<9;++i) base.world.rotation[i]=rotation[i];
        base.world.position[0]=2048; base.world.position[1]=2048; base.world.position[2]=128;
        base.world.scale=1;
        base.frustum={-1.01925051f,1.01925051f,0.575495183f,-0.575495183f,5,10240,0,{}};
        UpdateViewProjection(base);
        // Matches the observed game's projection to float precision.
        if(std::abs(base.worldToCamera[0]-0.9811131f)>0.00001f ||
           std::abs(base.worldToCamera[6]-1.737634f)>0.00001f ||
           std::abs(base.worldToCamera[15]+2048)>0.001f) return 1;
        VrRuntimePose reference{{0,0,0,1},{0,0,0},1,1};
        VrRuntimeEyeView eye{};
        eye.pose=reference; eye.pose.position.x=-0.032f;
        eye.fov_angle_left=-0.8f; eye.fov_angle_right=0.8f;
        eye.fov_angle_up=0.7f; eye.fov_angle_down=-0.7f;
        Camera left{},right{};
        if(!MakeEyeCamera(base,reference,eye,70,left)) return 1;
        eye.pose.position.x=0.032f;
        if(!MakeEyeCamera(base,reference,eye,70,right) ||
            std::abs(right.world.position[0]-left.world.position[0]-4.48f)>0.001f) return 1;
        eye.pose.orientation.w=0;
        if(MakeEyeCamera(base,reference,eye,70,right)) return 1;
        eye.pose=reference;
        reference.position_valid=0;
        if(MakeEyeCamera(base,reference,eye,70,right)) {
            std::puts("FAIL: invalid recenter reference was accepted"); return 1;
        }
        reference.position_valid=1;
        reference.orientation_valid=0;
        if(MakeEyeCamera(base,reference,eye,70,right)) return 1;
        reference.orientation_valid=1;
        reference.orientation.w=2;
        eye.pose.orientation.w=0.5f;
        if(MakeEyeCamera(base,reference,eye,70,right)) return 1;
        Camera changed=left;
        if(!SameRenderedCamera(left,changed)) return 1;
        changed.world.rotation[0]+=0.1f;
        if(SameRenderedCamera(left,changed)) return 1;
        changed=left; changed.frustum.left-=0.1f;
        if(SameRenderedCamera(left,changed)) return 1;
        changed=left; changed.worldToCamera[0]+=0.1f;
        if(SameRenderedCamera(left,changed)) return 1;
        std::puts("FNV projection, eye separation and invalid-pose checks passed");
        return 0;
    }
    if(argc!=2) return 2;
    vrclient::runtime::LegacyStereo runtime;
    if(!runtime.initialize(argv[1],64,64)) { std::puts(runtime.error()); return 1; }
    std::puts("x86 OpenXR device/session/swapchain initialized (not a stereo frame test)");
    return 0;
}

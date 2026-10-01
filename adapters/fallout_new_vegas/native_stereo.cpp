#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <wrl/client.h>
#include <MinHook.h>
#include "stereo_camera.h"
#include "../../src/native/runtime/public/legacy_stereo.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>

namespace {
using namespace vrclient::fnv;
using Microsoft::WRL::ComPtr;
using Render = void (__thiscall*)(void*,void*,int,int);
Render original = nullptr;
HMODULE module = nullptr;
std::atomic<bool> enabled{false};
std::atomic<bool> installed{false};
std::filesystem::path loaderPath, logPath;
std::unique_ptr<vrclient::runtime::LegacyStereo> runtime;
bool attempted = false;
VrRuntimePose reference{};
bool referenceCaptured = false;
unsigned frames = 0;

void Log(const char* message) noexcept {
    if(logPath.empty()) return;
    FILE* file = nullptr;
    if(_wfopen_s(&file,logPath.c_str(),L"a") || !file) return;
    std::fprintf(file,"%llu %s\n",GetTickCount64(),message);
    std::fclose(file);
}
bool Readable(const void* pointer, size_t size) {
    MEMORY_BASIC_INFORMATION region{};
    if(!pointer || VirtualQuery(pointer,&region,sizeof(region))!=sizeof(region) ||
        region.State!=MEM_COMMIT || (region.Protect & (PAGE_GUARD|PAGE_NOACCESS))) return false;
    const auto offset = reinterpret_cast<uintptr_t>(pointer)-reinterpret_cast<uintptr_t>(region.BaseAddress);
    return offset<=region.RegionSize && size<=region.RegionSize-offset;
}
template<class T> T* PointerAt(uintptr_t address) {
    const auto location = reinterpret_cast<T**>(address);
    return Readable(location,sizeof(*location)) ? *location : nullptr;
}
void SetCamera(Camera& to,const Camera& from) {
    // Never overwrite NiObject ownership/refcount fields while restoring a view.
    to.world=from.world;
    to.frustum=from.frustum;
    std::memcpy(to.worldToCamera,from.worldToCamera,sizeof(to.worldToCamera));
}
struct RestoreCamera {
    Camera* target;
    Camera saved;
    ~RestoreCamera() { SetCamera(*target,saved); }
};

void __fastcall HookRender(void* self, void*, void* texture,int arg2,int arg3) noexcept {
    thread_local bool inside = false;
    if(inside) { original(self,texture,arg2,arg3); return; }
    inside=true;
    struct Leave { bool& value; ~Leave(){value=false;} } leave{inside};
    unsigned rendered = 0;
    try {
        if(!enabled.load()) {
            runtime.reset(); attempted=false; referenceCaptured=false;
            original(self,texture,arg2,arg3); return;
        }
        auto* graph = PointerAt<unsigned char>(0x011DEB7C);
        auto* camera = graph ? PointerAt<Camera>(reinterpret_cast<uintptr_t>(graph)+0xAC) : nullptr;
        auto* renderer = PointerAt<unsigned char>(0x011F4748);
        auto* device = renderer ? PointerAt<IDirect3DDevice9>(reinterpret_cast<uintptr_t>(renderer)+0x288) : nullptr;
        if(!Readable(camera,sizeof(Camera)) || !Readable(device,sizeof(void*)) ||
            !IsPlausible(*camera) || camera->frustum.orthographic) {
            original(self,texture,arg2,arg3); return;
        }
        if(device->TestCooperativeLevel()!=D3D_OK) {
            runtime.reset(); attempted=false; referenceCaptured=false;
            original(self,texture,arg2,arg3); return;
        }
        ComPtr<IDirect3DSurface9> target;
        D3DSURFACE_DESC desc{};
        if(FAILED(device->GetRenderTarget(0,&target)) || FAILED(target->GetDesc(&desc))) {
            original(self,texture,arg2,arg3); return;
        }
        if(!attempted) {
            attempted=true;
            runtime=std::make_unique<vrclient::runtime::LegacyStereo>();
            if(!runtime->initialize(loaderPath.c_str(),desc.Width,desc.Height)) {
                Log(runtime->error()); runtime.reset();
            } else Log("OpenXR initialized on game render thread");
        }
        VrRuntimeEyeView eyes[2]{};
        if(!runtime || !runtime->begin(eyes)) { original(self,texture,arg2,arg3); return; }
        if(!referenceCaptured || (GetAsyncKeyState(VK_F8)&1)) {
            reference=eyes[0].pose;
            reference.position={(eyes[0].pose.position.x+eyes[1].pose.position.x)/2,
                (eyes[0].pose.position.y+eyes[1].pose.position.y)/2,
                (eyes[0].pose.position.z+eyes[1].pose.position.z)/2};
            referenceCaptured=true;
        }
        RestoreCamera restore{camera,*camera};
        bool valid = true;
        for(unsigned eye=0;eye<2;++eye) {
            Camera view{};
            // Default Bethesda scale. Calibration remains explicit for headset tests.
            if(!MakeEyeCamera(restore.saved,reference,eyes[eye],70.0f,view)) { valid=false; break; }
            SetCamera(*camera,view);
            original(self,texture,arg2,arg3); ++rendered;
            // Refuse stale-camera output when an engine path overwrites our pose.
            valid=SameRenderedCamera(view,*camera);
            target.Reset();
            if(!valid || FAILED(device->GetRenderTarget(0,&target)) ||
                !runtime->copyEye(device,target.Get(),eye)) { valid=false; break; }
        }
        const bool submitted=runtime->end();
        if(!valid || !submitted) {
            Log(runtime->error()[0] ? runtime->error() : "Stereo frame rejected: camera changed or eye capture incomplete");
            enabled=false; // Fail closed without flooding logs or presenting false stereo.
        } else if(++frames==1 || frames%120==0) {
            char message[80]; std::snprintf(message,sizeof(message),"stereo_frames_submitted=%u",frames); Log(message);
        }
        if(!rendered) original(self,texture,arg2,arg3);
    } catch(...) {
        Log("C++ exception in stereo render; disabling adapter");
        enabled=false;
        if(runtime) runtime->end();
        if(!rendered) original(self,texture,arg2,arg3);
    }
}
}

extern "C" __declspec(dllexport) DWORD WINAPI FnvStart(void*) {
    if(installed.load()) { enabled=true; return 1; }
    wchar_t path[32768]{};
    if(!GetModuleFileNameW(module,path,32768)) return 0;
    auto directory=std::filesystem::path(path).parent_path();
    loaderPath=directory/L"openxr_loader.dll";
    logPath=directory/L"fnv-native.log";
    const unsigned char expected[]{0x55,0x8B,0xEC,0x83,0xEC,0x08,0x89,0x4D,0xF8,0x6A,0x00,0xE8,0xC0,0x04,0xBE,0xFF};
    auto* entry=reinterpret_cast<void*>(0x008706B0);
    if(!Readable(entry,sizeof(expected)) || std::memcmp(entry,expected,sizeof(expected))) {
        Log("Refused unsupported or already-modified render entry"); return 0;
    }
    if(MH_Initialize()!=MH_OK) return 0;
    if(MH_CreateHook(entry,reinterpret_cast<void*>(&HookRender),reinterpret_cast<void**>(&original))!=MH_OK) {
        MH_Uninitialize(); return 0;
    }
    enabled=true;
    if(MH_EnableHook(entry)!=MH_OK) { enabled=false; MH_RemoveHook(entry); MH_Uninitialize(); return 0; }
    installed=true;
    Log("Experimental render hook installed; no stereo frames confirmed yet");
    return 1;
}
extern "C" __declspec(dllexport) DWORD WINAPI FnvStop(void*) { enabled=false; return 1; }
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) { module=instance; DisableThreadLibraryCalls(instance); }
    return TRUE;
}

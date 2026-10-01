#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define XR_NO_PROTOTYPES
#include "../public/legacy_stereo.h"
#include <windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

namespace vrclient::runtime {
using Microsoft::WRL::ComPtr;
#define XR_FUNCTIONS(F) \
 F(DestroyInstance) F(GetSystem) F(GetD3D11GraphicsRequirementsKHR) \
 F(CreateSession) F(DestroySession) F(PollEvent) F(BeginSession) F(EndSession) \
 F(CreateReferenceSpace) F(DestroySpace) F(EnumerateSwapchainFormats) \
 F(CreateSwapchain) F(DestroySwapchain) F(EnumerateSwapchainImages) \
 F(AcquireSwapchainImage) F(WaitSwapchainImage) F(ReleaseSwapchainImage) \
 F(WaitFrame) F(BeginFrame) F(EndFrame) F(LocateViews)

struct LegacyStereo::Impl {
    HMODULE loader = nullptr;
    PFN_xrGetInstanceProcAddr getProc = nullptr;
#define DECLARE(name) PFN_xr##name name = nullptr;
    XR_FUNCTIONS(DECLARE)
#undef DECLARE
    XrInstance instance = XR_NULL_HANDLE;
    XrSession session = XR_NULL_HANDLE;
    XrSpace space = XR_NULL_HANDLE;
    XrSwapchain swapchain = XR_NULL_HANDLE;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDirect3DSurface9> resolve, readback;
    std::vector<XrSwapchainImageD3D11KHR> images;
    std::vector<unsigned char> rgba;
    XrView views[2]{{XR_TYPE_VIEW},{XR_TYPE_VIEW}};
    XrTime time = 0;
    unsigned width = 0, height = 0, image = 0, copied = 0;
    bool running = false, begun = false, acquired = false, waited = false;
    bool rgbaFormat = false, terminal = false;
    char message[192]{};
    bool fail(const char* operation, long result) {
        std::snprintf(message, sizeof(message), "%s failed (%ld)", operation, result);
        return false;
    }
    bool check(XrResult result, const char* operation) {
        return XR_SUCCEEDED(result) || fail(operation, result);
    }
    template<class T> bool load(const char* name, T& function) {
        PFN_xrVoidFunction raw = nullptr;
        if (!check(getProc(instance, name, &raw), name) || !raw) return false;
        function = reinterpret_cast<T>(raw);
        return true;
    }
    bool finish() {
        if (!begun) return false;
        bool valid = copied == 3 && acquired && waited;
        if (acquired && waited) {
            context->Flush();
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            valid = check(ReleaseSwapchainImage(swapchain, &release), "xrReleaseSwapchainImage") && valid;
        } else if (acquired) {
            // A failed wait cannot legally be followed by release; retire session.
            terminal = true;
        }
        XrCompositionLayerProjectionView projectionViews[2]{};
        for (unsigned eye = 0; eye < 2; ++eye) {
            auto& v = projectionViews[eye];
            v.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            v.pose = views[eye].pose;
            v.fov = views[eye].fov;
            v.subImage = {swapchain, {{0,0},{static_cast<int>(width),static_cast<int>(height)}}, eye};
        }
        XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        projection.space = space;
        projection.viewCount = 2;
        projection.views = projectionViews;
        const auto* layer = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
        end.displayTime = time;
        end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end.layerCount = valid ? 1 : 0;
        end.layers = valid ? &layer : nullptr;
        const bool ok = check(EndFrame(session, &end), "xrEndFrame");
        begun = acquired = waited = false;
        copied = 0;
        return ok && valid;
    }
    ~Impl() {
        if (begun) finish();
        if (context) context->Flush();
        readback.Reset(); resolve.Reset();
        if (swapchain && DestroySwapchain) DestroySwapchain(swapchain);
        if (space && DestroySpace) DestroySpace(space);
        if (session && DestroySession) DestroySession(session);
        context.Reset(); device.Reset();
        if (instance && DestroyInstance) DestroyInstance(instance);
        if (loader) FreeLibrary(loader);
    }
};

LegacyStereo::LegacyStereo() : impl_(std::make_unique<Impl>()) {}
LegacyStereo::~LegacyStereo() = default;
const char* LegacyStereo::error() const { return impl_->message; }

bool LegacyStereo::initialize(const wchar_t* path, unsigned width, unsigned height) {
    auto& s = *impl_;
    if (s.instance || !path || !std::filesystem::path(path).is_absolute() ||
        !width || !height || width > 8192 || height > 8192) return s.fail("initialize arguments/state", -1);
    s.width = width; s.height = height;
    s.loader = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!s.loader) return s.fail("load x86 OpenXR loader", GetLastError());
    s.getProc = reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(s.loader,"xrGetInstanceProcAddr"));
    if (!s.getProc) return s.fail("xrGetInstanceProcAddr export", -1);
    PFN_xrCreateInstance createInstance = nullptr;
    if (!s.load("xrCreateInstance", createInstance)) return false;
    const char* extensions[]{XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
    std::strcpy(info.applicationInfo.applicationName, "VRClient Legacy Stereo");
    info.applicationInfo.apiVersion = XR_MAKE_VERSION(1,0,0);
    info.enabledExtensionCount = 1; info.enabledExtensionNames = extensions;
    if (!s.check(createInstance(&info, &s.instance), "xrCreateInstance")) return false;
#define LOAD(name) if (!s.load("xr" #name, s.name)) return false;
    XR_FUNCTIONS(LOAD)
#undef LOAD
    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    if (!s.check(s.GetSystem(s.instance,&systemInfo,&system),"xrGetSystem: connect headset")) return false;
    XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    if (!s.check(s.GetD3D11GraphicsRequirementsKHR(s.instance,system,&requirements),"graphics requirements")) return false;
    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return s.fail("DXGI factory",hr);
    ComPtr<IDXGIAdapter1> adapter;
    for (unsigned index = 0;; ++index) {
        adapter.Reset();
        if (factory->EnumAdapters1(index,&adapter) != S_OK) return s.fail("headset GPU not found",-1);
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
            std::memcmp(&desc.AdapterLuid,&requirements.adapterLuid,sizeof(LUID)) == 0) break;
    }
    D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL chosen{};
    hr = D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels,2,D3D11_SDK_VERSION,&s.device,&chosen,&s.context);
    if (FAILED(hr) || chosen < requirements.minFeatureLevel) return s.fail("D3D11 headset device",hr);
    XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    binding.device = s.device.Get();
    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.next = &binding; sessionInfo.systemId = system;
    if (!s.check(s.CreateSession(s.instance,&sessionInfo,&s.session),"xrCreateSession")) return false;
    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceInfo.poseInReferenceSpace.orientation.w = 1;
    if (!s.check(s.CreateReferenceSpace(s.session,&spaceInfo,&s.space),"xrCreateReferenceSpace")) return false;
    uint32_t count = 0;
    if (!s.check(s.EnumerateSwapchainFormats(s.session,0,&count,nullptr),"swapchain formats") || count > 256) return false;
    std::vector<int64_t> formats(count);
    if (!s.check(s.EnumerateSwapchainFormats(s.session,count,&count,formats.data()),"swapchain formats")) return false;
    int64_t format = DXGI_FORMAT_B8G8R8A8_UNORM;
    if (std::find(formats.begin(),formats.end(),format) == formats.end()) {
        format = DXGI_FORMAT_R8G8B8A8_UNORM; s.rgbaFormat = true;
        if (std::find(formats.begin(),formats.end(),format) == formats.end()) return s.fail("RGBA swapchain format",-1);
    }
    XrSwapchainCreateInfo swapInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapInfo.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    swapInfo.format = format; swapInfo.sampleCount = 1;
    swapInfo.width = width; swapInfo.height = height;
    swapInfo.faceCount = 1; swapInfo.arraySize = 2; swapInfo.mipCount = 1;
    if (!s.check(s.CreateSwapchain(s.session,&swapInfo,&s.swapchain),"xrCreateSwapchain")) return false;
    if (!s.check(s.EnumerateSwapchainImages(s.swapchain,0,&count,nullptr),"swapchain images") || !count || count > 64) return false;
    s.images.assign(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    return s.check(s.EnumerateSwapchainImages(s.swapchain,count,&count,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(s.images.data())),"swapchain images");
}

bool LegacyStereo::begin(VrRuntimeEyeView (&eyes)[2]) {
    auto& s = *impl_;
    if (!s.swapchain || s.begun || s.terminal) return false;
    for (;;) {
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        const auto result = s.PollEvent(s.instance,&event);
        if (result == XR_EVENT_UNAVAILABLE) break;
        if (!s.check(result,"xrPollEvent")) return false;
        if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) { s.terminal = true; return false; }
        if (event.type != XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) continue;
        const auto& state = reinterpret_cast<XrEventDataSessionStateChanged&>(event);
        if (state.session != s.session) continue;
        if (state.state == XR_SESSION_STATE_READY && !s.running) {
            XrSessionBeginInfo info{XR_TYPE_SESSION_BEGIN_INFO};
            info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            if (!s.check(s.BeginSession(s.session,&info),"xrBeginSession")) return false;
            s.running = true;
        } else if (state.state == XR_SESSION_STATE_STOPPING && s.running) {
            s.check(s.EndSession(s.session),"xrEndSession"); s.running = false;
        } else if (state.state == XR_SESSION_STATE_LOSS_PENDING || state.state == XR_SESSION_STATE_EXITING) {
            s.terminal = true; return false;
        }
    }
    if (!s.running) return false;
    XrFrameState frame{XR_TYPE_FRAME_STATE};
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
    if (!s.check(s.WaitFrame(s.session,&wait,&frame),"xrWaitFrame")) return false;
    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    if (!s.check(s.BeginFrame(s.session,&beginInfo),"xrBeginFrame")) return false;
    s.begun = true; s.time = frame.predictedDisplayTime; s.copied = 0;
    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
    locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime = s.time; locate.space = s.space;
    XrViewState state{XR_TYPE_VIEW_STATE};
    uint32_t count = 0;
    const auto flags = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
    if (!frame.shouldRender || !s.check(s.LocateViews(s.session,&locate,&state,2,&count,s.views),"xrLocateViews") ||
        count != 2 || (state.viewStateFlags & flags) != flags) { s.finish(); return false; }
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (!s.check(s.AcquireSwapchainImage(s.swapchain,&acquire,&s.image),"xrAcquireSwapchainImage")) { s.finish(); return false; }
    s.acquired = true;
    XrSwapchainImageWaitInfo waitImage{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitImage.timeout = XR_INFINITE_DURATION;
    if (!s.check(s.WaitSwapchainImage(s.swapchain,&waitImage),"xrWaitSwapchainImage")) { s.finish(); return false; }
    s.waited = true;
    if (s.image >= s.images.size()) { s.finish(); return s.fail("swapchain image index",-1); }
    for (unsigned eye = 0; eye < 2; ++eye) {
        eyes[eye] = {};
        auto& out = eyes[eye]; const auto& v = s.views[eye];
        out.eye = static_cast<VrRuntimeEye>(eye);
        out.pose = {{v.pose.orientation.x,v.pose.orientation.y,v.pose.orientation.z,v.pose.orientation.w},
            {v.pose.position.x,v.pose.position.y,v.pose.position.z},1,1};
        out.fov_angle_left = v.fov.angleLeft; out.fov_angle_right = v.fov.angleRight;
        out.fov_angle_up = v.fov.angleUp; out.fov_angle_down = v.fov.angleDown;
    }
    return true;
}

bool LegacyStereo::copyEye(IDirect3DDevice9* device, IDirect3DSurface9* source, unsigned eye) {
    auto& s = *impl_;
    if (!device || !source || eye > 1 || !s.begun || !s.waited || (s.copied & (1u << eye))) return false;
    D3DSURFACE_DESC desc{};
    if (FAILED(source->GetDesc(&desc)) || desc.Width != s.width || desc.Height != s.height ||
        (desc.Format != D3DFMT_A8R8G8B8 && desc.Format != D3DFMT_X8R8G8B8)) return s.fail("D3D9 source size/format",-1);
    // ponytail: bounded CPU readback for the first legacy test. Replace with
    // shared GPU surfaces only after this path produces verified stereo frames.
    if (!s.readback) {
        HRESULT hr = device->CreateOffscreenPlainSurface(s.width,s.height,desc.Format,D3DPOOL_SYSTEMMEM,&s.readback,nullptr);
        if (FAILED(hr)) return s.fail("D3D9 readback surface",hr);
    }
    if (desc.MultiSampleType != D3DMULTISAMPLE_NONE) {
        if (!s.resolve) {
            HRESULT hr = device->CreateRenderTarget(s.width,s.height,desc.Format,D3DMULTISAMPLE_NONE,0,FALSE,&s.resolve,nullptr);
            if (FAILED(hr)) return s.fail("D3D9 resolve surface",hr);
        }
        HRESULT hr = device->StretchRect(source,nullptr,s.resolve.Get(),nullptr,D3DTEXF_NONE);
        if (FAILED(hr)) return s.fail("D3D9 MSAA resolve",hr);
        source = s.resolve.Get();
    }
    HRESULT hr = device->GetRenderTargetData(source,s.readback.Get());
    if (FAILED(hr)) return s.fail("D3D9 frame readback",hr);
    D3DLOCKED_RECT lock{};
    hr = s.readback->LockRect(&lock,nullptr,D3DLOCK_READONLY);
    if (FAILED(hr)) return s.fail("D3D9 readback lock",hr);
    if (lock.Pitch < static_cast<int>(s.width*4) || !lock.pBits) { s.readback->UnlockRect(); return s.fail("D3D9 pitch",-1); }
    const void* pixels = lock.pBits;
    if (s.rgbaFormat) {
        s.rgba.resize(static_cast<size_t>(s.width)*s.height*4);
        for (unsigned y = 0; y < s.height; ++y) for (unsigned x = 0; x < s.width; ++x) {
            const auto* in = static_cast<unsigned char*>(lock.pBits) + static_cast<size_t>(y)*lock.Pitch + x*4;
            auto* out = s.rgba.data() + (static_cast<size_t>(y)*s.width+x)*4;
            out[0]=in[2]; out[1]=in[1]; out[2]=in[0]; out[3]=255;
        }
        pixels = s.rgba.data();
    }
    s.context->UpdateSubresource(s.images[s.image].texture, D3D11CalcSubresource(0,eye,1),nullptr,
        pixels,s.rgbaFormat ? s.width*4 : static_cast<unsigned>(lock.Pitch),0);
    s.readback->UnlockRect();
    s.copied |= 1u << eye;
    return true;
}
bool LegacyStereo::end() { return impl_->finish(); }
#undef XR_FUNCTIONS
}

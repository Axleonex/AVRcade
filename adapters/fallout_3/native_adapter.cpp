#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <d3d9.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include "camera_stereo.h"
#include "fallout3_hook_profile.h"
#include "../../src/native/gamebryo_dx9_openxr/d3d9_openxr_bridge.h"

#pragma comment(lib, "bcrypt.lib")

namespace {
using vrclient::gamebryo::BridgeConfig;
using vrclient::gamebryo::D3d9OpenXrBridge;
using vrclient::gamebryo::EyeRenderRequest;
using vrclient::gamebryo::TrackingOrigin;
using vrclient::fallout3::Camera;
using Microsoft::WRL::ComPtr;

// Fallout3.exe 1.7.0.3 0x006ECBA0 is a thiscall dispatcher: callers load ECX,
// push three arguments, and the function returns with `ret 0x0C`. It copies the
// active NiCamera world position before selecting the interior/exterior renderer.
using RenderFunction = void (__thiscall*)(void*, void*, bool, std::uint32_t);

HMODULE selfModule{};
Fallout3HookProfile activeProfile{};
std::filesystem::path moduleDirectory;
std::filesystem::path logPath;
std::unique_ptr<D3d9OpenXrBridge> bridge;
RenderFunction originalRender{};
std::atomic<bool> configured{};
std::atomic<bool> hookInstalled{};
bool minHookInitialized{};
bool deviceWasLost{};
VrRuntimePose referencePose{};
bool referencePoseCaptured{};
std::uint64_t submittedFrames{};
std::atomic<std::uint64_t> renderHookCalls{};
std::atomic<std::uint64_t> renderContextSkips{};

bool GetModulePath(HMODULE module, std::filesystem::path& output) {
    std::vector<wchar_t> buffer(512);
    while (buffer.size() <= 32768) {
        SetLastError(ERROR_SUCCESS);
        const DWORD length = GetModuleFileNameW(
            module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!length) return false;
        if (length < buffer.size() - 1 ||
            (length < buffer.size() && GetLastError() != ERROR_INSUFFICIENT_BUFFER)) {
            output = std::filesystem::path(buffer.data(), buffer.data() + length);
            return true;
        }
        buffer.resize(buffer.size() * 2);
    }
    return false;
}

bool EnsureModulePaths() {
    if (!moduleDirectory.empty() && !logPath.empty()) return true;
    std::filesystem::path modulePath;
    if (!selfModule || !GetModulePath(selfModule, modulePath)) return false;
    moduleDirectory = modulePath.parent_path();
    logPath = moduleDirectory / L"fallout3-native.log";
    return true;
}

void Log(const std::string& message) noexcept {
    if (logPath.empty()) return;
    FILE* file{};
    if (_wfopen_s(&file, logPath.c_str(), L"a") != 0 || !file) return;
    std::fprintf(file, "%llu %s\n", GetTickCount64(), message.c_str());
    std::fclose(file);
}

std::string Hex(const unsigned char* bytes, std::size_t count) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string value(count * 2, '0');
    for (std::size_t index = 0; index < count; ++index) {
        value[index * 2] = digits[bytes[index] >> 4];
        value[index * 2 + 1] = digits[bytes[index] & 15];
    }
    return value;
}

bool CurrentExecutableSha256(std::string& output) {
    std::filesystem::path path;
    if (!GetModulePath(nullptr, path)) return false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    DWORD objectSize{}, bytes{};
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
              BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize),
                                sizeof(objectSize), &bytes, 0) == 0;
    std::vector<unsigned char> object(objectSize), buffer(64 * 1024), digest(32);
    if (ok) ok = BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) == 0;
    while (ok) {
        DWORD read{};
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) { ok = false; break; }
        if (!read) break;
        if (BCryptHashData(hash, buffer.data(), read, 0) != 0) { ok = false; break; }
    }
    if (ok) ok = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    if (ok) output = Hex(digest.data(), digest.size());
    return ok;
}

bool ReadProfileValue(const std::filesystem::path& path, const wchar_t* key,
                      wchar_t* output, DWORD outputCount) {
    return GetPrivateProfileStringW(L"HookProfile", key, L"", output, outputCount,
                                    path.c_str()) > 0;
}

bool ParseUnsigned(const std::filesystem::path& path, const wchar_t* key, std::uint32_t& output) {
    wchar_t text[64]{};
    if (!ReadProfileValue(path, key, text, static_cast<DWORD>(std::size(text)))) return false;
    wchar_t* end{};
    const auto value = std::wcstoul(text, &end, 0);
    if (end == text || *end != L'\0' || value > UINT32_MAX) return false;
    output = static_cast<std::uint32_t>(value);
    return true;
}

bool ParseProfileFile(const std::filesystem::path& path, Fallout3HookProfile& profile) {
    if (!std::filesystem::is_regular_file(path)) return false;
    profile = {};
    profile.structSize = sizeof(profile);
    profile.interfaceVersion = Fallout3HookInterfaceVersion;

    wchar_t hash[80]{};
    if (!ReadProfileValue(path, L"ExecutableSha256", hash, static_cast<DWORD>(std::size(hash))) ||
        std::wcslen(hash) != 64) return false;
    for (std::size_t index = 0; index < 64; ++index) {
        if (hash[index] > 0x7f || !std::iswxdigit(hash[index])) return false;
        profile.expectedExecutableSha256[index] = static_cast<char>(hash[index]);
    }

    if (!ParseUnsigned(path, L"RenderHookRva", profile.renderHookRva) ||
        !ParseUnsigned(path, L"RenderContextPointerRva", profile.renderContextPointerRva) ||
        !ParseUnsigned(path, L"SceneGraphPointerRva", profile.sceneGraphPointerRva) ||
        !ParseUnsigned(path, L"RendererPointerRva", profile.rendererPointerRva) ||
        !ParseUnsigned(path, L"CameraOffset", profile.cameraOffset) ||
        !ParseUnsigned(path, L"DeviceOffset", profile.deviceOffset)) return false;

    wchar_t bytes[128]{};
    if (!ReadProfileValue(path, L"RenderEntryBytes", bytes, static_cast<DWORD>(std::size(bytes))))
        return false;
    std::wstring compact;
    for (const wchar_t value : std::wstring(bytes))
        if (!std::iswspace(value) && value != L',' && value != L'-') compact.push_back(value);
    if (compact.size() < 10 || compact.size() > sizeof(profile.expectedRenderEntry) * 2 ||
        compact.size() % 2 != 0) return false;
    profile.renderEntryByteCount = static_cast<std::uint32_t>(compact.size() / 2);
    for (std::size_t index = 0; index < profile.renderEntryByteCount; ++index) {
        wchar_t pair[]{compact[index * 2], compact[index * 2 + 1], L'\0'};
        wchar_t* end{};
        const auto value = std::wcstoul(pair, &end, 16);
        if (end != pair + 2 || value > 0xff) return false;
        profile.expectedRenderEntry[index] = static_cast<std::uint8_t>(value);
    }

    wchar_t scale[64]{};
    if (!ReadProfileValue(path, L"UnitsPerMeter", scale, static_cast<DWORD>(std::size(scale))))
        return false;
    wchar_t* scaleEnd{};
    profile.unitsPerMeter = std::wcstof(scale, &scaleEnd);
    if (scaleEnd == scale || *scaleEnd != L'\0') return false;
    std::uint32_t standing{};
    if (!ParseUnsigned(path, L"StandingOrigin", standing) || standing > 1) return false;
    profile.standingOrigin = static_cast<std::uint8_t>(standing);
    return true;
}

bool ValidRva(std::uint32_t rva) {
    if (!rva) return false;
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE && rva < nt->OptionalHeader.SizeOfImage;
}

bool Readable(const void* pointer, std::size_t size) {
    MEMORY_BASIC_INFORMATION region{};
    if (!pointer || VirtualQuery(pointer, &region, sizeof(region)) != sizeof(region) ||
        region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const auto offset = reinterpret_cast<std::uintptr_t>(pointer) -
                        reinterpret_cast<std::uintptr_t>(region.BaseAddress);
    return offset <= region.RegionSize && size <= region.RegionSize - offset;
}

template <class T>
T* PointerAt(std::uintptr_t address) {
    const auto location = reinterpret_cast<T**>(address);
    return Readable(location, sizeof(*location)) ? *location : nullptr;
}

bool ValidProfile(const Fallout3HookProfile& profile) {
    return profile.structSize == sizeof(Fallout3HookProfile) &&
           profile.interfaceVersion == Fallout3HookInterfaceVersion &&
           strnlen_s(profile.expectedExecutableSha256,
                     std::size(profile.expectedExecutableSha256)) == 64 &&
           ValidRva(profile.renderHookRva) && ValidRva(profile.renderContextPointerRva) &&
           ValidRva(profile.sceneGraphPointerRva) &&
           ValidRva(profile.rendererPointerRva) && profile.cameraOffset <= 0x10000 &&
           profile.deviceOffset <= 0x10000 && profile.renderEntryByteCount >= 5 &&
           profile.renderEntryByteCount <= std::size(profile.expectedRenderEntry) &&
           std::isfinite(profile.unitsPerMeter) && profile.unitsPerMeter > 0.0F &&
           profile.unitsPerMeter <= 10000.0F && profile.standingOrigin <= 1;
}

bool ExpectedRenderEntryMatches(const Fallout3HookProfile& profile) {
    const auto* base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    const auto* entry = base + profile.renderHookRva;
    return Readable(entry, profile.renderEntryByteCount) &&
           std::memcmp(entry, profile.expectedRenderEntry, profile.renderEntryByteCount) == 0;
}

bool EnsureBridge(IDirect3DDevice9* device, std::uint32_t width, std::uint32_t height) {
    if (bridge) return true;
    const auto loader = moduleDirectory / L"openxr_loader.dll";
    if (!std::filesystem::is_regular_file(loader)) {
        Log("OpenXR initialization refused: x86 openxr_loader.dll missing");
        return false;
    }
    bridge = std::make_unique<D3d9OpenXrBridge>();
    BridgeConfig config{loader.wstring(), width, height,
        activeProfile.standingOrigin ? TrackingOrigin::Standing : TrackingOrigin::Seated};
    if (!bridge->configure(std::move(config), [](const auto& diagnostics) { Log(diagnostics.lastMessage); })) {
        bridge.reset();
        return false;
    }
    (void)device;
    return true;
}

struct RestoreCamera {
    Camera* target;
    Camera saved;
    ~RestoreCamera() { if (target) vrclient::fallout3::CopyView(*target, saved); }
};

void DisableStereo(const char* reason) {
    Log(reason);
    configured.store(false, std::memory_order_release);
    referencePoseCaptured = false;
    if (bridge) bridge->stop();
    bridge.reset();
}

void __fastcall HookRender(void* self, void*, void* renderContext,
                           bool allowAlternate, std::uint32_t flags) noexcept {
    const auto callNumber = renderHookCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    thread_local bool insideHook{};
    if (!originalRender) return;
    if (insideHook || !configured.load(std::memory_order_acquire)) {
        originalRender(self, renderContext, allowAlternate, flags);
        return;
    }
    insideHook = true;
    struct LeaveHook { bool& value; ~LeaveHook() { value = false; } } leave{insideHook};

    bool stereoComplete{};
    try {
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        auto* mainRenderContext = PointerAt<std::uint8_t>(base + activeProfile.renderContextPointerRva);
        if (!mainRenderContext || renderContext != mainRenderContext) {
            const auto skipped = renderContextSkips.fetch_add(1, std::memory_order_relaxed) + 1;
            if (skipped == 1 || skipped % 600 == 0) {
                char message[192]{};
                std::snprintf(message, sizeof(message),
                    "fallout3_render_dispatch call=%llu skipped_context=%llu actual=%p expected=%p",
                    static_cast<unsigned long long>(callNumber),
                    static_cast<unsigned long long>(skipped), renderContext, mainRenderContext);
                Log(message);
            }
            originalRender(self, renderContext, allowAlternate, flags);
            return;
        }
        auto* sceneGraph = PointerAt<std::uint8_t>(base + activeProfile.sceneGraphPointerRva);
        auto* renderer = PointerAt<std::uint8_t>(base + activeProfile.rendererPointerRva);
        auto* camera = sceneGraph
            ? PointerAt<Camera>(reinterpret_cast<std::uintptr_t>(sceneGraph) + activeProfile.cameraOffset)
            : nullptr;
        auto* device = renderer
            ? PointerAt<IDirect3DDevice9>(reinterpret_cast<std::uintptr_t>(renderer) + activeProfile.deviceOffset)
            : nullptr;
        if (!Readable(camera, sizeof(Camera)) || !Readable(device, sizeof(void*)) ||
            !vrclient::fallout3::IsPlausible(*camera) || camera->frustum.orthographic) {
            originalRender(self, renderContext, allowAlternate, flags);
            return;
        }

        const HRESULT cooperative = device->TestCooperativeLevel();
        if (cooperative != D3D_OK) {
            if (!deviceWasLost && bridge) bridge->onDeviceLost();
            deviceWasLost = true;
            originalRender(self, renderContext, allowAlternate, flags);
            return;
        }

        ComPtr<IDirect3DSurface9> target;
        D3DSURFACE_DESC description{};
        if (FAILED(device->GetRenderTarget(0, &target)) || FAILED(target->GetDesc(&description))) {
            originalRender(self, renderContext, allowAlternate, flags);
            return;
        }
        if (deviceWasLost) {
            if (bridge) bridge->onResetComplete(description.Width, description.Height);
            deviceWasLost = false;
        }
        if (!EnsureBridge(device, description.Width, description.Height)) {
            originalRender(self, renderContext, allowAlternate, flags);
            return;
        }

        EyeRenderRequest eyes[2]{};
        if (!bridge->beginFrame(eyes)) {
            originalRender(self, renderContext, allowAlternate, flags);
            return;
        }
        if (!referencePoseCaptured || (GetAsyncKeyState(VK_F8) & 1)) {
            referencePose = eyes[0].view.pose;
            referencePose.position = {
                (eyes[0].view.pose.position.x + eyes[1].view.pose.position.x) * 0.5F,
                (eyes[0].view.pose.position.y + eyes[1].view.pose.position.y) * 0.5F,
                (eyes[0].view.pose.position.z + eyes[1].view.pose.position.z) * 0.5F
            };
            referencePoseCaptured = true;
            Log("OpenXR reference pose captured (F8 recenters)");
        }

        {
            RestoreCamera restore{camera, *camera};
            bool eyesValid = true;
            for (std::uint32_t eye = 0; eye < 2; ++eye) {
                Camera eyeCamera{};
                if (!vrclient::fallout3::MakeEyeCamera(restore.saved, referencePose, eyes[eye].view,
                                                       activeProfile.unitsPerMeter, eyeCamera)) {
                    char message[192]{};
                    std::snprintf(message, sizeof(message),
                        "fallout3_stereo_rejected eye=%u stage=make_eye_camera base_plausible=%u "
                        "reference_pose_valid=%u eye_pose_valid=%u fov=[%.4f,%.4f,%.4f,%.4f]",
                        eye, vrclient::fallout3::IsPlausible(restore.saved) ? 1U : 0U,
                        vrclient::fallout3::ValidPose(referencePose) ? 1U : 0U,
                        vrclient::fallout3::ValidPose(eyes[eye].view.pose) ? 1U : 0U,
                        eyes[eye].view.fov_angle_left, eyes[eye].view.fov_angle_right,
                        eyes[eye].view.fov_angle_up, eyes[eye].view.fov_angle_down);
                    Log(message);
                    eyesValid = false;
                    break;
                }
                vrclient::fallout3::CopyView(*camera, eyeCamera);
                originalRender(self, renderContext, allowAlternate, flags);
                if (!vrclient::fallout3::SameRenderedCamera(eyeCamera, *camera)) {
                    char message[192]{};
                    std::snprintf(message, sizeof(message),
                        "fallout3_stereo_rejected eye=%u stage=rendered_camera "
                        "actual_plausible=%u position_delta=[%.4f,%.4f,%.4f]",
                        eye, vrclient::fallout3::IsPlausible(*camera) ? 1U : 0U,
                        camera->world.position[0] - eyeCamera.world.position[0],
                        camera->world.position[1] - eyeCamera.world.position[1],
                        camera->world.position[2] - eyeCamera.world.position[2]);
                    Log(message);
                    eyesValid = false;
                    break;
                }
                target.Reset();
                const HRESULT targetResult = device->GetRenderTarget(0, &target);
                if (FAILED(targetResult) || !target) {
                    char message[128]{};
                    std::snprintf(message, sizeof(message),
                        "fallout3_stereo_rejected eye=%u stage=render_target hresult=0x%08lx",
                        eye, static_cast<unsigned long>(targetResult));
                    Log(message);
                    eyesValid = false;
                    break;
                }
                if (!bridge->submitEye(device, target.Get(), eye,
                        vrclient::fallout3::ViewProjectionSignature(eyeCamera))) {
                    char message[96]{};
                    std::snprintf(message, sizeof(message),
                        "fallout3_stereo_rejected eye=%u stage=submit_eye", eye);
                    Log(message);
                    eyesValid = false;
                    break;
                }
            }
            stereoComplete = eyesValid && bridge->endFrame();
            if (!stereoComplete && eyesValid) Log("OpenXR rejected an incomplete Fallout 3 stereo frame");
        }

        if (!stereoComplete) {
            // Restore an ordinary desktop frame so a partial eye render is never presented.
            originalRender(self, renderContext, allowAlternate, flags);
            DisableStereo("Fallout 3 stereo hook failed closed; desktop rendering restored");
        } else if (++submittedFrames == 1 || submittedFrames % 120 == 0) {
            char message[96]{};
            std::snprintf(message, sizeof(message), "fallout3_independent_stereo_frames=%llu",
                          static_cast<unsigned long long>(submittedFrames));
            Log(message);
        }
    } catch (...) {
        originalRender(self, renderContext, allowAlternate, flags);
        DisableStereo("C++ exception in Fallout 3 render hook; desktop rendering restored");
    }
}

bool InstallRenderHook() {
    auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    auto* entry = base + activeProfile.renderHookRva;
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) {
        Log("render hook refused: MinHook initialization failed");
        return false;
    }
    minHookInitialized = true;
    if (MH_CreateHook(entry, reinterpret_cast<void*>(&HookRender),
                      reinterpret_cast<void**>(&originalRender)) != MH_OK) {
        MH_Uninitialize();
        minHookInitialized = false;
        Log("render hook refused: trampoline creation failed");
        return false;
    }
    {
        char message[192]{};
        std::snprintf(message, sizeof(message),
            "render hook addresses entry=%p detour=%p trampoline=%p",
            entry, reinterpret_cast<void*>(&HookRender),
            reinterpret_cast<void*>(originalRender));
        Log(message);
        if (Readable(reinterpret_cast<void*>(originalRender), 32))
            Log("render hook trampoline bytes=" +
                Hex(reinterpret_cast<const unsigned char*>(originalRender), 32));
    }
    if (MH_EnableHook(entry) != MH_OK) {
        MH_RemoveHook(entry);
        MH_Uninitialize();
        minHookInitialized = false;
        originalRender = nullptr;
        Log("render hook refused: activation failed");
        return false;
    }
    hookInstalled.store(true, std::memory_order_release);
    Log("Fallout 3 exact-build camera/render hook installed");
    return true;
}
}

extern "C" __declspec(dllexport) bool Fallout3VrConfigure(const Fallout3HookProfile* profile) {
    if (!profile || !ValidProfile(*profile)) {
        Log("configuration refused: invalid v3 interface, exact hash, camera path, render bytes, or scale");
        return false;
    }
    if (hookInstalled.load(std::memory_order_acquire)) {
        Log("configuration refused: render hook is already installed");
        return false;
    }
    std::string actual;
    if (!CurrentExecutableSha256(actual) || _stricmp(actual.c_str(), profile->expectedExecutableSha256) != 0) {
        Log("configuration refused: Fallout3.exe SHA-256 does not match the reviewed build profile");
        return false;
    }
    if (!ExpectedRenderEntryMatches(*profile)) {
        Log("configuration refused: render entry bytes do not match the reviewed build profile");
        return false;
    }
    activeProfile = *profile;
    referencePoseCaptured = false;
    submittedFrames = 0;
    renderHookCalls.store(0, std::memory_order_relaxed);
    renderContextSkips.store(0, std::memory_order_relaxed);
    configured.store(true, std::memory_order_release);
    if (!InstallRenderHook()) {
        configured.store(false, std::memory_order_release);
        return false;
    }
    Log("Native VR - Experimental configured for exact build; per-eye replay path armed, headset proof pending");
    return true;
}

extern "C" __declspec(dllexport) bool Fallout3VrBeginFrame(
    IDirect3DDevice9* device, std::uint32_t width, std::uint32_t height, EyeRenderRequest* requests) {
    if (!configured.load(std::memory_order_acquire) || !device || !requests || !width || !height) return false;
    if (!EnsureBridge(device, width, height)) return false;
    EyeRenderRequest eyes[2]{};
    if (!bridge->beginFrame(eyes)) return false;
    requests[0] = eyes[0]; requests[1] = eyes[1];
    return true;
}

extern "C" __declspec(dllexport) bool Fallout3VrSubmitEye(
    IDirect3DDevice9* device, IDirect3DSurface9* surface, std::uint32_t eye,
    std::uint64_t appliedViewProjectionSignature) {
    return bridge && bridge->submitEye(device, surface, eye, appliedViewProjectionSignature);
}

extern "C" __declspec(dllexport) bool Fallout3VrEndFrame() {
    return bridge && bridge->endFrame();
}

extern "C" __declspec(dllexport) void Fallout3VrDeviceLost() {
    if (bridge) bridge->onDeviceLost();
}

extern "C" __declspec(dllexport) void Fallout3VrResetComplete(std::uint32_t width, std::uint32_t height) {
    if (bridge) bridge->onResetComplete(width, height);
}

extern "C" __declspec(dllexport) void Fallout3VrStop() {
    configured.store(false, std::memory_order_release);
    if (hookInstalled.exchange(false, std::memory_order_acq_rel)) {
        auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
        auto* entry = base + activeProfile.renderHookRva;
        MH_DisableHook(entry);
        MH_RemoveHook(entry);
    }
    originalRender = nullptr;
    if (bridge) bridge->stop();
    bridge.reset();
    referencePoseCaptured = false;
    if (minHookInitialized) {
        MH_Uninitialize();
        minHookInitialized = false;
    }
    Log("Native VR - Experimental stopped");
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        selfModule = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

// Minimal FOSE ABI surface. No script opcodes are registered, so this plugin
// cannot conflict with another FOSE plugin's command range.
struct FosePluginInfo {
    std::uint32_t infoVersion;
    const char* name;
    std::uint32_t version;
};

struct FoseInterfacePrefix {
    std::uint32_t foseVersion;
    std::uint32_t runtimeVersion;
    std::uint32_t editorVersion;
    std::uint32_t isEditor;
};

extern "C" __declspec(dllexport) bool FOSEPlugin_Query(
    const FoseInterfacePrefix* fose, FosePluginInfo* info) {
    if (!fose || !info) return false;
    EnsureModulePaths();
    info->infoVersion = 1;
    info->name = "VRClient Fallout 3 Native VR";
    info->version = 2;
    if (fose->isEditor) {
        Log("FOSE query refused: the native VR adapter is runtime-only");
        return false;
    }
    return true;
}

extern "C" __declspec(dllexport) bool FOSEPlugin_Load(const FoseInterfacePrefix*) {
    if (!EnsureModulePaths()) return false;
    // Vortex and other desktop FOSE launches load this DLL too. VR is enabled
    // only for a launch that explicitly opts in, leaving deployed mods alone.
    wchar_t vrOptIn[2]{};
    if (GetEnvironmentVariableW(L"VRCLIENT_FALLOUT3_NATIVE_VR", vrOptIn, 2) != 1 ||
        vrOptIn[0] != L'1') {
        Log("FOSE loaded in desktop mode; native VR hooks are inactive");
        return true;
    }
    Fallout3HookProfile profile{};
    const auto profilePath = moduleDirectory / L"fallout3-native-profile.ini";
    if (!ParseProfileFile(profilePath, profile)) {
        Log("FOSE load refused: fallout3-native-profile.ini is missing or invalid");
        return false;
    }
    return Fallout3VrConfigure(&profile);
}

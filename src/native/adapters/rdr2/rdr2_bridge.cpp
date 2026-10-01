#include "plugins/sdk/rage_services.h"
#include "plugins/sdk/shared_services.h"
#include "public/vr_runtime_api.h"
#include "adapters/redengine/redengine_detour_transaction.h"
#include "adapters/unreal/d3d12_scene_relay_renderer.h"

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <detours.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

// The RDR2 adapter is linked into this bridge DLL.  Keep these declarations at
// global scope so the C-linkage definitions from adapters/rdr2/native_adapter.cpp
// are resolved by the linker (an `extern` declaration inside the anonymous
// namespace would otherwise acquire internal linkage under clang-cl).
extern "C" VrAdapterResult VRCLIENT_ADAPTER_CALL vrclient_create_adapter(
    const VrAdapterHostServices*, VrGameAdapter**);
extern "C" void VRCLIENT_ADAPTER_CALL vrclient_destroy_adapter(VrGameAdapter*);

namespace {

using Microsoft::WRL::ComPtr;
using vrclient::adapters::redengine::DetourChange;
using vrclient::adapters::redengine::DetourOperation;
using vrclient::adapters::redengine::commitDetourTransaction;
using vrclient::adapters::unreal::D3D12SceneRelayRenderer;

using CreateFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
using CreateFactory2Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);
using CreateSwapChainFn = HRESULT(STDMETHODCALLTYPE*)(
    IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
using CreateSwapChainForHwndFn = HRESULT(STDMETHODCALLTYPE*)(
    IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(
    IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

constexpr char kBuildId[] = "steam-1174180-build-13773296";
constexpr char kGameId[] = "red-dead-redemption-2";
constexpr wchar_t kStoryModeLaunchArgument[] = L"-vrclient-rdr2-story";
constexpr std::uint64_t kGetGameplayCamCoord = 0x595320200B98596Eull;
constexpr std::uint64_t kGetGameplayCamRot = 0x0252D2B5582957A6ull;
constexpr std::uint64_t kGetGameplayCamFov = 0xF6A96E5ACEEC6E50ull;
constexpr std::uint64_t kIsGameplayCamRendering = 0x8660EA714834E412ull;
constexpr std::uint64_t kGetGameplayCamRelativeHeading = 0xC4ABF536048998AAull;
constexpr std::uint64_t kGetGameplayCamRelativePitch = 0x99AADEBBA803F827ull;
constexpr std::uint64_t kSetGameplayCamRelativeHeading = 0x5D1EB123EAC5D071ull;
constexpr std::uint64_t kSetGameplayCamRelativePitch = 0xFB760AF4F537B8BFull;

struct Vec3 {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
};

struct CameraState {
  Vec3 position{};
  Vec3 rotation{};
  float fov = 75.0F;
  float base_heading = 0.0F;
  float base_pitch = 0.0F;
  bool baseline_set = false;
  bool ready = false;
  bool gameplay = false;
};

struct InputState {
  std::array<float, VRCLIENT_INPUT_MAX_ACTIONS> values{};
  std::array<VrAdapterBool, VRCLIENT_INPUT_MAX_ACTIONS> active{};
};

struct RenderUserData {
  VrGameAdapter* adapter = nullptr;
  VrAdapterContext* context = nullptr;
};

struct ScriptApi {
  using ScriptRegisterFn = void(__cdecl*)(HMODULE, void(__cdecl*)());
  using ScriptUnregisterFn = void(__cdecl*)(HMODULE);
  using ScriptWaitFn = void(__cdecl*)(DWORD);
  using NativeInitFn = void(__cdecl*)(std::uint64_t);
  using NativePush64Fn = void(__cdecl*)(std::uint64_t);
  using NativeCallFn = std::uint64_t*(__cdecl*)();
  ScriptRegisterFn register_script = nullptr;
  ScriptUnregisterFn unregister_script = nullptr;
  ScriptWaitFn wait = nullptr;
  NativeInitFn native_init = nullptr;
  NativePush64Fn native_push64 = nullptr;
  NativeCallFn native_call = nullptr;
  HMODULE module = nullptr;

  bool ready() const {
    return register_script != nullptr && wait != nullptr && native_init != nullptr &&
        native_push64 != nullptr && native_call != nullptr;
  }
};

std::mutex g_mutex;
ComPtr<ID3D12Device> g_device;
ComPtr<ID3D12CommandQueue> g_queue;
ComPtr<IDXGISwapChain3> g_swapchain;
std::unique_ptr<D3D12SceneRelayRenderer> g_relay;
std::uint32_t g_width = 0;
std::uint32_t g_height = 0;
std::atomic<std::uint64_t> g_present_count{0};
std::atomic<std::uint64_t> g_resize_count{0};
std::atomic<std::uint64_t> g_capture_count{0};
std::atomic<std::uint64_t> g_capture_fail_count{0};
std::atomic<bool> g_stop_requested{false};
std::atomic<bool> g_hooks_started{false};
std::atomic<bool> g_script_registered{false};
std::atomic<bool> g_camera_ready_logged{false};
std::atomic<bool> g_stereo_submit_logged{false};
std::atomic<float> g_head_yaw{0.0F};
std::atomic<float> g_head_pitch{0.0F};
const VrRuntimeFrameData* g_current_frame = nullptr;
const VrRuntimeRenderTarget* g_current_targets = nullptr;
std::uint32_t g_current_target_count = 0;
CameraState g_camera;
InputState g_input;
ScriptApi g_script;
std::thread g_worker;
HMODULE g_module = nullptr;
std::filesystem::path g_log_path;

CreateFactoryFn g_real_create_factory = nullptr;
CreateFactoryFn g_real_create_factory1 = nullptr;
CreateFactory2Fn g_real_create_factory2 = nullptr;
CreateSwapChainFn g_real_create_swapchain = nullptr;
CreateSwapChainForHwndFn g_real_create_swapchain_for_hwnd = nullptr;
PresentFn g_real_present = nullptr;
ResizeBuffersFn g_real_resize_buffers = nullptr;
std::atomic<bool> g_factory_hooks{false};
std::atomic<bool> g_swapchain_hooks{false};
SRWLOCK g_hook_lock = SRWLOCK_INIT;

template <typename Interface, typename Function>
Function vtableFunction(Interface* object, std::size_t index) {
  if (object == nullptr) return nullptr;
  void** table = *reinterpret_cast<void***>(object);
  return reinterpret_cast<Function>(table[index]);
}

void logLine(const char* event_name, const char* detail = nullptr) {
  char line[512]{};
  std::snprintf(line, sizeof(line), "VRClient RDR2 bridge: %s%s%s\n", event_name,
                detail == nullptr ? "" : " ", detail == nullptr ? "" : detail);
  OutputDebugStringA(line);
  std::lock_guard lock(g_mutex);
  if (g_log_path.empty()) return;
  FILE* stream = nullptr;
  if (_wfopen_s(&stream, g_log_path.c_str(), L"ab") != 0 || stream == nullptr) return;
  std::fwrite(line, 1, std::strlen(line), stream);
  std::fclose(stream);
}

void resetLogFile() {
  std::lock_guard lock(g_mutex);
  if (g_log_path.empty()) return;
  FILE* stream = nullptr;
  if (_wfopen_s(&stream, g_log_path.c_str(), L"wb") == 0 && stream != nullptr)
    std::fclose(stream);
}

void observeSwapchain(IUnknown* device_or_queue, IUnknown* swapchain);
void installSwapchainHooks(IDXGISwapChain3* swapchain);
void installFactoryHooks(void* factory_object);
bool bindScriptHook();

bool hasCommandLineArgument(const wchar_t* expected) {
  if (expected == nullptr || *expected == L'\0') return false;
  int argument_count = 0;
  wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
  if (arguments == nullptr) return false;
  bool found = false;
  for (int index = 1; index < argument_count; ++index) {
    if (_wcsicmp(arguments[index], expected) == 0) {
      found = true;
      break;
    }
  }
  LocalFree(arguments);
  return found;
}

HRESULT STDMETHODCALLTYPE hookedCreateSwapChain(
    IDXGIFactory* factory, IUnknown* device, DXGI_SWAP_CHAIN_DESC* description,
    IDXGISwapChain** swapchain) {
  const HRESULT result = g_real_create_swapchain(factory, device, description, swapchain);
  if (SUCCEEDED(result) && swapchain != nullptr && *swapchain != nullptr)
    observeSwapchain(device, *swapchain);
  return result;
}

HRESULT STDMETHODCALLTYPE hookedCreateSwapChainForHwnd(
    IDXGIFactory2* factory, IUnknown* device, HWND window,
    const DXGI_SWAP_CHAIN_DESC1* description,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen, IDXGIOutput* output,
    IDXGISwapChain1** swapchain) {
  const HRESULT result = g_real_create_swapchain_for_hwnd(
      factory, device, window, description, fullscreen, output, swapchain);
  if (SUCCEEDED(result) && swapchain != nullptr && *swapchain != nullptr)
    observeSwapchain(device, *swapchain);
  return result;
}

HRESULT STDMETHODCALLTYPE hookedPresent(
    IDXGISwapChain* swapchain, UINT sync_interval, UINT flags) {
  // Present advances the swap-chain index. Capture the buffer that was just
  // submitted rather than the next buffer returned after Present returns.
  ComPtr<IDXGISwapChain3> current;
  ComPtr<ID3D12Resource> buffer;
  if (swapchain != nullptr &&
      SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&current)))) {
    const auto index = current->GetCurrentBackBufferIndex();
    current->GetBuffer(index, IID_PPV_ARGS(&buffer));
  }
  const HRESULT result = g_real_present(swapchain, sync_interval, flags);
  if (SUCCEEDED(result)) {
    if (current != nullptr && buffer != nullptr) {
      std::lock_guard lock(g_mutex);
      if (g_relay != nullptr && g_swapchain.Get() == current.Get()) {
        const auto capture = g_relay->capture(buffer.Get(), D3D12_RESOURCE_STATE_PRESENT);
        if (capture == VR_RUNTIME_OK) {
          g_capture_count.fetch_add(1, std::memory_order_relaxed);
        } else {
          g_capture_fail_count.fetch_add(1, std::memory_order_relaxed);
        }
      }
    }
    g_present_count.fetch_add(1, std::memory_order_relaxed);
  }
  return result;
}

HRESULT STDMETHODCALLTYPE hookedResizeBuffers(
    IDXGISwapChain* swapchain, UINT buffer_count, UINT width, UINT height,
    DXGI_FORMAT format, UINT flags) {
  const HRESULT result = g_real_resize_buffers(
      swapchain, buffer_count, width, height, format, flags);
  if (SUCCEEDED(result)) {
    std::lock_guard lock(g_mutex);
    if (g_swapchain.Get() == swapchain) {
      g_width = width;
      g_height = height;
      g_capture_count.store(0, std::memory_order_relaxed);
      g_relay.reset();
      if (g_device && g_queue) {
        g_relay = std::make_unique<D3D12SceneRelayRenderer>();
        if (!g_relay->initialize(g_device.Get(), g_queue.Get())) g_relay.reset();
      }
      g_resize_count.fetch_add(1, std::memory_order_relaxed);
    }
  }
  return result;
}

void installSwapchainHooks(IDXGISwapChain3* swapchain) {
  if (swapchain == nullptr || g_swapchain_hooks.load(std::memory_order_acquire)) return;
  AcquireSRWLockExclusive(&g_hook_lock);
  if (!g_swapchain_hooks.load(std::memory_order_relaxed)) {
    g_real_present = vtableFunction<IDXGISwapChain3, PresentFn>(swapchain, 8);
    g_real_resize_buffers = vtableFunction<IDXGISwapChain3, ResizeBuffersFn>(swapchain, 13);
    const std::array<DetourChange, 2> changes{{
        {reinterpret_cast<PVOID*>(&g_real_present), reinterpret_cast<PVOID>(&hookedPresent)},
        {reinterpret_cast<PVOID*>(&g_real_resize_buffers), reinterpret_cast<PVOID>(&hookedResizeBuffers)}}};
    g_swapchain_hooks.store(
        commitDetourTransaction(changes.data(), changes.size(), DetourOperation::Attach),
        std::memory_order_release);
  }
  ReleaseSRWLockExclusive(&g_hook_lock);
}

void observeSwapchain(IUnknown* device_or_queue, IUnknown* swapchain) {
  if (device_or_queue == nullptr || swapchain == nullptr) return;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12Device> device;
  ComPtr<IDXGISwapChain3> swapchain3;
  DXGI_SWAP_CHAIN_DESC1 description{};
  if (FAILED(device_or_queue->QueryInterface(IID_PPV_ARGS(&queue))) ||
      queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT ||
      FAILED(queue->GetDevice(IID_PPV_ARGS(&device))) ||
      FAILED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain3))) ||
      FAILED(swapchain3->GetDesc1(&description)) || description.Width == 0 ||
      description.Height == 0 || description.BufferCount < 2) return;
  ComPtr<ID3D12Device> swapchain_device;
  if (FAILED(swapchain3->GetDevice(IID_PPV_ARGS(&swapchain_device))) ||
      !swapchain_device || !device || swapchain_device.Get() != device.Get()) return;

  auto relay = std::make_unique<D3D12SceneRelayRenderer>();
  if (!relay->initialize(device.Get(), queue.Get())) relay.reset();
  {
    std::lock_guard lock(g_mutex);
    g_device = device;
    g_queue = queue;
    g_swapchain = swapchain3;
    g_width = description.Width;
    g_height = description.Height;
    g_relay = std::move(relay);
    g_capture_count.store(0, std::memory_order_relaxed);
    g_capture_fail_count.store(0, std::memory_order_relaxed);
  }
  installSwapchainHooks(swapchain3.Get());
  logLine("graphics_observed", "d3d12_swapchain_ready");
}

void installFactoryHooks(void* factory_object) {
  if (factory_object == nullptr || g_factory_hooks.load(std::memory_order_acquire)) return;
  ComPtr<IDXGIFactory2> factory;
  if (FAILED(static_cast<IUnknown*>(factory_object)->QueryInterface(IID_PPV_ARGS(&factory)))) return;
  AcquireSRWLockExclusive(&g_hook_lock);
  if (!g_factory_hooks.load(std::memory_order_relaxed)) {
    g_real_create_swapchain = vtableFunction<IDXGIFactory2, CreateSwapChainFn>(factory.Get(), 10);
    g_real_create_swapchain_for_hwnd = vtableFunction<IDXGIFactory2, CreateSwapChainForHwndFn>(factory.Get(), 15);
    const std::array<DetourChange, 2> changes{{
        {reinterpret_cast<PVOID*>(&g_real_create_swapchain), reinterpret_cast<PVOID>(&hookedCreateSwapChain)},
        {reinterpret_cast<PVOID*>(&g_real_create_swapchain_for_hwnd), reinterpret_cast<PVOID>(&hookedCreateSwapChainForHwnd)}}};
    g_factory_hooks.store(
        commitDetourTransaction(changes.data(), changes.size(), DetourOperation::Attach),
        std::memory_order_release);
  }
  ReleaseSRWLockExclusive(&g_hook_lock);
}

HRESULT WINAPI hookedCreateFactory(REFIID iid, void** factory) {
  const HRESULT result = g_real_create_factory(iid, factory);
  if (SUCCEEDED(result) && factory != nullptr && *factory != nullptr) {
    installFactoryHooks(*factory);
  }
  return result;
}

HRESULT WINAPI hookedCreateFactory1(REFIID iid, void** factory) {
  const HRESULT result = g_real_create_factory1(iid, factory);
  if (SUCCEEDED(result) && factory != nullptr && *factory != nullptr)
    installFactoryHooks(*factory);
  return result;
}

HRESULT WINAPI hookedCreateFactory2(UINT flags, REFIID iid, void** factory) {
  const HRESULT result = g_real_create_factory2(flags, iid, factory);
  if (SUCCEEDED(result) && factory != nullptr && *factory != nullptr)
    installFactoryHooks(*factory);
  return result;
}

bool startGraphicsHooks() {
  if (g_hooks_started.load(std::memory_order_acquire)) return true;
  HMODULE module = GetModuleHandleW(L"dxgi.dll");
  if (module == nullptr) module = LoadLibraryW(L"dxgi.dll");
  if (module == nullptr) return false;
  g_real_create_factory = reinterpret_cast<CreateFactoryFn>(GetProcAddress(module, "CreateDXGIFactory"));
  g_real_create_factory1 = reinterpret_cast<CreateFactoryFn>(GetProcAddress(module, "CreateDXGIFactory1"));
  g_real_create_factory2 = reinterpret_cast<CreateFactory2Fn>(GetProcAddress(module, "CreateDXGIFactory2"));
  if (g_real_create_factory == nullptr || g_real_create_factory1 == nullptr || g_real_create_factory2 == nullptr)
    return false;
  const std::array<DetourChange, 3> changes{{
      {reinterpret_cast<PVOID*>(&g_real_create_factory), reinterpret_cast<PVOID>(&hookedCreateFactory)},
      {reinterpret_cast<PVOID*>(&g_real_create_factory1), reinterpret_cast<PVOID>(&hookedCreateFactory1)},
      {reinterpret_cast<PVOID*>(&g_real_create_factory2), reinterpret_cast<PVOID>(&hookedCreateFactory2)}}};
  const bool installed = commitDetourTransaction(changes.data(), changes.size(), DetourOperation::Attach);
  g_hooks_started.store(installed, std::memory_order_release);
  return installed;
}

template <typename T>
std::uint64_t bits(T value) {
  static_assert(sizeof(T) <= sizeof(std::uint64_t));
  std::uint64_t out = 0;
  std::memcpy(&out, &value, sizeof(value));
  return out;
}

template <typename T, typename... Args>
T invokeNative(std::uint64_t hash, Args... args) {
  if (!g_script.ready()) return T{};
  g_script.native_init(hash);
  (g_script.native_push64(bits(args)), ...);
  auto* result = g_script.native_call();
  if (result == nullptr) return T{};
  T value{};
  std::memcpy(&value, result, sizeof(T));
  return value;
}

void updateCameraFromScript() {
  if (!g_script.ready()) return;
  const Vec3 position = invokeNative<Vec3>(kGetGameplayCamCoord);
  const Vec3 rotation = invokeNative<Vec3>(kGetGameplayCamRot, 2);
  const float fov = invokeNative<float>(kGetGameplayCamFov);
  const bool rendering = invokeNative<std::uint64_t>(kIsGameplayCamRendering) != 0;
  const float heading = invokeNative<float>(kGetGameplayCamRelativeHeading);
  const float pitch = invokeNative<float>(kGetGameplayCamRelativePitch);
  bool became_ready = false;
  {
    std::lock_guard lock(g_mutex);
    if (!g_camera.baseline_set) {
      g_camera.base_heading = heading;
      g_camera.base_pitch = pitch;
      g_camera.baseline_set = true;
    }
    g_camera.position = position;
    g_camera.rotation = rotation;
    g_camera.fov = std::isfinite(fov) && fov > 1.0F && fov < 179.0F ? fov : 75.0F;
    g_camera.gameplay = rendering;
    const bool ready = std::isfinite(position.x) && std::isfinite(position.y) &&
        std::isfinite(position.z) && rendering;
    became_ready = ready && !g_camera.ready;
    g_camera.ready = ready;
  }
  if (became_ready && !g_camera_ready_logged.exchange(true, std::memory_order_acq_rel))
    logLine("camera_ready", "native_gameplay_camera_observed");
}

bool keyDown(int virtual_key) {
  return (GetAsyncKeyState(virtual_key) & 0x8000) != 0;
}

float keyAxis(int negative_key, int positive_key) {
  const float negative = keyDown(negative_key) ? -1.0F : 0.0F;
  const float positive = keyDown(positive_key) ? 1.0F : 0.0F;
  return negative + positive;
}

void applyHeadPoseToScript() {
  if (!g_script.ready()) return;
  std::lock_guard lock(g_mutex);
  if (!g_camera.baseline_set || !g_camera.gameplay) return;
  invokeNative<std::uint64_t>(kSetGameplayCamRelativeHeading,
                              g_camera.base_heading + g_head_yaw.load(std::memory_order_relaxed), 0.0F);
  invokeNative<std::uint64_t>(kSetGameplayCamRelativePitch,
                              g_camera.base_pitch + g_head_pitch.load(std::memory_order_relaxed), 0.0F);
}

void __cdecl scriptMain() {
  while (!g_stop_requested.load(std::memory_order_acquire)) {
    updateCameraFromScript();
    applyHeadPoseToScript();
    if (g_script.wait != nullptr) g_script.wait(0);
    else Sleep(16);
  }
}

template <typename Function>
Function findExport(HMODULE module, std::initializer_list<const char*> names) {
  for (const char* name : names) {
    if (auto* symbol = GetProcAddress(module, name)) return reinterpret_cast<Function>(symbol);
  }
  return nullptr;
}

bool bindScriptHook() {
  HMODULE module = GetModuleHandleW(L"ScriptHookRDR2.dll");
  if (module == nullptr) {
    wchar_t executable_path[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable_path,
        static_cast<DWORD>(std::size(executable_path)));
    if (length == 0 || length >= std::size(executable_path)) return false;
    auto dependency = std::filesystem::path(executable_path);
    dependency.replace_filename(L"ScriptHookRDR2.dll");
    module = LoadLibraryExW(dependency.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  }
  if (module == nullptr) return false;
  ScriptApi api;
  api.module = module;
  api.register_script = findExport<ScriptApi::ScriptRegisterFn>(module, {
      "scriptRegister", "?scriptRegister@@YAXPEAUHINSTANCE__@@P6AXXZ@Z"});
  api.unregister_script = findExport<ScriptApi::ScriptUnregisterFn>(module, {
      "scriptUnregister", "?scriptUnregister@@YAXPEAUHINSTANCE__@@@Z"});
  api.wait = findExport<ScriptApi::ScriptWaitFn>(module, {
      "scriptWait", "?scriptWait@@YAXK@Z"});
  api.native_init = findExport<ScriptApi::NativeInitFn>(module, {
      "nativeInit", "?nativeInit@@YAX_K@Z"});
  api.native_push64 = findExport<ScriptApi::NativePush64Fn>(module, {
      "nativePush64", "?nativePush64@@YAX_K@Z"});
  api.native_call = findExport<ScriptApi::NativeCallFn>(module, {
      "nativeCall", "?nativeCall@@YAPEA_KXZ"});
  if (!api.ready()) return false;
  g_script = api;
  g_script.register_script(g_module, &scriptMain);
  g_script_registered.store(true, std::memory_order_release);
  logLine("scripthook_bound", "camera_service_registered");
  return true;
}

void logAdapterEvent(void*, const char* event_name, const char* key, const char* value) {
  char detail[256]{};
  std::snprintf(detail, sizeof(detail), "%s %s=%s", event_name == nullptr ? "event" : event_name,
                key == nullptr ? "key" : key, value == nullptr ? "value" : value);
  logLine("adapter", detail);
}

VrAdapterResult queryGraphics(void*, VrRageGraphicsSnapshot* snapshot) {
  if (snapshot == nullptr || snapshot->size < sizeof(VrRageGraphicsSnapshot))
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  std::lock_guard lock(g_mutex);
  snapshot->version = VRCLIENT_RAGE_GRAPHICS_SERVICE_VERSION;
  snapshot->renderer = VRCLIENT_RAGE_RENDERER_D3D12;
  snapshot->device = g_device.Get();
  snapshot->queue = g_queue.Get();
  snapshot->swapchain = g_swapchain.Get();
  snapshot->width = g_width;
  snapshot->height = g_height;
  snapshot->present_count = g_present_count.load(std::memory_order_relaxed);
  snapshot->resize_count = g_resize_count.load(std::memory_order_relaxed);
  snapshot->ready = g_device && g_queue && g_swapchain && g_relay &&
      g_capture_count.load(std::memory_order_relaxed) > 0;
  return snapshot->ready ? VR_ADAPTER_OK : VR_ADAPTER_ERROR_RECOVERABLE;
}

VrAdapterResult submitStereo(void*, const VrRageGraphicsSnapshot*,
                             const float*, const float*) {
  VrRuntimeResult result = VR_RUNTIME_ERROR_STATE;
  {
    std::lock_guard lock(g_mutex);
    if (g_relay == nullptr || g_current_frame == nullptr ||
        g_current_targets == nullptr || g_current_target_count != 2)
      return VR_ADAPTER_ERROR_RECOVERABLE;
    result = g_relay->render(
        *g_current_frame, g_current_targets, g_current_target_count);
  }
  if (result == VR_RUNTIME_OK &&
      !g_stereo_submit_logged.exchange(true, std::memory_order_acq_rel))
    logLine("stereo_submit", "openxr_eye_targets_submitted");
  return result == VR_RUNTIME_OK ? VR_ADAPTER_OK : VR_ADAPTER_ERROR_RECOVERABLE;
}

VrAdapterResult queryView(void*, VrRageViewSnapshot* snapshot) {
  if (snapshot == nullptr || snapshot->size < sizeof(VrRageViewSnapshot))
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  std::lock_guard lock(g_mutex);
  snapshot->version = VRCLIENT_RAGE_VIEW_SERVICE_VERSION;
  snapshot->position_m[0] = g_camera.position.x;
  snapshot->position_m[1] = g_camera.position.y;
  snapshot->position_m[2] = g_camera.position.z;
  snapshot->orientation_xyzw[0] = 0.0F;
  snapshot->orientation_xyzw[1] = 0.0F;
  snapshot->orientation_xyzw[2] = 0.0F;
  snapshot->orientation_xyzw[3] = 1.0F;
  snapshot->fov_angle_left = -0.7F;
  snapshot->fov_angle_right = 0.7F;
  snapshot->fov_angle_up = 0.55F;
  snapshot->fov_angle_down = -0.55F;
  snapshot->frame_index = g_present_count.load(std::memory_order_relaxed);
  snapshot->camera_id = 1;
  snapshot->ready = g_camera.ready;
  return snapshot->ready ? VR_ADAPTER_OK : VR_ADAPTER_ERROR_RECOVERABLE;
}

VrAdapterResult queryGameState(void*, VrRageGameStateSnapshot* snapshot) {
  if (snapshot == nullptr || snapshot->size < sizeof(VrRageGameStateSnapshot))
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  std::lock_guard lock(g_mutex);
  snapshot->version = VRCLIENT_RAGE_GAME_STATE_SERVICE_VERSION;
  snapshot->mode = g_camera.gameplay ? VRCLIENT_RAGE_GAME_MODE_GAMEPLAY : VRCLIENT_RAGE_GAME_MODE_LOADING;
  snapshot->player_available = g_camera.gameplay;
  snapshot->camera_control_allowed = g_camera.gameplay;
  snapshot->ready = g_camera.ready;
  return snapshot->ready ? VR_ADAPTER_OK : VR_ADAPTER_ERROR_RECOVERABLE;
}

VrAdapterResult applyRageInput(void*, VrRageControlId control, float value, VrAdapterBool active) {
  if (control < VRCLIENT_RAGE_CONTROL_LOOK_X ||
      control > VRCLIENT_RAGE_CONTROL_HORSE_REIN)
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  std::lock_guard lock(g_mutex);
  g_input.values[control] = value;
  g_input.active[control] = active;
  return VR_ADAPTER_OK;
}

VrAdapterResult querySharedInput(void*, std::uint32_t action_id,
                                 VrClientInputActionState* state) {
  if (state == nullptr || state->size < sizeof(VrClientInputActionState) ||
      action_id >= VRCLIENT_INPUT_MAX_ACTIONS)
    return VR_ADAPTER_ERROR_INVALID_ARGUMENT;
  state->action_id = action_id;
  state->active = 0;
  state->value = 0.0F;
  switch (action_id) {
    case VRCLIENT_INPUT_ACTION_MOVE_X:
      state->value = keyAxis('A', 'D');
      break;
    case VRCLIENT_INPUT_ACTION_MOVE_Y:
      state->value = keyAxis('S', 'W');
      break;
    case VRCLIENT_INPUT_ACTION_INTERACT:
      state->active = keyDown('E');
      break;
    case VRCLIENT_INPUT_ACTION_MENU:
      state->active = keyDown(VK_ESCAPE);
      break;
    case VRCLIENT_INPUT_ACTION_RECENTER:
      state->active = keyDown('R');
      break;
    case VRCLIENT_INPUT_ACTION_COMFORT_SNAP_TURN:
      state->active = keyDown('T');
      break;
    case VRCLIENT_INPUT_ACTION_COMFORT_VIGNETTE_TOGGLE:
      state->active = keyDown('V');
      break;
    case VRCLIENT_INPUT_ACTION_AIM:
      state->active = keyDown(VK_RBUTTON);
      break;
    case VRCLIENT_INPUT_ACTION_FIRE:
      state->active = keyDown(VK_LBUTTON);
      break;
    case VRCLIENT_INPUT_ACTION_HORSE_FORWARD:
      state->value = keyDown('W') ? 1.0F : 0.0F;
      break;
    case VRCLIENT_INPUT_ACTION_HORSE_REIN:
      state->value = keyAxis('A', 'D');
      break;
    default:
      break;
  }
  state->active = state->active || std::fabs(state->value) > 0.001F;
  state->timestamp_ns = static_cast<std::int64_t>(GetTickCount64()) * 1'000'000;
  return VR_ADAPTER_OK;
}

VrRuntimeResult renderAdapterFrame(void* user_data, const VrRuntimeFrameData* frame,
                                   const VrRuntimeRenderTarget* targets,
                                   std::uint32_t target_count) {
  auto* render = static_cast<RenderUserData*>(user_data);
  if (render == nullptr || render->adapter == nullptr || render->context == nullptr ||
      frame == nullptr || targets == nullptr || target_count != 2)
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  const auto yaw = std::atan2(
      2.0F * (frame->head_pose.orientation.w * frame->head_pose.orientation.y +
              frame->head_pose.orientation.x * frame->head_pose.orientation.z),
      1.0F - 2.0F * (frame->head_pose.orientation.y * frame->head_pose.orientation.y +
                      frame->head_pose.orientation.x * frame->head_pose.orientation.x));
  const auto pitch = std::asin((std::max)(-1.0F, (std::min)(1.0F,
      2.0F * (frame->head_pose.orientation.w * frame->head_pose.orientation.x -
              frame->head_pose.orientation.z * frame->head_pose.orientation.y))));
  g_head_yaw.store(yaw * 57.295779513F, std::memory_order_relaxed);
  g_head_pitch.store(pitch * 57.295779513F, std::memory_order_relaxed);
  VrAdapterFrameInfo adapter_frame{};
  adapter_frame.size = sizeof(adapter_frame);
  adapter_frame.frame_index = frame->timing.frame_index;
  adapter_frame.predicted_display_time_ns = frame->timing.predicted_display_time_ns;
  adapter_frame.predicted_display_period_seconds = frame->timing.predicted_display_period_seconds;
  adapter_frame.delta_seconds = frame->timing.predicted_display_period_seconds;
  adapter_frame.callback_flags = VR_ADAPTER_CALLBACK_HOT_PATH;
  g_current_frame = frame;
  g_current_targets = targets;
  g_current_target_count = target_count;
  const auto adapter_result = render->adapter->tick(
      render->adapter, render->context, &adapter_frame);
  g_current_frame = nullptr;
  g_current_targets = nullptr;
  g_current_target_count = 0;
  if (adapter_result == VR_ADAPTER_OK) return VR_RUNTIME_OK;
  // Loading/menu transitions are expected while the OpenXR session is already
  // alive. Keep polling until RAGE reports a gameplay camera instead of
  // tearing down the session on a transient adapter result.
  if (adapter_result == VR_ADAPTER_ERROR_RECOVERABLE) return VR_RUNTIME_SKIPPED;
  return VR_RUNTIME_ERROR_STATE;
}

void runOpenXrBridge() {
  while (!g_stop_requested.load(std::memory_order_acquire) &&
         !g_script_registered.load(std::memory_order_acquire)) {
    if (!g_script.ready()) bindScriptHook();
    if (!g_script_registered.load(std::memory_order_acquire)) Sleep(100);
  }
  while (!g_stop_requested.load(std::memory_order_acquire) &&
         g_capture_count.load(std::memory_order_acquire) == 0) Sleep(20);
  if (g_stop_requested.load(std::memory_order_acquire)) return;

  VrRuntimeD3D12Binding binding{};
  {
    std::lock_guard lock(g_mutex);
    binding.size = sizeof(binding);
    binding.device = g_device.Get();
    binding.queue = g_queue.Get();
  }
  if (binding.device == nullptr || binding.queue == nullptr || !g_script_registered.load()) {
    logLine("bridge_blocked", "scripthook_or_graphics_missing");
    return;
  }

  auto profile = g_log_path;
  profile.replace_filename(L"runtime-profile.json");
  VrRuntimeDesc runtime_desc{};
  runtime_desc.size = sizeof(runtime_desc);
  runtime_desc.application_name = "VRClient RDR2 Story Mode";
  std::string profile_string = profile.string();
  runtime_desc.runtime_profile_path = profile_string.c_str();
  runtime_desc.preferred_graphics_backend = VR_RUNTIME_GRAPHICS_BACKEND_D3D12;
  runtime_desc.d3d12_binding = &binding;
  VrRuntime* runtime = nullptr;
  auto result = vr_runtime_create(&runtime_desc, &runtime);
  if (result == VR_RUNTIME_OK) result = vr_runtime_start(runtime);
  if (result != VR_RUNTIME_OK) {
    logLine("openxr_blocked", vr_runtime_result_name(result));
    if (runtime != nullptr) vr_runtime_destroy(runtime);
    return;
  }

  // The adapter is compiled into the bridge DLL so no untrusted DLL search is
  // needed inside the game process.
  VrRageGraphicsService graphics{sizeof(graphics), VRCLIENT_RAGE_GRAPHICS_SERVICE_VERSION,
                                 &queryGraphics, &submitStereo, nullptr};
  VrRageViewService view{sizeof(view), VRCLIENT_RAGE_VIEW_SERVICE_VERSION, &queryView, nullptr};
  VrRageGameStateService state{sizeof(state), VRCLIENT_RAGE_GAME_STATE_SERVICE_VERSION,
                               &queryGameState, nullptr};
  VrRageInputService input{sizeof(input), VRCLIENT_RAGE_INPUT_SERVICE_VERSION,
                           &applyRageInput, nullptr};
  VrClientInputService shared_input{sizeof(shared_input), VRCLIENT_SHARED_SERVICE_VERSION,
                                   &querySharedInput, nullptr, VRCLIENT_INPUT_MAX_ACTIONS};
  VrAdapterDiagnosticsService diagnostics{sizeof(diagnostics), 1, &logAdapterEvent, nullptr};
  const std::array<VrAdapterService, 6> services{{
      {sizeof(VrAdapterService), VRCLIENT_ADAPTER_SERVICE_RAGE_GRAPHICS,
       VRCLIENT_RAGE_GRAPHICS_SERVICE_VERSION, &graphics},
      {sizeof(VrAdapterService), VRCLIENT_ADAPTER_SERVICE_RAGE_VIEW,
       VRCLIENT_RAGE_VIEW_SERVICE_VERSION, &view},
      {sizeof(VrAdapterService), VRCLIENT_ADAPTER_SERVICE_RAGE_GAME_STATE,
       VRCLIENT_RAGE_GAME_STATE_SERVICE_VERSION, &state},
      {sizeof(VrAdapterService), VRCLIENT_ADAPTER_SERVICE_RAGE_INPUT,
       VRCLIENT_RAGE_INPUT_SERVICE_VERSION, &input},
      {sizeof(VrAdapterService), VRCLIENT_ADAPTER_SERVICE_INPUT,
       VRCLIENT_SHARED_SERVICE_VERSION, &shared_input},
      {sizeof(VrAdapterService), VRCLIENT_ADAPTER_SERVICE_DIAGNOSTICS, 1, &diagnostics}}};
  VrAdapterHostServices host{sizeof(host), {sizeof(VrAdapterApiVersion), 1, 0, 0},
                             services.data(), static_cast<std::uint32_t>(services.size())};
  VrGameAdapter* adapter = nullptr;
  if (vrclient_create_adapter(&host, &adapter) != VR_ADAPTER_OK || adapter == nullptr) {
    logLine("adapter_blocked", "create_failed");
    vr_runtime_stop(runtime);
    vr_runtime_destroy(runtime);
    return;
  }
  const char* target_source = "steam";
  VrAdapterContext context{sizeof(context), &host,
                           {sizeof(VrAdapterTargetIdentity), kGameId, kBuildId,
                            nullptr, target_source}, nullptr};
  if (adapter->validate(adapter, &context) != VR_ADAPTER_OK ||
      adapter->init(adapter, &context) != VR_ADAPTER_OK) {
    logLine("adapter_blocked", "validation_or_init_failed");
    vrclient_destroy_adapter(adapter);
    vr_runtime_stop(runtime);
    vr_runtime_destroy(runtime);
    return;
  }

  RenderUserData render_user_data{adapter, &context};
  while (!g_stop_requested.load(std::memory_order_acquire)) {
    result = vr_runtime_run_frame(runtime, &renderAdapterFrame, &render_user_data);
    if (result != VR_RUNTIME_OK && result != VR_RUNTIME_SKIPPED) break;
    const auto runtime_state = vr_runtime_get_state(runtime);
    if (runtime_state == VR_RUNTIME_STATE_EXITING || runtime_state == VR_RUNTIME_STATE_LOSS_PENDING ||
        runtime_state == VR_RUNTIME_STATE_ERROR || runtime_state == VR_RUNTIME_STATE_STOPPED) break;
    if (result == VR_RUNTIME_SKIPPED) Sleep(10);
  }
  adapter->shutdown(adapter, &context);
  vrclient_destroy_adapter(adapter);
  vr_runtime_stop(runtime);
  vr_runtime_destroy(runtime);
  logLine("bridge_stopped", "openxr_session_closed");
}

void initializeBridge() {
  wchar_t module_path[32768]{};
  const DWORD length = GetModuleFileNameW(g_module, module_path, static_cast<DWORD>(std::size(module_path)));
  if (length != 0 && length < std::size(module_path)) {
    g_log_path = std::filesystem::path(module_path);
    const std::wstring log_name = L"rdr2-bridge-" +
        std::to_wstring(GetCurrentProcessId()) + L".log";
    g_log_path.replace_filename(log_name);
  }
  resetLogFile();
  if (!hasCommandLineArgument(kStoryModeLaunchArgument)) {
    logLine("bridge_blocked", "story_mode_marker_required");
    return;
  }
  if (!startGraphicsHooks()) {
    logLine("bridge_blocked", "d3d12_hooks_unavailable");
    return;
  }
  if (!bindScriptHook()) {
    logLine("bridge_blocked", "ScriptHookRDR2_load_or_exports_failed");
    return;
  }
  g_worker = std::thread(&runOpenXrBridge);
  g_worker.detach();
}

}  // namespace

DWORD WINAPI initializeBridgeThread(LPVOID) {
  initializeBridge();
  return 0;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_module = module;
    DisableThreadLibraryCalls(module);
    g_stop_requested.store(false, std::memory_order_release);
    // Do not construct std::thread under the loader lock.  ScriptHook loads
    // ASI plugins from inside DllMain; starting a plain Win32 worker lets the
    // loader return before CRT, Detours, DXGI, or OpenXR work begins.
    HANDLE setup = CreateThread(nullptr, 0, &initializeBridgeThread, nullptr, 0, nullptr);
    if (setup != nullptr) CloseHandle(setup);
  } else if (reason == DLL_PROCESS_DETACH) {
    g_stop_requested.store(true, std::memory_order_release);
    // Detach runs under the loader lock.  Do not call ScriptHook, Detours, or
    // C++ synchronization primitives here; process teardown discards the
    // hooks and the worker observes g_stop_requested on its next boundary.
  }
  return TRUE;
}

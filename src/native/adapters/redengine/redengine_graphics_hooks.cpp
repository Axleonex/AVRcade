#include "adapters/redengine/redengine_graphics_hooks.h"

#include "adapters/redengine/redengine_detour_transaction.h"
#include "adapters/redengine/redengine_graphics_observer.h"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>

namespace vrclient::adapters::redengine {
namespace {

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
using CreateCommittedResourceFn = HRESULT(STDMETHODCALLTYPE*)(
    ID3D12Device*, const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS,
    const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES,
    const D3D12_CLEAR_VALUE*, REFIID, void**);
using CreatePlacedResourceFn = HRESULT(STDMETHODCALLTYPE*)(
    ID3D12Device*, ID3D12Heap*, UINT64, const D3D12_RESOURCE_DESC*,
    D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);
using CreateRenderTargetViewFn = void(STDMETHODCALLTYPE*)(
    ID3D12Device*, ID3D12Resource*, const D3D12_RENDER_TARGET_VIEW_DESC*,
    D3D12_CPU_DESCRIPTOR_HANDLE);
using OMSetRenderTargetsFn = void(STDMETHODCALLTYPE*)(
    ID3D12GraphicsCommandList*, UINT,
    const D3D12_CPU_DESCRIPTOR_HANDLE*, BOOL,
    const D3D12_CPU_DESCRIPTOR_HANDLE*);
using ClearRenderTargetViewFn = void(STDMETHODCALLTYPE*)(
    ID3D12GraphicsCommandList*, D3D12_CPU_DESCRIPTOR_HANDLE,
    const FLOAT[4], UINT, const D3D12_RECT*);
using ResourceBarrierFn = void(STDMETHODCALLTYPE*)(
    ID3D12GraphicsCommandList*, UINT, const D3D12_RESOURCE_BARRIER*);
using CopyTextureRegionFn = void(STDMETHODCALLTYPE*)(
    ID3D12GraphicsCommandList*, const D3D12_TEXTURE_COPY_LOCATION*, UINT, UINT,
    UINT, const D3D12_TEXTURE_COPY_LOCATION*, const D3D12_BOX*);
using CopyResourceFn = void(STDMETHODCALLTYPE*)(
    ID3D12GraphicsCommandList*, ID3D12Resource*, ID3D12Resource*);
using ResolveSubresourceFn = void(STDMETHODCALLTYPE*)(
    ID3D12GraphicsCommandList*, ID3D12Resource*, UINT, ID3D12Resource*, UINT,
    DXGI_FORMAT);
using ExecuteCommandListsFn = void(STDMETHODCALLTYPE*)(
    ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

GraphicsObserver observer;
thread_local unsigned observer_callback_depth = 0;
SRWLOCK hook_lock = SRWLOCK_INIT;
std::atomic<bool> export_hooks_installed{false};
std::atomic<bool> factory_hooks_installed{false};
std::atomic<bool> swapchain_hooks_installed{false};
std::atomic<bool> device_hooks_installed{false};
std::atomic<bool> queue_hooks_installed{false};

CreateFactoryFn real_create_factory = nullptr;
CreateFactoryFn real_create_factory1 = nullptr;
CreateFactory2Fn real_create_factory2 = nullptr;
CreateSwapChainFn real_create_swapchain = nullptr;
CreateSwapChainForHwndFn real_create_swapchain_for_hwnd = nullptr;
PresentFn real_present = nullptr;
ResizeBuffersFn real_resize_buffers = nullptr;
CreateCommittedResourceFn real_create_committed_resource = nullptr;
CreatePlacedResourceFn real_create_placed_resource = nullptr;
CreateRenderTargetViewFn real_create_render_target_view = nullptr;
OMSetRenderTargetsFn real_om_set_render_targets = nullptr;
ClearRenderTargetViewFn real_clear_render_target_view = nullptr;
ResourceBarrierFn real_resource_barrier = nullptr;
CopyTextureRegionFn real_copy_texture_region = nullptr;
CopyResourceFn real_copy_resource = nullptr;
ResolveSubresourceFn real_resolve_subresource = nullptr;
ExecuteCommandListsFn real_execute_command_lists = nullptr;

template <typename Interface, typename Function>
Function vtableFunction(Interface* object, std::size_t index) {
  if (object == nullptr) {
    return nullptr;
  }
  void** table = *reinterpret_cast<void***>(object);
  return reinterpret_cast<Function>(table[index]);
}

bool changeHooks(
    const DetourChange* changes,
    std::size_t count,
    DetourOperation operation) {
  return commitDetourTransaction(changes, count, operation);
}

void installSwapchainHooks(IDXGISwapChain3* swapchain);
void installDeviceHooks(ID3D12Device* device);
void installQueueHooks(ID3D12CommandQueue* queue);

void observeCreatedSwapchain(IUnknown* device_or_queue, IUnknown* swapchain) {
  if (!observer.observe(device_or_queue, swapchain)) {
    return;
  }
  VrRedengineGraphicsSnapshot snapshot{};
  snapshot.size = sizeof(snapshot);
  observer.query(&snapshot);
  installDeviceHooks(static_cast<ID3D12Device*>(snapshot.device));
  installQueueHooks(static_cast<ID3D12CommandQueue*>(snapshot.queue));
  Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain3;
  if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain3)))) {
    installSwapchainHooks(swapchain3.Get());
  }
}

HRESULT STDMETHODCALLTYPE hookedCreateCommittedResource(
    ID3D12Device* device,
    const D3D12_HEAP_PROPERTIES* heap_properties,
    D3D12_HEAP_FLAGS heap_flags,
    const D3D12_RESOURCE_DESC* description,
    D3D12_RESOURCE_STATES initial_state,
    const D3D12_CLEAR_VALUE* optimized_clear_value,
    REFIID iid,
    void** resource) {
  const HRESULT result = real_create_committed_resource(
      device,
      heap_properties,
      heap_flags,
      description,
      initial_state,
      optimized_clear_value,
      iid,
      resource);
  if (SUCCEEDED(result) && resource != nullptr) {
    observer.recordResource(description, static_cast<IUnknown*>(*resource));
  }
  return result;
}

HRESULT STDMETHODCALLTYPE hookedCreatePlacedResource(
    ID3D12Device* device,
    ID3D12Heap* heap,
    UINT64 heap_offset,
    const D3D12_RESOURCE_DESC* description,
    D3D12_RESOURCE_STATES initial_state,
    const D3D12_CLEAR_VALUE* optimized_clear_value,
    REFIID iid,
    void** resource) {
  const HRESULT result = real_create_placed_resource(
      device,
      heap,
      heap_offset,
      description,
      initial_state,
      optimized_clear_value,
      iid,
      resource);
  if (SUCCEEDED(result) && resource != nullptr) {
    observer.recordResource(description, static_cast<IUnknown*>(*resource));
  }
  return result;
}

void STDMETHODCALLTYPE hookedCreateRenderTargetView(
    ID3D12Device* device,
    ID3D12Resource* resource,
    const D3D12_RENDER_TARGET_VIEW_DESC* description,
    D3D12_CPU_DESCRIPTOR_HANDLE destination) {
  real_create_render_target_view(device, resource, description, destination);
  observer.recordRenderTargetView(resource, destination);
}

void STDMETHODCALLTYPE hookedOMSetRenderTargets(
    ID3D12GraphicsCommandList* command_list,
    UINT render_target_descriptor_count,
    const D3D12_CPU_DESCRIPTOR_HANDLE* render_target_descriptors,
    BOOL single_range,
    const D3D12_CPU_DESCRIPTOR_HANDLE* depth_stencil_descriptor) {
  real_om_set_render_targets(
      command_list,
      render_target_descriptor_count,
      render_target_descriptors,
      single_range,
      depth_stencil_descriptor);
  observer.recordRenderTargetBindings(
      render_target_descriptor_count,
      render_target_descriptors,
      single_range);
}

void STDMETHODCALLTYPE hookedClearRenderTargetView(
    ID3D12GraphicsCommandList* command_list,
    D3D12_CPU_DESCRIPTOR_HANDLE render_target_view,
    const FLOAT color[4],
    UINT rectangle_count,
    const D3D12_RECT* rectangles) {
  real_clear_render_target_view(
      command_list,
      render_target_view,
      color,
      rectangle_count,
      rectangles);
  observer.recordRenderTargetClear(render_target_view);
}

void STDMETHODCALLTYPE hookedResourceBarrier(
    ID3D12GraphicsCommandList* command_list,
    UINT barrier_count,
    const D3D12_RESOURCE_BARRIER* barriers) {
  real_resource_barrier(command_list, barrier_count, barriers);
  if (observer_callback_depth == 0) {
    ++observer_callback_depth;
    observer.recordResourceBarriers(command_list, barrier_count, barriers);
    --observer_callback_depth;
  }
}

void STDMETHODCALLTYPE hookedExecuteCommandLists(
    ID3D12CommandQueue* queue,
    UINT count,
    ID3D12CommandList* const* command_lists) {
  real_execute_command_lists(queue, count, command_lists);
  observer.recordExecuteCommandLists(queue, count, command_lists);
}

void installQueueHooks(ID3D12CommandQueue* queue) {
  if (queue == nullptr ||
      queue_hooks_installed.load(std::memory_order_acquire)) {
    return;
  }
  AcquireSRWLockExclusive(&hook_lock);
  if (!queue_hooks_installed.load(std::memory_order_relaxed)) {
    real_execute_command_lists =
        vtableFunction<ID3D12CommandQueue, ExecuteCommandListsFn>(queue, 10);
    const DetourChange change{
        reinterpret_cast<PVOID*>(&real_execute_command_lists),
        reinterpret_cast<PVOID>(&hookedExecuteCommandLists)};
    queue_hooks_installed.store(
        changeHooks(&change, 1, DetourOperation::Attach),
        std::memory_order_release);
  }
  ReleaseSRWLockExclusive(&hook_lock);
}

void STDMETHODCALLTYPE hookedCopyTextureRegion(
    ID3D12GraphicsCommandList* command_list,
    const D3D12_TEXTURE_COPY_LOCATION* destination,
    UINT destination_x,
    UINT destination_y,
    UINT destination_z,
    const D3D12_TEXTURE_COPY_LOCATION* source,
    const D3D12_BOX* source_box) {
  real_copy_texture_region(
      command_list,
      destination,
      destination_x,
      destination_y,
      destination_z,
      source,
      source_box);
  if (observer_callback_depth == 0 && source != nullptr) {
    observer.recordCopySource(source->pResource);
  }
}

void STDMETHODCALLTYPE hookedCopyResource(
    ID3D12GraphicsCommandList* command_list,
    ID3D12Resource* destination,
    ID3D12Resource* source) {
  real_copy_resource(command_list, destination, source);
  if (observer_callback_depth == 0) {
    observer.recordCopySource(source);
  }
}

void STDMETHODCALLTYPE hookedResolveSubresource(
    ID3D12GraphicsCommandList* command_list,
    ID3D12Resource* destination,
    UINT destination_subresource,
    ID3D12Resource* source,
    UINT source_subresource,
    DXGI_FORMAT format) {
  real_resolve_subresource(
      command_list,
      destination,
      destination_subresource,
      source,
      source_subresource,
      format);
  if (observer_callback_depth == 0) {
    observer.recordCopySource(source);
  }
}

void installDeviceHooks(ID3D12Device* device) {
  if (device == nullptr ||
      device_hooks_installed.load(std::memory_order_acquire)) {
    return;
  }
  AcquireSRWLockExclusive(&hook_lock);
  if (!device_hooks_installed.load(std::memory_order_relaxed)) {
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list;
    if (FAILED(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&command_allocator))) ||
        FAILED(device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator.Get(),
            nullptr,
            IID_PPV_ARGS(&command_list)))) {
      ReleaseSRWLockExclusive(&hook_lock);
      return;
    }
    command_list->Close();
    real_create_committed_resource =
        vtableFunction<ID3D12Device, CreateCommittedResourceFn>(device, 27);
    real_create_placed_resource =
        vtableFunction<ID3D12Device, CreatePlacedResourceFn>(device, 29);
    real_create_render_target_view =
        vtableFunction<ID3D12Device, CreateRenderTargetViewFn>(device, 20);
    real_om_set_render_targets =
        vtableFunction<ID3D12GraphicsCommandList, OMSetRenderTargetsFn>(
            command_list.Get(), 46);
    real_clear_render_target_view =
        vtableFunction<ID3D12GraphicsCommandList, ClearRenderTargetViewFn>(
            command_list.Get(), 48);
    real_resource_barrier =
        vtableFunction<ID3D12GraphicsCommandList, ResourceBarrierFn>(
            command_list.Get(), 26);
    real_copy_texture_region =
        vtableFunction<ID3D12GraphicsCommandList, CopyTextureRegionFn>(
            command_list.Get(), 16);
    real_copy_resource =
        vtableFunction<ID3D12GraphicsCommandList, CopyResourceFn>(
            command_list.Get(), 17);
    real_resolve_subresource =
        vtableFunction<ID3D12GraphicsCommandList, ResolveSubresourceFn>(
            command_list.Get(), 19);
    const std::array<DetourChange, 9> changes{{
        {reinterpret_cast<PVOID*>(&real_create_committed_resource),
         reinterpret_cast<PVOID>(&hookedCreateCommittedResource)},
        {reinterpret_cast<PVOID*>(&real_create_placed_resource),
         reinterpret_cast<PVOID>(&hookedCreatePlacedResource)},
        {reinterpret_cast<PVOID*>(&real_create_render_target_view),
         reinterpret_cast<PVOID>(&hookedCreateRenderTargetView)},
        {reinterpret_cast<PVOID*>(&real_om_set_render_targets),
         reinterpret_cast<PVOID>(&hookedOMSetRenderTargets)},
        {reinterpret_cast<PVOID*>(&real_clear_render_target_view),
         reinterpret_cast<PVOID>(&hookedClearRenderTargetView)},
        {reinterpret_cast<PVOID*>(&real_resource_barrier),
         reinterpret_cast<PVOID>(&hookedResourceBarrier)},
        {reinterpret_cast<PVOID*>(&real_copy_texture_region),
         reinterpret_cast<PVOID>(&hookedCopyTextureRegion)},
        {reinterpret_cast<PVOID*>(&real_copy_resource),
         reinterpret_cast<PVOID>(&hookedCopyResource)},
        {reinterpret_cast<PVOID*>(&real_resolve_subresource),
         reinterpret_cast<PVOID>(&hookedResolveSubresource)},
    }};
    device_hooks_installed.store(
        changeHooks(changes.data(), changes.size(), DetourOperation::Attach),
        std::memory_order_release);
  }
  ReleaseSRWLockExclusive(&hook_lock);
}

HRESULT STDMETHODCALLTYPE hookedCreateSwapChain(
    IDXGIFactory* factory,
    IUnknown* device,
    DXGI_SWAP_CHAIN_DESC* description,
    IDXGISwapChain** swapchain) {
  const HRESULT result =
      real_create_swapchain(factory, device, description, swapchain);
  if (SUCCEEDED(result) && swapchain != nullptr) {
    observeCreatedSwapchain(device, *swapchain);
  }
  return result;
}

HRESULT STDMETHODCALLTYPE hookedCreateSwapChainForHwnd(
    IDXGIFactory2* factory,
    IUnknown* device,
    HWND window,
    const DXGI_SWAP_CHAIN_DESC1* description,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen,
    IDXGIOutput* output,
    IDXGISwapChain1** swapchain) {
  const HRESULT result = real_create_swapchain_for_hwnd(
      factory, device, window, description, fullscreen, output, swapchain);
  if (SUCCEEDED(result) && swapchain != nullptr) {
    observeCreatedSwapchain(device, *swapchain);
  }
  return result;
}

HRESULT STDMETHODCALLTYPE hookedPresent(
    IDXGISwapChain* swapchain,
    UINT sync_interval,
    UINT flags) {
  const HRESULT result = real_present(swapchain, sync_interval, flags);
  if (SUCCEEDED(result)) {
    observer.recordPresent(swapchain);
  }
  return result;
}

HRESULT STDMETHODCALLTYPE hookedResizeBuffers(
    IDXGISwapChain* swapchain,
    UINT buffer_count,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    UINT flags) {
  const HRESULT result = real_resize_buffers(
      swapchain, buffer_count, width, height, format, flags);
  if (SUCCEEDED(result)) {
    observer.recordResize(swapchain);
  }
  return result;
}

void installSwapchainHooks(IDXGISwapChain3* swapchain) {
  if (swapchain == nullptr ||
      swapchain_hooks_installed.load(std::memory_order_acquire)) {
    return;
  }
  AcquireSRWLockExclusive(&hook_lock);
  if (!swapchain_hooks_installed.load(std::memory_order_relaxed)) {
    real_present = vtableFunction<IDXGISwapChain3, PresentFn>(swapchain, 8);
    real_resize_buffers =
        vtableFunction<IDXGISwapChain3, ResizeBuffersFn>(swapchain, 13);
    const std::array<DetourChange, 2> changes{{
        {reinterpret_cast<PVOID*>(&real_present),
         reinterpret_cast<PVOID>(&hookedPresent)},
        {reinterpret_cast<PVOID*>(&real_resize_buffers),
         reinterpret_cast<PVOID>(&hookedResizeBuffers)},
    }};
    swapchain_hooks_installed.store(
        changeHooks(changes.data(), changes.size(), DetourOperation::Attach),
        std::memory_order_release);
  }
  ReleaseSRWLockExclusive(&hook_lock);
}

void installFactoryHooks(void* factory_object) {
  if (factory_object == nullptr ||
      factory_hooks_installed.load(std::memory_order_acquire)) {
    return;
  }
  IUnknown* unknown = static_cast<IUnknown*>(factory_object);
  Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
  if (FAILED(unknown->QueryInterface(IID_PPV_ARGS(&factory)))) {
    return;
  }

  AcquireSRWLockExclusive(&hook_lock);
  if (!factory_hooks_installed.load(std::memory_order_relaxed)) {
    real_create_swapchain =
        vtableFunction<IDXGIFactory2, CreateSwapChainFn>(factory.Get(), 10);
    real_create_swapchain_for_hwnd =
        vtableFunction<IDXGIFactory2, CreateSwapChainForHwndFn>(factory.Get(), 15);
    const std::array<DetourChange, 2> changes{{
        {reinterpret_cast<PVOID*>(&real_create_swapchain),
         reinterpret_cast<PVOID>(&hookedCreateSwapChain)},
        {reinterpret_cast<PVOID*>(&real_create_swapchain_for_hwnd),
         reinterpret_cast<PVOID>(&hookedCreateSwapChainForHwnd)},
    }};
    factory_hooks_installed.store(
        changeHooks(changes.data(), changes.size(), DetourOperation::Attach),
        std::memory_order_release);
  }
  ReleaseSRWLockExclusive(&hook_lock);
}

HRESULT WINAPI hookedCreateFactory(REFIID iid, void** factory) {
  const HRESULT result = real_create_factory(iid, factory);
  if (SUCCEEDED(result) && factory != nullptr) {
    installFactoryHooks(*factory);
  }
  return result;
}

HRESULT WINAPI hookedCreateFactory1(REFIID iid, void** factory) {
  const HRESULT result = real_create_factory1(iid, factory);
  if (SUCCEEDED(result) && factory != nullptr) {
    installFactoryHooks(*factory);
  }
  return result;
}

HRESULT WINAPI hookedCreateFactory2(UINT flags, REFIID iid, void** factory) {
  const HRESULT result = real_create_factory2(flags, iid, factory);
  if (SUCCEEDED(result) && factory != nullptr) {
    installFactoryHooks(*factory);
  }
  return result;
}

bool detachSwapchainHooks() {
  if (!swapchain_hooks_installed.load(std::memory_order_acquire)) {
    return true;
  }
  const std::array<DetourChange, 2> changes{{
      {reinterpret_cast<PVOID*>(&real_present),
       reinterpret_cast<PVOID>(&hookedPresent)},
      {reinterpret_cast<PVOID*>(&real_resize_buffers),
       reinterpret_cast<PVOID>(&hookedResizeBuffers)},
  }};
  const bool result =
      changeHooks(changes.data(), changes.size(), DetourOperation::Detach);
  if (result) {
    swapchain_hooks_installed.store(false, std::memory_order_release);
  }
  return result;
}

bool detachQueueHooks() {
  if (!queue_hooks_installed.load(std::memory_order_acquire)) {
    return true;
  }
  const DetourChange change{
      reinterpret_cast<PVOID*>(&real_execute_command_lists),
      reinterpret_cast<PVOID>(&hookedExecuteCommandLists)};
  const bool result =
      changeHooks(&change, 1, DetourOperation::Detach);
  if (result) {
    queue_hooks_installed.store(false, std::memory_order_release);
  }
  return result;
}

bool detachDeviceHooks() {
  if (!device_hooks_installed.load(std::memory_order_acquire)) {
    return true;
  }
  const std::array<DetourChange, 9> changes{{
      {reinterpret_cast<PVOID*>(&real_create_committed_resource),
       reinterpret_cast<PVOID>(&hookedCreateCommittedResource)},
      {reinterpret_cast<PVOID*>(&real_create_placed_resource),
       reinterpret_cast<PVOID>(&hookedCreatePlacedResource)},
      {reinterpret_cast<PVOID*>(&real_create_render_target_view),
       reinterpret_cast<PVOID>(&hookedCreateRenderTargetView)},
      {reinterpret_cast<PVOID*>(&real_om_set_render_targets),
       reinterpret_cast<PVOID>(&hookedOMSetRenderTargets)},
      {reinterpret_cast<PVOID*>(&real_clear_render_target_view),
       reinterpret_cast<PVOID>(&hookedClearRenderTargetView)},
      {reinterpret_cast<PVOID*>(&real_resource_barrier),
       reinterpret_cast<PVOID>(&hookedResourceBarrier)},
      {reinterpret_cast<PVOID*>(&real_copy_texture_region),
       reinterpret_cast<PVOID>(&hookedCopyTextureRegion)},
      {reinterpret_cast<PVOID*>(&real_copy_resource),
       reinterpret_cast<PVOID>(&hookedCopyResource)},
      {reinterpret_cast<PVOID*>(&real_resolve_subresource),
       reinterpret_cast<PVOID>(&hookedResolveSubresource)},
  }};
  const bool result =
      changeHooks(changes.data(), changes.size(), DetourOperation::Detach);
  if (result) {
    device_hooks_installed.store(false, std::memory_order_release);
  }
  return result;
}

bool detachFactoryHooks() {
  if (!factory_hooks_installed.load(std::memory_order_acquire)) {
    return true;
  }
  const std::array<DetourChange, 2> changes{{
      {reinterpret_cast<PVOID*>(&real_create_swapchain),
       reinterpret_cast<PVOID>(&hookedCreateSwapChain)},
      {reinterpret_cast<PVOID*>(&real_create_swapchain_for_hwnd),
       reinterpret_cast<PVOID>(&hookedCreateSwapChainForHwnd)},
  }};
  const bool result =
      changeHooks(changes.data(), changes.size(), DetourOperation::Detach);
  if (result) {
    factory_hooks_installed.store(false, std::memory_order_release);
  }
  return result;
}

bool detachExportHooks() {
  if (!export_hooks_installed.load(std::memory_order_acquire)) {
    return true;
  }
  const std::array<DetourChange, 3> changes{{
      {reinterpret_cast<PVOID*>(&real_create_factory),
       reinterpret_cast<PVOID>(&hookedCreateFactory)},
      {reinterpret_cast<PVOID*>(&real_create_factory1),
       reinterpret_cast<PVOID>(&hookedCreateFactory1)},
      {reinterpret_cast<PVOID*>(&real_create_factory2),
       reinterpret_cast<PVOID>(&hookedCreateFactory2)},
  }};
  const bool result =
      changeHooks(changes.data(), changes.size(), DetourOperation::Detach);
  if (result) {
    export_hooks_installed.store(false, std::memory_order_release);
  }
  return result;
}

}  // namespace

GraphicsHookResult startGraphicsObservation() {
  if (export_hooks_installed.load(std::memory_order_acquire)) {
    return GraphicsHookResult::AlreadyInstalled;
  }

  // NVIDIA Streamline can own the factory exports when frame generation is
  // active. Prefer that interposer when it is already present; otherwise hook
  // the system DXGI exports. No proxy DLL is installed by VRClient.
  HMODULE module = GetModuleHandleW(L"sl.interposer.dll");
  if (module == nullptr) {
    module = LoadLibraryW(L"dxgi.dll");
  }
  if (module == nullptr) {
    return GraphicsHookResult::ModuleUnavailable;
  }

  real_create_factory = reinterpret_cast<CreateFactoryFn>(
      GetProcAddress(module, "CreateDXGIFactory"));
  real_create_factory1 = reinterpret_cast<CreateFactoryFn>(
      GetProcAddress(module, "CreateDXGIFactory1"));
  real_create_factory2 = reinterpret_cast<CreateFactory2Fn>(
      GetProcAddress(module, "CreateDXGIFactory2"));
  if (real_create_factory == nullptr || real_create_factory1 == nullptr ||
      real_create_factory2 == nullptr) {
    return GraphicsHookResult::ExportUnavailable;
  }

  const std::array<DetourChange, 3> changes{{
      {reinterpret_cast<PVOID*>(&real_create_factory),
       reinterpret_cast<PVOID>(&hookedCreateFactory)},
      {reinterpret_cast<PVOID*>(&real_create_factory1),
       reinterpret_cast<PVOID>(&hookedCreateFactory1)},
      {reinterpret_cast<PVOID*>(&real_create_factory2),
       reinterpret_cast<PVOID>(&hookedCreateFactory2)},
  }};
  if (!changeHooks(changes.data(), changes.size(), DetourOperation::Attach)) {
    return GraphicsHookResult::HookFailed;
  }
  export_hooks_installed.store(true, std::memory_order_release);
  return GraphicsHookResult::Installed;
}

bool stopGraphicsObservation() {
  AcquireSRWLockExclusive(&hook_lock);
  const bool queue_ok = detachQueueHooks();
  const bool device_ok = detachDeviceHooks();
  const bool swapchain_ok = detachSwapchainHooks();
  const bool factory_ok = detachFactoryHooks();
  const bool exports_ok = detachExportHooks();
  if (queue_ok && device_ok && swapchain_ok && factory_ok && exports_ok) {
    observer.reset();
  }
  ReleaseSRWLockExclusive(&hook_lock);
  return queue_ok && device_ok && swapchain_ok && factory_ok && exports_ok;
}

void requestVrcamCapture() {
  observer.requestVrcamCapture();
}

bool makeD3D12Binding(VrRuntimeD3D12Binding* binding) {
  return observer.makeD3D12Binding(binding);
}

VrRuntimeResult renderCapturedVrcamFrame(
    const VrRuntimeFrameData& frame,
    const VrRuntimeRenderTarget* targets,
    std::uint32_t target_count) {
  return observer.renderCapturedVrcamFrame(frame, targets, target_count);
}

VrRuntimeResult readbackCapturedVrcam(
    std::vector<std::uint8_t>* pixels,
    std::uint32_t* width,
    std::uint32_t* height) {
  return observer.readbackCapturedVrcam(pixels, width, height);
}

void queryGraphicsObservation(VrRedengineGraphicsSnapshot* snapshot) {
  observer.query(snapshot);
}

}  // namespace vrclient::adapters::redengine

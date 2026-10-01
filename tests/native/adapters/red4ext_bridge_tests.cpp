#include <RED4ext/Common.hpp>
#include <RED4ext/Api/ApiVersion.hpp>
#include <RED4ext/Api/v1/EMainReason.hpp>
#include <RED4ext/Api/v1/PluginHandle.hpp>
#include <RED4ext/Api/v1/PluginInfo.hpp>
#include <RED4ext/Api/v1/Sdk.hpp>

#include "plugins/sdk/redengine_services.h"
#include "adapters/redengine/redengine_vrcam_selector.h"
#include "adapters/unreal/d3d12_render_util.h"
#include "adapters/unreal/d3d12_scene_relay_renderer.h"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <Windows.h>
#include <wrl/client.h>

#include <iostream>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using SupportsFn = uint32_t(RED4EXT_CALL*)();
using QueryFn = void(RED4EXT_CALL*)(RED4ext::v1::PluginInfo*);
using MainFn = bool(RED4EXT_CALL*)(
    RED4ext::v1::PluginHandle,
    RED4ext::v1::EMainReason,
    const RED4ext::v1::Sdk*);
using QueryGraphicsFn = void(RED4EXT_CALL*)(VrRedengineGraphicsSnapshot*);
using RequestVrcamCaptureFn = void(RED4EXT_CALL*)();

struct LogCounts {
  static inline int info = 0;
  static inline int error = 0;
};

void RED4EXT_CALL infoLog(RED4ext::v1::PluginHandle, const char*) {
  ++LogCounts::info;
}

void RED4EXT_CALL errorLog(RED4ext::v1::PluginHandle, const char*) {
  ++LogCounts::error;
}

void expect(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void expectRelayColor(
    ID3D12Device* device,
    ID3D12CommandQueue* queue,
    ID3D12Resource* target,
    const std::array<uint8_t, 4>& expected) {
  using Microsoft::WRL::ComPtr;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT rows = 0;
  UINT64 row_bytes = 0;
  UINT64 total_bytes = 0;
  const D3D12_RESOURCE_DESC target_description = target->GetDesc();
  device->GetCopyableFootprints(
      &target_description,
      0,
      1,
      0,
      &footprint,
      &rows,
      &row_bytes,
      &total_bytes);
  D3D12_HEAP_PROPERTIES readback_heap{};
  readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC readback_description{};
  readback_description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  readback_description.Width = total_bytes;
  readback_description.Height = 1;
  readback_description.DepthOrArraySize = 1;
  readback_description.MipLevels = 1;
  readback_description.SampleDesc.Count = 1;
  readback_description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> readback;
  expect(SUCCEEDED(device->CreateCommittedResource(
             &readback_heap,
             D3D12_HEAP_FLAG_NONE,
             &readback_description,
             D3D12_RESOURCE_STATE_COPY_DEST,
             nullptr,
             IID_PPV_ARGS(&readback))),
         "failed to create relay readback buffer");
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  expect(SUCCEEDED(device->CreateCommandAllocator(
             D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) &&
             SUCCEEDED(device->CreateCommandList(
                 0,
                 D3D12_COMMAND_LIST_TYPE_DIRECT,
                 allocator.Get(),
                 nullptr,
                 IID_PPV_ARGS(&list))),
         "failed to create relay readback command list");
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = target;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  list->ResourceBarrier(1, &barrier);
  D3D12_TEXTURE_COPY_LOCATION destination{};
  destination.pResource = readback.Get();
  destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  destination.PlacedFootprint = footprint;
  D3D12_TEXTURE_COPY_LOCATION source{};
  source.pResource = target;
  source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  source.SubresourceIndex = 0;
  list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
  expect(SUCCEEDED(list->Close()), "failed to close relay readback list");
  ID3D12CommandList* lists[]{list.Get()};
  queue->ExecuteCommandLists(1, lists);
  ComPtr<ID3D12Fence> fence;
  expect(SUCCEEDED(device->CreateFence(
             0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) &&
             SUCCEEDED(queue->Signal(fence.Get(), 1)),
         "failed to signal relay readback fence");
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  expect(event != nullptr && vrclient::adapters::unreal::waitForFence(
                                 fence.Get(), 1, event, 10'000),
         "relay readback fence timed out");
  CloseHandle(event);
  uint8_t* pixels = nullptr;
  const D3D12_RANGE read_range{0, static_cast<SIZE_T>(total_bytes)};
  expect(SUCCEEDED(readback->Map(
             0, &read_range, reinterpret_cast<void**>(&pixels))),
         "failed to map relay readback buffer");
  for (std::size_t channel = 0; channel < expected.size(); ++channel) {
    expect(
        std::abs(static_cast<int>(pixels[footprint.Offset + channel]) -
                 static_cast<int>(expected[channel])) <= 2,
        "relay eye target pixel did not match the VRCAM source");
  }
  const D3D12_RANGE no_write{0, 0};
  readback->Unmap(0, &no_write);
}

LRESULT CALLBACK testWindowProcedure(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam) {
  return DefWindowProcW(window, message, wparam, lparam);
}

HWND createTestWindow() {
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSW window_class{};
  window_class.lpfnWndProc = &testWindowProcedure;
  window_class.hInstance = instance;
  window_class.lpszClassName = L"VRClientRedengineObserverTest";
  RegisterClassW(&window_class);
  return CreateWindowExW(
      0,
      window_class.lpszClassName,
      L"VRClient REDengine observer test",
      WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      320,
      200,
      nullptr,
      nullptr,
      instance,
      nullptr);
}

void exerciseD3D12Observation(
    QueryGraphicsFn query_graphics,
    RequestVrcamCaptureFn request_vrcam_capture) {
  using Microsoft::WRL::ComPtr;
  HWND window = createTestWindow();
  expect(window != nullptr, "failed to create observer test window");

  ComPtr<IDXGIFactory4> factory;
  expect(SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))),
         "failed to create observed DXGI factory");
  ComPtr<IDXGIAdapter> warp_adapter;
  expect(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp_adapter))),
         "failed to enumerate WARP adapter");
  ComPtr<ID3D12Device> device;
  expect(SUCCEEDED(D3D12CreateDevice(
             warp_adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))),
         "failed to create WARP D3D12 device");

  D3D12_COMMAND_QUEUE_DESC queue_description{};
  queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  ComPtr<ID3D12CommandQueue> queue;
  expect(SUCCEEDED(device->CreateCommandQueue(
             &queue_description, IID_PPV_ARGS(&queue))),
         "failed to create direct D3D12 queue");

  DXGI_SWAP_CHAIN_DESC1 swapchain_description{};
  swapchain_description.Width = 320;
  swapchain_description.Height = 200;
  swapchain_description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapchain_description.SampleDesc.Count = 1;
  swapchain_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapchain_description.BufferCount = 2;
  swapchain_description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  ComPtr<IDXGISwapChain1> swapchain1;
  expect(SUCCEEDED(factory->CreateSwapChainForHwnd(
             queue.Get(),
             window,
             &swapchain_description,
             nullptr,
             nullptr,
             &swapchain1)),
         "pass-through CreateSwapChainForHwnd failed");
  ComPtr<IDXGISwapChain3> swapchain;
  expect(SUCCEEDED(swapchain1.As(&swapchain)),
         "created swapchain does not expose IDXGISwapChain3");
  expect(SUCCEEDED(swapchain->Present(0, 0)),
         "pass-through Present failed");
  expect(SUCCEEDED(swapchain->ResizeBuffers(
             2, 400, 240, DXGI_FORMAT_R8G8B8A8_UNORM, 0)),
         "pass-through ResizeBuffers failed");

  VrRedengineGraphicsSnapshot snapshot{};
  snapshot.size = sizeof(snapshot);
  query_graphics(&snapshot);
  expect(snapshot.version == VRCLIENT_REDENGINE_GRAPHICS_SERVICE_VERSION,
         "unexpected graphics observation version");
  expect(snapshot.renderer == VRCLIENT_REDENGINE_RENDERER_D3D12 &&
             snapshot.ready != 0,
         "D3D12 observation did not become ready");
  expect(snapshot.device != nullptr && snapshot.queue != nullptr &&
             snapshot.swapchain != nullptr,
         "D3D12 observation omitted a borrowed graphics object");
  expect(snapshot.width == 400 && snapshot.height == 240 &&
             snapshot.buffer_count == 2,
         "ResizeBuffers observation did not refresh dimensions");
  expect(snapshot.present_count == 1 && snapshot.resize_count == 1,
         "Present/Resize counters are not transparent and exact");

  D3D12_HEAP_PROPERTIES heap_properties{};
  heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC unrelated_description{};
  unrelated_description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  unrelated_description.Width = 512;
  unrelated_description.Height = 512;
  unrelated_description.DepthOrArraySize = 1;
  unrelated_description.MipLevels = 1;
  unrelated_description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  unrelated_description.SampleDesc.Count = 1;
  unrelated_description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  unrelated_description.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  ComPtr<ID3D12Resource> unrelated_resource;
  expect(SUCCEEDED(device->CreateCommittedResource(
             &heap_properties,
             D3D12_HEAP_FLAG_NONE,
             &unrelated_description,
             D3D12_RESOURCE_STATE_RENDER_TARGET,
             nullptr,
             IID_PPV_ARGS(&unrelated_resource))),
         "pass-through unrelated resource creation failed");

  D3D12_RESOURCE_DESC unorm_eye_description = unrelated_description;
  unorm_eye_description.Width = 1080;
  unorm_eye_description.Height = 1200;
  ComPtr<ID3D12Resource> unorm_eye_resource;
  expect(SUCCEEDED(device->CreateCommittedResource(
             &heap_properties,
             D3D12_HEAP_FLAG_NONE,
             &unorm_eye_description,
             D3D12_RESOURCE_STATE_RENDER_TARGET,
             nullptr,
             IID_PPV_ARGS(&unorm_eye_resource))),
         "pass-through UNORM VRCAM diagnostic resource creation failed");

  D3D12_RESOURCE_DESC eye_description = unorm_eye_description;
  eye_description.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
  ComPtr<ID3D12Resource> eye_resource;
  expect(SUCCEEDED(device->CreateCommittedResource(
             &heap_properties,
             D3D12_HEAP_FLAG_NONE,
             &eye_description,
             D3D12_RESOURCE_STATE_RENDER_TARGET,
             nullptr,
             IID_PPV_ARGS(&eye_resource))),
         "pass-through candidate VRCAM resource creation failed");

  snapshot = {};
  snapshot.size = sizeof(snapshot);
  query_graphics(&snapshot);
  expect(snapshot.candidate_view_width == 1080 &&
             snapshot.candidate_view_height == 1200,
         "graphics snapshot omitted the configured candidate view dimensions");
  expect(snapshot.candidate_view_resource_count == 2,
         "observer must count the candidate eye resource and ignore unrelated textures");
  expect(snapshot.candidate_unorm_resource_count == 1,
         "observer must report target-sized UNORM allocations without selecting them");
  expect(snapshot.candidate_color_resource == eye_resource.Get() &&
             snapshot.candidate_color_format ==
                 static_cast<uint32_t>(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) &&
             snapshot.candidate_color_resource_count == 1,
         "observer must expose the first target-sized sRGB VRCAM color resource");

  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_description{};
  rtv_heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_heap_description.NumDescriptors = 3;
  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  expect(SUCCEEDED(device->CreateDescriptorHeap(
             &rtv_heap_description, IID_PPV_ARGS(&rtv_heap))),
         "failed to create RTV heap for VRCAM identity proof");
  const UINT rtv_stride = device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  D3D12_CPU_DESCRIPTOR_HANDLE rtv =
      rtv_heap->GetCPUDescriptorHandleForHeapStart();
  device->CreateRenderTargetView(unrelated_resource.Get(), nullptr, rtv);
  rtv.ptr += rtv_stride;
  device->CreateRenderTargetView(eye_resource.Get(), nullptr, rtv);
  rtv.ptr += rtv_stride;
  const D3D12_CPU_DESCRIPTOR_HANDLE unorm_rtv = rtv;
  expect(SUCCEEDED(unorm_eye_resource->SetName(L"VRClientTestUNORM")),
         "failed to name UNORM candidate for identity proof");
  device->CreateRenderTargetView(unorm_eye_resource.Get(), nullptr, rtv);

  D3D12_RESOURCE_DESC copy_destination_description = unorm_eye_description;
  copy_destination_description.Flags = D3D12_RESOURCE_FLAG_NONE;
  ComPtr<ID3D12Resource> copy_destination;
  expect(SUCCEEDED(device->CreateCommittedResource(
             &heap_properties,
             D3D12_HEAP_FLAG_NONE,
             &copy_destination_description,
             D3D12_RESOURCE_STATE_COPY_DEST,
             nullptr,
             IID_PPV_ARGS(&copy_destination))),
         "failed to create untracked copy destination for VRCAM identity proof");

  ComPtr<ID3D12CommandAllocator> command_allocator;
  expect(SUCCEEDED(device->CreateCommandAllocator(
             D3D12_COMMAND_LIST_TYPE_DIRECT,
             IID_PPV_ARGS(&command_allocator))),
         "failed to create command allocator for VRCAM bind proof");
  ComPtr<ID3D12GraphicsCommandList> command_list;
  expect(SUCCEEDED(device->CreateCommandList(
             0,
             D3D12_COMMAND_LIST_TYPE_DIRECT,
             command_allocator.Get(),
             nullptr,
             IID_PPV_ARGS(&command_list))),
         "failed to create command list for VRCAM bind proof");
  command_list->OMSetRenderTargets(1, &unorm_rtv, FALSE, nullptr);
  const FLOAT clear_color[4]{0.2F, 0.4F, 0.6F, 1.0F};
  command_list->ClearRenderTargetView(
      unorm_rtv, clear_color, 0, nullptr);
  D3D12_RESOURCE_BARRIER shader_read_barrier{};
  shader_read_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  shader_read_barrier.Transition.pResource = unorm_eye_resource.Get();
  shader_read_barrier.Transition.Subresource =
      D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  shader_read_barrier.Transition.StateBefore =
      D3D12_RESOURCE_STATE_RENDER_TARGET;
  shader_read_barrier.Transition.StateAfter =
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  command_list->ResourceBarrier(1, &shader_read_barrier);
  D3D12_RESOURCE_BARRIER copy_source_barrier = shader_read_barrier;
  copy_source_barrier.Transition.StateBefore =
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  copy_source_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  command_list->ResourceBarrier(1, &copy_source_barrier);
  command_list->CopyResource(
      copy_destination.Get(), unorm_eye_resource.Get());
  expect(SUCCEEDED(command_list->Close()),
         "failed to close command list for VRCAM bind proof");
  snapshot = {};
  snapshot.size = sizeof(snapshot);
  query_graphics(&snapshot);
  expect(snapshot.candidate_color_rtv_count == 1,
         "observer must correlate RTV binding only to the exposed VRCAM color resource");
  expect(snapshot.candidate_unorm_rtv_resource_count == 1 &&
             snapshot.candidate_unorm_rtv_count == 1,
         "observer must correlate distinct and total RTV definitions for UNORM candidates");
  expect(snapshot.candidate_unorm_bound_resource_count == 1 &&
             snapshot.candidate_unorm_bind_count == 1 &&
             snapshot.candidate_unorm_tracked_count == 1 &&
             snapshot.candidate_unorm_bind_counts[0] == 1,
         "observer must report per-resource OMSetRenderTargets usage for UNORM candidates");
  expect(snapshot.candidate_unorm_cleared_resource_count == 1 &&
             snapshot.candidate_unorm_clear_count == 1 &&
             snapshot.candidate_unorm_clear_counts[0] == 1,
         "observer must report per-resource clears for UNORM candidates");
  expect(snapshot.candidate_unorm_shader_read_resource_count == 1 &&
             snapshot.candidate_unorm_shader_read_transition_count == 1 &&
             snapshot.candidate_unorm_shader_read_transition_counts[0] == 1,
         "observer must report per-resource transitions from render target to shader read");
  expect(std::string(snapshot.candidate_unorm_names[0]) ==
             "VRClientTestUNORM",
         "observer must preserve available debug names for UNORM candidates");
  expect(snapshot.candidate_unorm_copy_source_resource_count == 1 &&
             snapshot.candidate_unorm_copy_source_count == 1 &&
             snapshot.candidate_unorm_copy_source_counts[0] == 1,
         "observer must report per-resource copy-source usage for UNORM candidates");
  expect(snapshot.vrcam_ready == 1 &&
             snapshot.vrcam_color_resource == unorm_eye_resource.Get() &&
             snapshot.vrcam_color_format ==
                 static_cast<uint32_t>(DXGI_FORMAT_R8G8B8A8_UNORM) &&
             snapshot.vrcam_candidate_index == 0 &&
             snapshot.vrcam_bind_count == 1 &&
             snapshot.vrcam_shader_read_transition_count == 1,
         "dominant render lifecycle must expose a stable VRCAM color resource");

  ID3D12CommandList* source_lists[]{command_list.Get()};
  queue->ExecuteCommandLists(1, source_lists);
  expect(SUCCEEDED(swapchain->Present(0, 0)),
         "pass-through Present for one-shot VRCAM capture failed");
  snapshot = {};
  snapshot.size = sizeof(snapshot);
  query_graphics(&snapshot);
  expect(snapshot.vrcam_capture_ready == 1 &&
             snapshot.vrcam_capture_count == 1 &&
             snapshot.vrcam_capture_fail_count == 0 &&
             snapshot.vrcam_source_state == D3D12_RESOURCE_STATE_COPY_SOURCE,
         "observer must capture the selected VRCAM resource once and restore its exact state");
  expect(snapshot.candidate_unorm_primary_queue_execute_counts[0] >= 1 &&
             snapshot.candidate_unorm_other_direct_queue_execute_counts[0] == 0 &&
             snapshot.candidate_unorm_compute_queue_execute_counts[0] == 0 &&
             snapshot.candidate_unorm_copy_queue_execute_counts[0] == 0,
         "observer must attribute a VRCAM-producing command list to its execution queue");
  request_vrcam_capture();
  ComPtr<ID3D12CommandAllocator> refresh_allocator;
  ComPtr<ID3D12GraphicsCommandList> refresh_list;
  expect(SUCCEEDED(device->CreateCommandAllocator(
             D3D12_COMMAND_LIST_TYPE_DIRECT,
             IID_PPV_ARGS(&refresh_allocator))) &&
             SUCCEEDED(device->CreateCommandList(
                 0,
                 D3D12_COMMAND_LIST_TYPE_DIRECT,
                 refresh_allocator.Get(),
                 nullptr,
                 IID_PPV_ARGS(&refresh_list))),
         "failed to create refreshed VRCAM producer list");
  D3D12_RESOURCE_BARRIER refresh_to_render{};
  refresh_to_render.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  refresh_to_render.Transition.pResource = unorm_eye_resource.Get();
  refresh_to_render.Transition.Subresource =
      D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  refresh_to_render.Transition.StateBefore =
      D3D12_RESOURCE_STATE_COPY_SOURCE;
  refresh_to_render.Transition.StateAfter =
      D3D12_RESOURCE_STATE_RENDER_TARGET;
  refresh_list->ResourceBarrier(1, &refresh_to_render);
  refresh_list->ClearRenderTargetView(unorm_rtv, clear_color, 0, nullptr);
  refresh_list->ResourceBarrier(1, &shader_read_barrier);
  refresh_list->ResourceBarrier(1, &copy_source_barrier);
  expect(SUCCEEDED(refresh_list->Close()),
         "failed to close refreshed VRCAM producer list");
  ID3D12CommandList* refresh_lists[]{refresh_list.Get()};
  queue->ExecuteCommandLists(1, refresh_lists);
  expect(SUCCEEDED(swapchain->Present(0, 0)),
         "pass-through Present after refreshed VRCAM capture failed");
  snapshot = {};
  snapshot.size = sizeof(snapshot);
  query_graphics(&snapshot);
  expect(snapshot.vrcam_capture_count == 2 &&
             snapshot.vrcam_capture_fail_count == 0,
         "OpenXR must be able to request a fresh capture at a later Present boundary");
  vrclient::adapters::unreal::D3D12SceneRelayRenderer relay;
  expect(relay.initialize(device.Get(), queue.Get()),
         "failed to initialize the VRCAM eye relay");
  D3D12_RESOURCE_DESC transient_description = unorm_eye_description;
  transient_description.Width = 8;
  transient_description.Height = 8;
  ComPtr<ID3D12Resource> transient_source;
  expect(SUCCEEDED(device->CreateCommittedResource(
             &heap_properties,
             D3D12_HEAP_FLAG_NONE,
             &transient_description,
             D3D12_RESOURCE_STATE_RENDER_TARGET,
             nullptr,
             IID_PPV_ARGS(&transient_source))),
         "failed to create transient inline-capture source");
  D3D12_DESCRIPTOR_HEAP_DESC transient_rtv_heap_description{};
  transient_rtv_heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  transient_rtv_heap_description.NumDescriptors = 1;
  ComPtr<ID3D12DescriptorHeap> transient_rtv_heap;
  expect(SUCCEEDED(device->CreateDescriptorHeap(
             &transient_rtv_heap_description,
             IID_PPV_ARGS(&transient_rtv_heap))),
         "failed to create transient inline-capture RTV heap");
  const auto transient_rtv =
      transient_rtv_heap->GetCPUDescriptorHandleForHeapStart();
  device->CreateRenderTargetView(
      transient_source.Get(), nullptr, transient_rtv);
  ComPtr<ID3D12CommandAllocator> transient_allocator;
  ComPtr<ID3D12GraphicsCommandList> transient_list;
  expect(SUCCEEDED(device->CreateCommandAllocator(
             D3D12_COMMAND_LIST_TYPE_DIRECT,
             IID_PPV_ARGS(&transient_allocator))) &&
             SUCCEEDED(device->CreateCommandList(
                 0,
                 D3D12_COMMAND_LIST_TYPE_DIRECT,
                 transient_allocator.Get(),
                 nullptr,
                 IID_PPV_ARGS(&transient_list))),
         "failed to create transient inline-capture command list");
  const FLOAT inline_color[4]{0.2F, 0.4F, 0.6F, 1.0F};
  transient_list->ClearRenderTargetView(
      transient_rtv, inline_color, 0, nullptr);
  D3D12_RESOURCE_BARRIER transient_to_read = shader_read_barrier;
  transient_to_read.Transition.pResource = transient_source.Get();
  transient_list->ResourceBarrier(1, &transient_to_read);
  expect(relay.recordInlineCapture(
             transient_list.Get(),
             transient_source.Get(),
             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) == VR_RUNTIME_OK,
         "failed to record capture at the exact producer transition");
  D3D12_RESOURCE_BARRIER transient_to_render = transient_to_read;
  transient_to_render.Transition.StateBefore =
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  transient_to_render.Transition.StateAfter =
      D3D12_RESOURCE_STATE_RENDER_TARGET;
  transient_list->ResourceBarrier(1, &transient_to_render);
  const FLOAT overwritten_color[4]{1.0F, 0.0F, 1.0F, 1.0F};
  transient_list->ClearRenderTargetView(
      transient_rtv, overwritten_color, 0, nullptr);
  transient_list->ResourceBarrier(1, &transient_to_read);
  expect(SUCCEEDED(transient_list->Close()),
         "failed to close transient inline-capture command list");
  ID3D12CommandList* transient_lists[]{transient_list.Get()};
  queue->ExecuteCommandLists(1, transient_lists);
  expect(relay.publishInlineCapture(transient_list.Get()),
         "executed inline capture was not published");
  std::vector<std::uint8_t> inline_pixels;
  std::uint32_t inline_width = 0;
  std::uint32_t inline_height = 0;
  expect(relay.readbackCapturedRgba8(
             &inline_pixels, &inline_width, &inline_height) == VR_RUNTIME_OK &&
             inline_width == 8 && inline_height == 8 &&
             std::array<std::uint8_t, 4>{
                 inline_pixels[0], inline_pixels[1],
                 inline_pixels[2], inline_pixels[3]} ==
                 std::array<std::uint8_t, 4>{51, 102, 153, 255},
         "inline capture must preserve pixels before transient source reuse");
  expect(relay.capture(
             unorm_eye_resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE) ==
             VR_RUNTIME_OK,
         "failed to capture the selected VRCAM source");
  const std::array<uint8_t, 4> expected_color{51, 102, 153, 255};
  std::vector<std::uint8_t> captured_pixels;
  std::uint32_t captured_width = 0;
  std::uint32_t captured_height = 0;
  expect(relay.readbackCapturedRgba8(
             &captured_pixels, &captured_width, &captured_height) ==
             VR_RUNTIME_OK &&
             captured_width == 1080 && captured_height == 1200 &&
             captured_pixels.size() ==
                 static_cast<std::size_t>(captured_width) * captured_height * 4 &&
             std::array<std::uint8_t, 4>{
                 captured_pixels[0], captured_pixels[1],
                 captured_pixels[2], captured_pixels[3]} == expected_color,
         "diagnostic readback must preserve the captured VRCAM RGBA pixels");
  D3D12_RESOURCE_DESC eye_target_description = unorm_eye_description;
  eye_target_description.Width = 8;
  eye_target_description.Height = 8;
  ComPtr<ID3D12Resource> eye_targets[2];
  for (auto& target : eye_targets) {
    expect(SUCCEEDED(device->CreateCommittedResource(
               &heap_properties,
               D3D12_HEAP_FLAG_NONE,
               &eye_target_description,
               D3D12_RESOURCE_STATE_RENDER_TARGET,
               nullptr,
               IID_PPV_ARGS(&target))),
           "failed to create controlled OpenXR eye target");
  }
  VrRuntimeFrameData relay_frame{};
  relay_frame.timing.predicted_display_time_ns = 1;
  relay_frame.eye_count = 2;
  relay_frame.eyes[0].eye = VR_RUNTIME_EYE_LEFT;
  relay_frame.eyes[1].eye = VR_RUNTIME_EYE_RIGHT;
  VrRuntimeRenderTarget relay_targets[2]{};
  for (std::size_t eye = 0; eye < 2; ++eye) {
    relay_targets[eye].backend = VR_RUNTIME_GRAPHICS_BACKEND_D3D12;
    relay_targets[eye].eye = static_cast<VrRuntimeEye>(eye);
    relay_targets[eye].color_texture = eye_targets[eye].Get();
    relay_targets[eye].width = 8;
    relay_targets[eye].height = 8;
    relay_targets[eye].viewport = {0, 0, 8, 8};
    relay_targets[eye].color_format = DXGI_FORMAT_R8G8B8A8_UNORM;
  }
  expect(relay.render(relay_frame, relay_targets, 2) == VR_RUNTIME_OK,
         "failed to render the captured VRCAM source into both eye targets");
  expectRelayColor(
      device.Get(), queue.Get(), eye_targets[0].Get(), expected_color);
  expectRelayColor(
      device.Get(), queue.Get(), eye_targets[1].Get(), expected_color);

  const D3D12_RESOURCE_ALLOCATION_INFO allocation =
      device->GetResourceAllocationInfo(0, 1, &eye_description);
  D3D12_HEAP_DESC heap_description{};
  heap_description.SizeInBytes = allocation.SizeInBytes;
  heap_description.Alignment = allocation.Alignment;
  heap_description.Properties = heap_properties;
  heap_description.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
  ComPtr<ID3D12Heap> eye_heap;
  expect(SUCCEEDED(device->CreateHeap(
             &heap_description, IID_PPV_ARGS(&eye_heap))),
         "failed to create heap for candidate placed VRCAM resource");
  ComPtr<ID3D12Resource> placed_eye_resource;
  expect(SUCCEEDED(device->CreatePlacedResource(
             eye_heap.Get(),
             0,
             &eye_description,
             D3D12_RESOURCE_STATE_RENDER_TARGET,
             nullptr,
             IID_PPV_ARGS(&placed_eye_resource))),
         "pass-through candidate placed VRCAM resource creation failed");
  snapshot = {};
  snapshot.size = sizeof(snapshot);
  query_graphics(&snapshot);
  expect(snapshot.candidate_view_resource_count == 3,
         "observer must cover committed and placed candidate eye resources");

  placed_eye_resource.Reset();
  eye_heap.Reset();
  command_list.Reset();
  command_allocator.Reset();
  rtv_heap.Reset();
  eye_resource.Reset();
  unorm_eye_resource.Reset();
  unrelated_resource.Reset();
  swapchain.Reset();
  swapchain1.Reset();
  queue.Reset();
  device.Reset();
  warp_adapter.Reset();
  factory.Reset();
  DestroyWindow(window);
  UnregisterClassW(
      L"VRClientRedengineObserverTest", GetModuleHandleW(nullptr));
}

}  // namespace

int main(int argc, char** argv) {
  HMODULE module = nullptr;
  try {
    const std::array<
        vrclient::adapters::redengine::VrcamCandidateScore, 12>
        live_scores{{
            {200, 32, 32},
            {48, 8, 8},
            {14590, 1459, 1459},
            {144, 16, 16},
            {136, 16, 8},
            {0, 1459, 1459},
            {0, 0, 0},
            {0, 0, 0},
            {44060, 4406, 4406},
            {0, 0, 0},
            {0, 0, 0},
            {844, 4406, 4406},
        }};
    expect(
        vrclient::adapters::redengine::selectVrcamCandidate(live_scores) == 11,
        "selector must prefer the published low-bind texture over its high-bind render peer");
    const std::array<
        vrclient::adapters::redengine::VrcamCandidateScore, 12>
        inline_live_scores{{
            {5, 10, 10},
            {8, 16, 16},
            {21070, 2107, 2107},
            {0, 0, 0},
            {0, 0, 0},
            {0, 2107, 2107},
            {0, 0, 0},
            {0, 0, 0},
            {16840, 1684, 1684},
            {0, 0, 0},
            {0, 0, 0},
            {52, 1684, 1684},
        }};
    expect(
        vrclient::adapters::redengine::selectVrcamCandidate(
            inline_live_scores) == 11,
        "selector must reject a zero-bind black intermediate and match the low-bind VRCAM cadence peer");
    expect(argc == 2, "usage: vr_red4ext_bridge_tests <bridge-dll>");
    module = LoadLibraryA(argv[1]);
    expect(module != nullptr, "failed to load RED4ext bridge DLL");

    const auto supports = reinterpret_cast<SupportsFn>(
        GetProcAddress(module, "Supports"));
    const auto query = reinterpret_cast<QueryFn>(GetProcAddress(module, "Query"));
    const auto mainFn = reinterpret_cast<MainFn>(GetProcAddress(module, "Main"));
    const auto query_graphics = reinterpret_cast<QueryGraphicsFn>(
        GetProcAddress(module, "vrclient_redengine_query_graphics"));
    const auto request_vrcam_capture =
        reinterpret_cast<RequestVrcamCaptureFn>(GetProcAddress(
            module, "vrclient_redengine_request_vrcam_capture"));
    expect(supports != nullptr && query != nullptr && mainFn != nullptr &&
               query_graphics != nullptr && request_vrcam_capture != nullptr,
           "bridge is missing required RED4ext exports");
    expect(supports() == RED4EXT_API_VERSION_1, "bridge must use RED4ext API v1");

    RED4ext::v1::PluginInfo info{};
    query(&info);
    expect(info.name != nullptr && std::wstring(info.name) == L"VRClient.REDengine",
           "unexpected bridge plugin name");
    const bool openxr_build =
        info.version.major == 0 && info.version.minor == 6 &&
        info.version.patch <= 6;
    expect(openxr_build ||
               (info.version.major == 0 && info.version.minor == 5 &&
                info.version.patch == 0),
           "VRCAM bridge must report transport 0.5.0 or OpenXR 0.6.x");
    expect(info.runtime.major == 3 && info.runtime.minor == 0 &&
               info.runtime.build == 80 && info.runtime.revision == 51928,
           "bridge runtime must be pinned to Cyberpunk 2.31");

    RED4ext::v1::Logger logger{};
    logger.Info = &infoLog;
    logger.Error = &errorLog;
    // RED4ext follows CDPR's compact product-version convention: displayed
    // patch 2.31 is encoded as semantic fields 2.3.1.
    RED4ext::v1::SemVer runtime{2, 3, 1, {0, 0}};
    RED4ext::v1::Sdk sdk{};
    sdk.runtime = &runtime;
    sdk.logger = &logger;
    const auto handle = reinterpret_cast<RED4ext::v1::PluginHandle>(1);
    expect(mainFn(handle, RED4ext::v1::EMainReason::Load, &sdk),
           "supported bridge load failed");
    exerciseD3D12Observation(query_graphics, request_vrcam_capture);
    expect(mainFn(handle, RED4ext::v1::EMainReason::Unload, &sdk),
           "supported bridge unload failed");
    expect(LogCounts::info == (openxr_build ? 5 : 4),
           "bridge lifecycle and graphics/OpenXR evidence must be logged");

    runtime = {2, 3, 0, {0, 0}};
    expect(!mainFn(handle, RED4ext::v1::EMainReason::Load, &sdk),
           "bridge must refuse a mismatched Cyberpunk runtime");
    expect(LogCounts::error == 1, "runtime refusal must be logged");

    FreeLibrary(module);
    std::cout << "RED4ext bridge ABI, pass-through D3D12 observation, and "
                 "fail-closed lifecycle tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    if (module != nullptr) {
      FreeLibrary(module);
    }
    std::cerr << error.what() << '\n';
    return 1;
  }
}

#include <windows.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "adapters/unreal/d3d12_stereo_diagnostic_renderer.h"
#include "adapters/unreal/d3d12_scene_relay_renderer.h"
#include "public/vr_runtime_api.h"

namespace {

using Microsoft::WRL::ComPtr;

bool validStereoTargets(const std::array<ComPtr<ID3D12Resource>, 2>& targets,
                        const UINT eye_count) {
  return eye_count == targets.size() && targets[0] && targets[1] &&
         targets[0].Get() != targets[1].Get();
}

ComPtr<IDXGIAdapter1> chooseAdapter(IDXGIFactory6* factory) {
  ComPtr<IDXGIAdapter1> adapter;
  for (UINT index = 0;
       factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND;
       ++index) {
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(adapter->GetDesc1(&desc)) ||
        (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
      adapter.Reset();
      continue;
    }

    if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                    __uuidof(ID3D12Device), nullptr))) {
      return adapter;
    }
    adapter.Reset();
  }

  ComPtr<IDXGIAdapter> warp;
  if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)))) {
    return {};
  }
  warp.As(&adapter);
  return adapter;
}

int fail(const char* stage, const HRESULT hr) {
  std::fprintf(stderr, "FAIL stage=%s hr=0x%08lx\n", stage,
               static_cast<unsigned long>(hr));
  return 1;
}

}  // namespace

int main() {
  ComPtr<ID3D12Debug> debug;
  if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
    return fail("debug-layer", E_NOINTERFACE);
  }
  debug->EnableDebugLayer();

  ComPtr<IDXGIFactory6> factory;
  HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    return fail("factory", hr);
  }

  const ComPtr<IDXGIAdapter1> adapter = chooseAdapter(factory.Get());
  if (!adapter) {
    return fail("adapter", DXGI_ERROR_NOT_FOUND);
  }

  ComPtr<ID3D12Device> device;
  hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                         IID_PPV_ARGS(&device));
  if (FAILED(hr)) {
    return fail("device", hr);
  }
  ComPtr<ID3D12InfoQueue> info_queue;
  if (FAILED(device.As(&info_queue))) {
    return fail("info-queue", E_NOINTERFACE);
  }

  D3D12_COMMAND_QUEUE_DESC queue_desc{};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  ComPtr<ID3D12CommandQueue> queue;
  hr = device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue));
  if (FAILED(hr)) {
    return fail("queue", hr);
  }

  ComPtr<ID3D12CommandAllocator> allocator;
  hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                      IID_PPV_ARGS(&allocator));
  if (FAILED(hr)) {
    return fail("allocator", hr);
  }

  ComPtr<ID3D12GraphicsCommandList> command_list;
  hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                 allocator.Get(), nullptr,
                                 IID_PPV_ARGS(&command_list));
  if (FAILED(hr)) {
    return fail("command-list", hr);
  }

  D3D12_HEAP_PROPERTIES resource_heap{};
  resource_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  resource_heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  resource_heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  resource_heap.CreationNodeMask = 1;
  resource_heap.VisibleNodeMask = 1;

  D3D12_RESOURCE_DESC resource_desc{};
  resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  resource_desc.Width = 4;
  resource_desc.Height = 4;
  resource_desc.DepthOrArraySize = 1;
  resource_desc.MipLevels = 1;
  // OpenXR D3D12 runtimes commonly expose typeless swapchain textures. The
  // renderer must create an RTV using the selected typed swapchain format.
  resource_desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
  resource_desc.SampleDesc.Count = 1;
  resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

  const std::array<std::array<float, 4>, 2> eye_colors{{
      {0.8F, 0.1F, 0.1F, 1.0F},
      {0.1F, 0.2F, 0.8F, 1.0F},
  }};
  D3D12_CLEAR_VALUE clear_value{};
  clear_value.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

  std::array<ComPtr<ID3D12Resource>, 2> eye_targets;
  std::array<ComPtr<ID3D12Resource>, 2> readback_buffers;
  std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, 2> footprints{};
  std::array<UINT64, 2> readback_sizes{};

  D3D12_HEAP_PROPERTIES readback_heap{};
  readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
  readback_heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  readback_heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  readback_heap.CreationNodeMask = 1;
  readback_heap.VisibleNodeMask = 1;

  for (UINT eye = 0; eye < eye_targets.size(); ++eye) {
    for (UINT channel = 0; channel < 4; ++channel) {
      clear_value.Color[channel] = eye_colors[eye][channel];
    }
    hr = device->CreateCommittedResource(
        &resource_heap, D3D12_HEAP_FLAG_NONE, &resource_desc,
        D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
        IID_PPV_ARGS(&eye_targets[eye]));
    if (FAILED(hr)) {
      return fail("eye-target", hr);
    }

    UINT row_count = 0;
    UINT64 row_size = 0;
    device->GetCopyableFootprints(
        &resource_desc, 0, 1, 0, &footprints[eye], &row_count, &row_size,
        &readback_sizes[eye]);

    D3D12_RESOURCE_DESC readback_desc{};
    readback_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readback_desc.Width = readback_sizes[eye];
    readback_desc.Height = 1;
    readback_desc.DepthOrArraySize = 1;
    readback_desc.MipLevels = 1;
    readback_desc.SampleDesc.Count = 1;
    readback_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = device->CreateCommittedResource(
        &readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&readback_buffers[eye]));
    if (FAILED(hr)) {
      return fail("readback-buffer", hr);
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = eye_targets[eye].Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    command_list->ResourceBarrier(1, &barrier);

    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = readback_buffers[eye].Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprints[eye];
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = eye_targets[eye].Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source.SubresourceIndex = 0;
    command_list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
  }

  if (validStereoTargets(eye_targets, 1) ||
      !validStereoTargets(eye_targets, 2)) {
    return fail("stereo-contract", E_UNEXPECTED);
  }

  VrRuntimeFrameData frame{};
  frame.timing.frame_index = 1;
  frame.timing.predicted_display_time_ns = 1;
  frame.eye_count = 2;
  frame.eyes[0].eye = VR_RUNTIME_EYE_LEFT;
  frame.eyes[1].eye = VR_RUNTIME_EYE_RIGHT;
  for (auto& eye : frame.eyes) {
    eye.fov_angle_left = -0.75F;
    eye.fov_angle_right = 0.75F;
    eye.fov_angle_up = 0.75F;
    eye.fov_angle_down = -0.75F;
  }
  VrRuntimeRenderTarget render_targets[2]{};
  for (UINT eye = 0; eye < eye_targets.size(); ++eye) {
    render_targets[eye].backend = VR_RUNTIME_GRAPHICS_BACKEND_D3D12;
    render_targets[eye].eye = static_cast<VrRuntimeEye>(eye);
    render_targets[eye].color_texture = eye_targets[eye].Get();
    render_targets[eye].width = 4;
    render_targets[eye].height = 4;
    render_targets[eye].color_format = DXGI_FORMAT_R8G8B8A8_UNORM;
  }
  vrclient::adapters::unreal::D3D12StereoDiagnosticRenderer renderer;
  info_queue->ClearStoredMessages();
  if (!renderer.initialize(device.Get(), queue.Get()) ||
      renderer.render(frame, render_targets, 2) != VR_RUNTIME_OK) {
    return fail("diagnostic-render", E_FAIL);
  }

  vrclient::adapters::unreal::D3D12SceneRelayRenderer scene_relay;
  if (!scene_relay.initialize(device.Get(), queue.Get()) ||
      !scene_relay.configureSideBySideProjection({110.0F, 1.0F}) ||
      scene_relay.render(frame, render_targets, 2) != VR_RUNTIME_SKIPPED) {
    return fail("scene-relay-requires-capture", E_FAIL);
  }

  hr = command_list->Close();
  if (FAILED(hr)) {
    return fail("command-list-close", hr);
  }
  ID3D12CommandList* submitted[] = {command_list.Get()};
  queue->ExecuteCommandLists(1, submitted);

  ComPtr<ID3D12Fence> fence;
  hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
  if (FAILED(hr)) {
    return fail("fence", hr);
  }
  constexpr UINT64 kFenceValue = 1;
  hr = queue->Signal(fence.Get(), kFenceValue);
  if (FAILED(hr)) {
    return fail("signal", hr);
  }

  HANDLE fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!fence_event) {
    return fail("fence-event", HRESULT_FROM_WIN32(GetLastError()));
  }
  if (fence->GetCompletedValue() < kFenceValue) {
    hr = fence->SetEventOnCompletion(kFenceValue, fence_event);
    if (FAILED(hr)) {
      CloseHandle(fence_event);
      return fail("fence-wait", hr);
    }
    const DWORD wait_result = WaitForSingleObject(fence_event, 10'000);
    if (wait_result != WAIT_OBJECT_0) {
      const DWORD error =
          wait_result == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT;
      CloseHandle(fence_event);
      return fail("fence-timeout", HRESULT_FROM_WIN32(error));
    }
  }
  CloseHandle(fence_event);

  const UINT64 message_count = info_queue->GetNumStoredMessages();
  for (UINT64 index = 0; index < message_count; ++index) {
    SIZE_T message_size = 0;
    info_queue->GetMessage(index, nullptr, &message_size);
    std::vector<std::uint8_t> storage(message_size);
    auto* message =
        reinterpret_cast<D3D12_MESSAGE*>(storage.data());
    if (FAILED(info_queue->GetMessage(index, message, &message_size))) {
      return fail("debug-message-read", E_FAIL);
    }
    if (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
        message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) {
      std::fprintf(
          stderr,
          "D3D12 validation: %s\n",
          message->pDescription != nullptr ? message->pDescription : "");
      return fail("debug-layer-validation", E_FAIL);
    }
  }

  for (UINT eye = 0; eye < readback_buffers.size(); ++eye) {
    void* mapped = nullptr;
    const D3D12_RANGE read_range{0, static_cast<SIZE_T>(readback_sizes[eye])};
    hr = readback_buffers[eye]->Map(0, &read_range, &mapped);
    if (FAILED(hr) || mapped == nullptr) {
      return fail("readback-map", hr);
    }

    const auto* pixel = static_cast<const std::uint8_t*>(mapped) +
                        footprints[eye].Offset;
    for (UINT channel = 0; channel < 4; ++channel) {
      const auto expected = static_cast<std::uint8_t>(
          std::lround(eye_colors[eye][channel] * 255.0F));
      const int delta = static_cast<int>(pixel[channel]) -
                        static_cast<int>(expected);
      if (delta < -1 || delta > 1) {
        readback_buffers[eye]->Unmap(0, nullptr);
        return fail("readback-pixel", E_UNEXPECTED);
      }
    }
    const D3D12_RANGE written_range{0, 0};
    readback_buffers[eye]->Unmap(0, &written_range);
  }

  // Scene-relay tracer bullet: capture one flat rendered texture, then prove
  // that the public relay interface delivers its pixels to both typeless
  // OpenXR-style eye targets. This is scene transport, not true stereo.
  const std::array<float, 4> relay_color{0.15F, 0.65F, 0.25F, 1.0F};
  D3D12_RESOURCE_DESC source_desc = resource_desc;
  // Meccha's live DX12 swap chain uses this 10-bit format. Keep the source
  // exact so the probe exercises the shader's real cross-format conversion
  // into the RGBA8 OpenXR eye textures.
  source_desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
  D3D12_CLEAR_VALUE source_clear{};
  source_clear.Format = source_desc.Format;
  for (UINT channel = 0; channel < 4; ++channel) {
    source_clear.Color[channel] = relay_color[channel];
  }
  ComPtr<ID3D12Resource> scene_source;
  hr = device->CreateCommittedResource(
      &resource_heap, D3D12_HEAP_FLAG_NONE, &source_desc,
      D3D12_RESOURCE_STATE_RENDER_TARGET, &source_clear,
      IID_PPV_ARGS(&scene_source));
  if (FAILED(hr)) {
    return fail("scene-relay-source", hr);
  }
  D3D12_DESCRIPTOR_HEAP_DESC source_rtv_heap_desc{};
  source_rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  source_rtv_heap_desc.NumDescriptors = 1;
  ComPtr<ID3D12DescriptorHeap> source_rtv_heap;
  hr = device->CreateDescriptorHeap(
      &source_rtv_heap_desc, IID_PPV_ARGS(&source_rtv_heap));
  if (FAILED(hr)) {
    return fail("scene-relay-source-rtv-heap", hr);
  }
  device->CreateRenderTargetView(
      scene_source.Get(), nullptr,
      source_rtv_heap->GetCPUDescriptorHandleForHeapStart());

  hr = allocator->Reset();
  if (FAILED(hr) || FAILED(command_list->Reset(allocator.Get(), nullptr))) {
    return fail("scene-relay-source-reset", hr);
  }
  command_list->ClearRenderTargetView(
      source_rtv_heap->GetCPUDescriptorHandleForHeapStart(),
      relay_color.data(), 0, nullptr);
  std::array<D3D12_RESOURCE_BARRIER, 3> relay_setup_barriers{};
  relay_setup_barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  relay_setup_barriers[0].Transition.pResource = scene_source.Get();
  relay_setup_barriers[0].Transition.Subresource =
      D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  relay_setup_barriers[0].Transition.StateBefore =
      D3D12_RESOURCE_STATE_RENDER_TARGET;
  relay_setup_barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
  for (UINT eye = 0; eye < eye_targets.size(); ++eye) {
    auto& barrier = relay_setup_barriers[eye + 1];
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = eye_targets[eye].Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
  }
  command_list->ResourceBarrier(
      static_cast<UINT>(relay_setup_barriers.size()),
      relay_setup_barriers.data());
  if (FAILED(command_list->Close())) {
    return fail("scene-relay-source-close", E_FAIL);
  }
  ID3D12CommandList* relay_setup_lists[] = {command_list.Get()};
  queue->ExecuteCommandLists(1, relay_setup_lists);
  constexpr UINT64 kRelaySetupFence = 2;
  if (FAILED(queue->Signal(fence.Get(), kRelaySetupFence))) {
    return fail("scene-relay-source-signal", E_FAIL);
  }
  HANDLE relay_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (relay_event == nullptr ||
      FAILED(fence->SetEventOnCompletion(kRelaySetupFence, relay_event)) ||
      WaitForSingleObject(relay_event, 10'000) != WAIT_OBJECT_0) {
    if (relay_event != nullptr) CloseHandle(relay_event);
    return fail("scene-relay-source-wait", E_FAIL);
  }
  CloseHandle(relay_event);

  if (scene_relay.capture(scene_source.Get()) != VR_RUNTIME_OK ||
      scene_relay.render(frame, render_targets, 2) != VR_RUNTIME_OK) {
    return fail("scene-relay-capture-render", E_FAIL);
  }

  hr = allocator->Reset();
  if (FAILED(hr) || FAILED(command_list->Reset(allocator.Get(), nullptr))) {
    return fail("scene-relay-readback-reset", hr);
  }
  for (UINT eye = 0; eye < eye_targets.size(); ++eye) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = eye_targets[eye].Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    command_list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = readback_buffers[eye].Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprints[eye];
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = eye_targets[eye].Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    command_list->CopyTextureRegion(
        &destination, 0, 0, 0, &source, nullptr);
  }
  if (FAILED(command_list->Close())) {
    return fail("scene-relay-readback-close", E_FAIL);
  }
  ID3D12CommandList* relay_readback_lists[] = {command_list.Get()};
  queue->ExecuteCommandLists(1, relay_readback_lists);
  constexpr UINT64 kRelayReadbackFence = 3;
  if (FAILED(queue->Signal(fence.Get(), kRelayReadbackFence))) {
    return fail("scene-relay-readback-signal", E_FAIL);
  }
  relay_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (relay_event == nullptr ||
      FAILED(fence->SetEventOnCompletion(kRelayReadbackFence, relay_event)) ||
      WaitForSingleObject(relay_event, 10'000) != WAIT_OBJECT_0) {
    if (relay_event != nullptr) CloseHandle(relay_event);
    return fail("scene-relay-readback-wait", E_FAIL);
  }
  CloseHandle(relay_event);

  for (UINT eye = 0; eye < readback_buffers.size(); ++eye) {
    void* mapped = nullptr;
    const D3D12_RANGE read_range{0, static_cast<SIZE_T>(readback_sizes[eye])};
    if (FAILED(readback_buffers[eye]->Map(0, &read_range, &mapped)) ||
        mapped == nullptr) {
      return fail("scene-relay-readback-map", E_FAIL);
    }
    const auto* pixel = static_cast<const std::uint8_t*>(mapped) +
                        footprints[eye].Offset;
    for (UINT channel = 0; channel < 4; ++channel) {
      const auto expected = static_cast<std::uint8_t>(
          std::lround(relay_color[channel] * 255.0F));
      const int delta = static_cast<int>(pixel[channel]) - expected;
      if (delta < -2 || delta > 2) {
        std::fprintf(
            stderr,
            "stereo eye=%u channel=%u observed=%u expected=%u\n",
            eye, channel, static_cast<unsigned>(pixel[channel]),
            static_cast<unsigned>(expected));
        readback_buffers[eye]->Unmap(0, nullptr);
        return fail("scene-relay-readback-pixel", E_UNEXPECTED);
      }
    }
    const D3D12_RANGE written_range{0, 0};
    readback_buffers[eye]->Unmap(0, &written_range);
  }

  // True-stereo transport tracer: emulate Unreal's side-by-side native stereo
  // backbuffer, then prove each OpenXR target receives only its matching half.
  const std::array<float, 4> stereo_left_color{0.8F, 0.1F, 0.2F, 1.0F};
  const std::array<float, 4> stereo_right_color{0.1F, 0.25F, 0.85F, 1.0F};
  ComPtr<ID3D12Resource> stereo_source;
  hr = device->CreateCommittedResource(
      &resource_heap, D3D12_HEAP_FLAG_NONE, &source_desc,
      D3D12_RESOURCE_STATE_RENDER_TARGET, &source_clear,
      IID_PPV_ARGS(&stereo_source));
  if (FAILED(hr)) {
    return fail("stereo-relay-source", hr);
  }
  device->CreateRenderTargetView(
      stereo_source.Get(), nullptr,
      source_rtv_heap->GetCPUDescriptorHandleForHeapStart());

  if (FAILED(allocator->Reset()) ||
      FAILED(command_list->Reset(allocator.Get(), nullptr))) {
    return fail("stereo-relay-setup-reset", E_FAIL);
  }
  const D3D12_RECT left_rect{0, 0, 2, 4};
  const D3D12_RECT right_rect{2, 0, 4, 4};
  command_list->ClearRenderTargetView(
      source_rtv_heap->GetCPUDescriptorHandleForHeapStart(),
      stereo_left_color.data(), 1, &left_rect);
  command_list->ClearRenderTargetView(
      source_rtv_heap->GetCPUDescriptorHandleForHeapStart(),
      stereo_right_color.data(), 1, &right_rect);
  std::array<D3D12_RESOURCE_BARRIER, 3> stereo_setup_barriers{};
  stereo_setup_barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  stereo_setup_barriers[0].Transition.pResource = stereo_source.Get();
  stereo_setup_barriers[0].Transition.Subresource =
      D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  stereo_setup_barriers[0].Transition.StateBefore =
      D3D12_RESOURCE_STATE_RENDER_TARGET;
  stereo_setup_barriers[0].Transition.StateAfter =
      D3D12_RESOURCE_STATE_PRESENT;
  for (UINT eye = 0; eye < eye_targets.size(); ++eye) {
    auto& barrier = stereo_setup_barriers[eye + 1];
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = eye_targets[eye].Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
  }
  command_list->ResourceBarrier(
      static_cast<UINT>(stereo_setup_barriers.size()),
      stereo_setup_barriers.data());
  if (FAILED(command_list->Close())) {
    return fail("stereo-relay-setup-close", E_FAIL);
  }
  ID3D12CommandList* stereo_setup_lists[] = {command_list.Get()};
  queue->ExecuteCommandLists(1, stereo_setup_lists);
  constexpr UINT64 kStereoSetupFence = 4;
  if (FAILED(queue->Signal(fence.Get(), kStereoSetupFence))) {
    return fail("stereo-relay-setup-signal", E_FAIL);
  }
  relay_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (relay_event == nullptr ||
      FAILED(fence->SetEventOnCompletion(kStereoSetupFence, relay_event)) ||
      WaitForSingleObject(relay_event, 10'000) != WAIT_OBJECT_0) {
    if (relay_event != nullptr) CloseHandle(relay_event);
    return fail("stereo-relay-setup-wait", E_FAIL);
  }
  CloseHandle(relay_event);

  // The 4x4 probe packs two deliberately narrow 2x4 source eyes. Keep the
  // synthetic target frustum inside that source while exercising the same
  // fail-closed projection containment used by the live 960x1080 eye halves.
  for (auto& eye : frame.eyes) {
    eye.fov_angle_left = -0.5F;
    eye.fov_angle_right = 0.5F;
  }
  if (scene_relay.captureSideBySideStereo(stereo_source.Get()) !=
          VR_RUNTIME_OK ||
      scene_relay.render(frame, render_targets, 2) != VR_RUNTIME_OK) {
    return fail("stereo-relay-capture-render", E_FAIL);
  }

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT flat_footprint{};
  UINT flat_rows = 0;
  UINT64 flat_row_size = 0;
  UINT64 flat_readback_size = 0;
  device->GetCopyableFootprints(
      &source_desc, 0, 1, 0, &flat_footprint, &flat_rows,
      &flat_row_size, &flat_readback_size);
  D3D12_RESOURCE_DESC flat_readback_desc{};
  flat_readback_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  flat_readback_desc.Width = flat_readback_size;
  flat_readback_desc.Height = 1;
  flat_readback_desc.DepthOrArraySize = 1;
  flat_readback_desc.MipLevels = 1;
  flat_readback_desc.SampleDesc.Count = 1;
  flat_readback_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> flat_readback;
  if (FAILED(device->CreateCommittedResource(
          &readback_heap, D3D12_HEAP_FLAG_NONE, &flat_readback_desc,
          D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
          IID_PPV_ARGS(&flat_readback)))) {
    return fail("stereo-flat-readback-buffer", E_FAIL);
  }

  if (FAILED(allocator->Reset()) ||
      FAILED(command_list->Reset(allocator.Get(), nullptr))) {
    return fail("stereo-relay-readback-reset", E_FAIL);
  }
  for (UINT eye = 0; eye < eye_targets.size(); ++eye) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = eye_targets[eye].Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    command_list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = readback_buffers[eye].Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprints[eye];
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = eye_targets[eye].Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    command_list->CopyTextureRegion(
        &destination, 0, 0, 0, &source, nullptr);
  }
  D3D12_RESOURCE_BARRIER flat_to_copy{};
  flat_to_copy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  flat_to_copy.Transition.pResource = stereo_source.Get();
  flat_to_copy.Transition.Subresource =
      D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  flat_to_copy.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
  flat_to_copy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  command_list->ResourceBarrier(1, &flat_to_copy);
  D3D12_TEXTURE_COPY_LOCATION flat_destination{};
  flat_destination.pResource = flat_readback.Get();
  flat_destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  flat_destination.PlacedFootprint = flat_footprint;
  D3D12_TEXTURE_COPY_LOCATION flat_source{};
  flat_source.pResource = stereo_source.Get();
  flat_source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  command_list->CopyTextureRegion(
      &flat_destination, 0, 0, 0, &flat_source, nullptr);
  std::swap(
      flat_to_copy.Transition.StateBefore,
      flat_to_copy.Transition.StateAfter);
  command_list->ResourceBarrier(1, &flat_to_copy);
  if (FAILED(command_list->Close())) {
    return fail("stereo-relay-readback-close", E_FAIL);
  }
  ID3D12CommandList* stereo_readback_lists[] = {command_list.Get()};
  queue->ExecuteCommandLists(1, stereo_readback_lists);
  constexpr UINT64 kStereoReadbackFence = 5;
  if (FAILED(queue->Signal(fence.Get(), kStereoReadbackFence))) {
    return fail("stereo-relay-readback-signal", E_FAIL);
  }
  relay_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (relay_event == nullptr ||
      FAILED(fence->SetEventOnCompletion(kStereoReadbackFence, relay_event)) ||
      WaitForSingleObject(relay_event, 10'000) != WAIT_OBJECT_0) {
    if (relay_event != nullptr) CloseHandle(relay_event);
    return fail("stereo-relay-readback-wait", E_FAIL);
  }
  CloseHandle(relay_event);

  const std::array<std::array<float, 4>, 2> stereo_colors{
      stereo_left_color, stereo_right_color};
  for (UINT eye = 0; eye < readback_buffers.size(); ++eye) {
    void* mapped = nullptr;
    const D3D12_RANGE read_range{0, static_cast<SIZE_T>(readback_sizes[eye])};
    if (FAILED(readback_buffers[eye]->Map(0, &read_range, &mapped)) ||
        mapped == nullptr) {
      return fail("stereo-relay-readback-map", E_FAIL);
    }
    const auto* pixel = static_cast<const std::uint8_t*>(mapped) +
                        footprints[eye].Offset;
    for (UINT channel = 0; channel < 4; ++channel) {
      const auto expected = static_cast<std::uint8_t>(
          std::lround(stereo_colors[eye][channel] * 255.0F));
      const int delta = static_cast<int>(pixel[channel]) - expected;
      if (delta < -2 || delta > 2) {
        std::fprintf(
            stderr,
            "distinct eye=%u channel=%u observed=%u expected=%u\n",
            eye, channel, static_cast<unsigned>(pixel[channel]),
            static_cast<unsigned>(expected));
        readback_buffers[eye]->Unmap(0, nullptr);
        return fail("stereo-relay-distinct-pixel", E_UNEXPECTED);
      }
    }
    const D3D12_RANGE written_range{0, 0};
    readback_buffers[eye]->Unmap(0, &written_range);
  }

  void* flat_mapped = nullptr;
  const D3D12_RANGE flat_read_range{
      0, static_cast<SIZE_T>(flat_readback_size)};
  if (FAILED(flat_readback->Map(0, &flat_read_range, &flat_mapped)) ||
      flat_mapped == nullptr) {
    return fail("stereo-flat-readback-map", E_FAIL);
  }
  const auto* flat_bytes = static_cast<const std::uint8_t*>(flat_mapped) +
      flat_footprint.Offset;
  const auto* flat_left = reinterpret_cast<const std::uint32_t*>(flat_bytes);
  const auto* flat_right = reinterpret_cast<const std::uint32_t*>(
      flat_bytes + 3 * sizeof(std::uint32_t));
  const auto red10 = [](std::uint32_t pixel) {
    return static_cast<float>(pixel & 0x3FFU) / 1023.0F;
  };
  const auto green10 = [](std::uint32_t pixel) {
    return static_cast<float>((pixel >> 10) & 0x3FFU) / 1023.0F;
  };
  const auto blue10 = [](std::uint32_t pixel) {
    return static_cast<float>((pixel >> 20) & 0x3FFU) / 1023.0F;
  };
  const auto matches_left = [&](std::uint32_t pixel) {
    return std::abs(red10(pixel) - stereo_left_color[0]) < 0.01F &&
        std::abs(green10(pixel) - stereo_left_color[1]) < 0.01F &&
        std::abs(blue10(pixel) - stereo_left_color[2]) < 0.01F;
  };
  if (!matches_left(*flat_left) || !matches_left(*flat_right)) {
    flat_readback->Unmap(0, nullptr);
    return fail("stereo-flat-left-eye-restoration", E_UNEXPECTED);
  }
  const D3D12_RANGE flat_written_range{0, 0};
  flat_readback->Unmap(0, &flat_written_range);

  std::puts(
      "PASS backend=d3d12 eyes=2 clears=2 readback=verified "
      "scene-relay=verified stereo-relay=distinct source=r10g10b10a2 "
      "fence=complete negative-control=pass");
  return 0;
}

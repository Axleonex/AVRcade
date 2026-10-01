#include "adapters/redengine/redengine_graphics_observer.h"
#include "adapters/redengine/redengine_vrcam_selector.h"
#include "adapters/unreal/d3d12_scene_relay_renderer.h"

#include <algorithm>
#include <cstring>

namespace vrclient::adapters::redengine {
namespace {

constexpr std::uint32_t kCandidateViewWidth = 1080;
constexpr std::uint32_t kCandidateViewHeight = 1200;
constexpr DXGI_FORMAT kCandidateColorFormat =
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

}  // namespace

GraphicsObserver::GraphicsObserver() = default;
GraphicsObserver::~GraphicsObserver() = default;

bool GraphicsObserver::observe(IUnknown* device_or_queue, IUnknown* swapchain) {
  if (device_or_queue == nullptr || swapchain == nullptr) {
    return false;
  }

  Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
  Microsoft::WRL::ComPtr<ID3D12Device> device;
  Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain3;
  DXGI_SWAP_CHAIN_DESC1 description{};
  if (FAILED(device_or_queue->QueryInterface(IID_PPV_ARGS(&queue))) ||
      queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT ||
      FAILED(queue->GetDevice(IID_PPV_ARGS(&device))) ||
      FAILED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain3))) ||
      FAILED(swapchain3->GetDesc1(&description)) ||
      description.Width == 0 || description.Height == 0 ||
      description.BufferCount < 2) {
    return false;
  }

  Microsoft::WRL::ComPtr<ID3D12Device> swapchain_device;
  if (FAILED(swapchain3->GetDevice(IID_PPV_ARGS(&swapchain_device))) ||
      device.Get() != swapchain_device.Get()) {
    return false;
  }

  auto relay = std::make_unique<
      vrclient::adapters::unreal::D3D12SceneRelayRenderer>();
  if (!relay->initialize(device.Get(), queue.Get())) {
    relay.reset();
  }

  std::lock_guard lock(mutex_);
  device_ = device;
  queue_ = queue;
  swapchain_ = swapchain3;
  description_ = description;
  observed_swapchain_.store(swapchain3.Get(), std::memory_order_release);
  present_count_.store(0, std::memory_order_relaxed);
  resize_count_.store(0, std::memory_order_relaxed);
  candidate_view_resource_count_.store(0, std::memory_order_relaxed);
  candidate_color_resource_.Reset();
  selected_vrcam_resource_.Reset();
  selected_vrcam_candidate_index_ = UINT32_MAX;
  vrcam_relay_ = std::move(relay);
  vrcam_capture_requested_.store(true, std::memory_order_relaxed);
  vrcam_capture_count_.store(0, std::memory_order_relaxed);
  vrcam_capture_fail_count_.store(0, std::memory_order_relaxed);
  candidate_color_resource_count_.store(0, std::memory_order_relaxed);
  candidate_color_rtv_count_.store(0, std::memory_order_relaxed);
  candidate_unorm_resource_count_.store(0, std::memory_order_relaxed);
  unorm_candidates_ = {};
  unorm_candidate_count_ = 0;
  published_unorm_candidate_count_.store(0, std::memory_order_release);
  unorm_rtv_mappings_ = {};
  command_list_usages_ = {};
  command_list_usage_count_ = 0;
  unorm_rtv_mapping_count_.store(0, std::memory_order_release);
  rtv_descriptor_stride_.store(
      device->GetDescriptorHandleIncrementSize(
          D3D12_DESCRIPTOR_HEAP_TYPE_RTV),
      std::memory_order_release);
  for (auto& bind_count : unorm_bind_counts_) {
    bind_count.store(0, std::memory_order_relaxed);
  }
  for (auto& clear_count : unorm_clear_counts_) {
    clear_count.store(0, std::memory_order_relaxed);
  }
  for (auto& transition_count : unorm_shader_read_transition_counts_) {
    transition_count.store(0, std::memory_order_relaxed);
  }
  for (auto& copy_count : unorm_copy_source_counts_) {
    copy_count.store(0, std::memory_order_relaxed);
  }
  for (auto& state : unorm_last_states_) {
    state.store(0, std::memory_order_relaxed);
  }
  for (auto* counts : {&unorm_primary_queue_execute_counts_,
                       &unorm_other_direct_queue_execute_counts_,
                       &unorm_compute_queue_execute_counts_,
                       &unorm_copy_queue_execute_counts_}) {
    for (auto& count : *counts) {
      count.store(0, std::memory_order_relaxed);
    }
  }
  candidate_unorm_rtv_resource_count_.store(0, std::memory_order_relaxed);
  candidate_unorm_rtv_count_.store(0, std::memory_order_relaxed);
  candidate_unorm_bound_resource_count_.store(0, std::memory_order_relaxed);
  candidate_unorm_bind_count_.store(0, std::memory_order_relaxed);
  candidate_unorm_cleared_resource_count_.store(0, std::memory_order_relaxed);
  candidate_unorm_clear_count_.store(0, std::memory_order_relaxed);
  candidate_unorm_shader_read_resource_count_.store(
      0, std::memory_order_relaxed);
  candidate_unorm_shader_read_transition_count_.store(
      0, std::memory_order_relaxed);
  candidate_unorm_copy_source_resource_count_.store(
      0, std::memory_order_relaxed);
  candidate_unorm_copy_source_count_.store(0, std::memory_order_relaxed);
  return true;
}

void GraphicsObserver::recordPresent(IDXGISwapChain* swapchain) {
  if (swapchain == nullptr ||
      observed_swapchain_.load(std::memory_order_acquire) != swapchain) {
    return;
  }
  present_count_.fetch_add(1, std::memory_order_relaxed);
}

void GraphicsObserver::requestVrcamCapture() {
  vrcam_capture_requested_.store(true, std::memory_order_release);
}

bool GraphicsObserver::makeD3D12Binding(
    VrRuntimeD3D12Binding* binding) const {
  if (binding == nullptr) {
    return false;
  }
  std::lock_guard lock(mutex_);
  if (!device_ || !queue_) {
    *binding = {};
    return false;
  }
  binding->size = sizeof(*binding);
  binding->device = device_.Get();
  binding->queue = queue_.Get();
  return true;
}

VrRuntimeResult GraphicsObserver::renderCapturedVrcamFrame(
    const VrRuntimeFrameData& frame,
    const VrRuntimeRenderTarget* targets,
    std::uint32_t target_count) {
  vrclient::adapters::unreal::D3D12SceneRelayRenderer* relay = nullptr;
  {
    std::lock_guard lock(mutex_);
    relay = vrcam_relay_.get();
  }
  return relay != nullptr
      ? relay->render(frame, targets, target_count)
      : VR_RUNTIME_ERROR_STATE;
}

VrRuntimeResult GraphicsObserver::readbackCapturedVrcam(
    std::vector<std::uint8_t>* pixels,
    std::uint32_t* width,
    std::uint32_t* height) {
  vrclient::adapters::unreal::D3D12SceneRelayRenderer* relay = nullptr;
  {
    std::lock_guard lock(mutex_);
    relay = vrcam_relay_.get();
  }
  return relay != nullptr
      ? relay->readbackCapturedRgba8(pixels, width, height)
      : VR_RUNTIME_ERROR_STATE;
}

void GraphicsObserver::recordResize(IDXGISwapChain* swapchain) {
  if (swapchain == nullptr ||
      observed_swapchain_.load(std::memory_order_acquire) != swapchain) {
    return;
  }
  Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain3;
  DXGI_SWAP_CHAIN_DESC1 description{};
  if (FAILED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain3))) ||
      FAILED(swapchain3->GetDesc1(&description))) {
    return;
  }
  {
    std::lock_guard lock(mutex_);
    description_ = description;
  }
  resize_count_.fetch_add(1, std::memory_order_relaxed);
}

void GraphicsObserver::recordResource(
    const D3D12_RESOURCE_DESC* description,
    IUnknown* resource) {
  if (description == nullptr ||
      description->Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
      description->Width != kCandidateViewWidth ||
      description->Height != kCandidateViewHeight ||
      (description->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                             D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) == 0) {
    return;
  }
  candidate_view_resource_count_.fetch_add(1, std::memory_order_relaxed);
  if (description->Format == DXGI_FORMAT_R8G8B8A8_UNORM &&
      (description->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0 &&
      (description->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) == 0) {
    candidate_unorm_resource_count_.fetch_add(1, std::memory_order_relaxed);
    if (resource != nullptr) {
      Microsoft::WRL::ComPtr<ID3D12Resource> unorm_resource;
      if (SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&unorm_resource)))) {
        std::lock_guard lock(mutex_);
        if (unorm_candidate_count_ < unorm_candidates_.size()) {
          unorm_candidates_[unorm_candidate_count_].resource =
              unorm_resource.Get();
          ++unorm_candidate_count_;
          published_unorm_candidate_count_.store(
              unorm_candidate_count_, std::memory_order_release);
        }
      }
    }
  }
  if (resource == nullptr || description->Format != kCandidateColorFormat ||
      (description->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) == 0 ||
      (description->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0) {
    return;
  }
  candidate_color_resource_count_.fetch_add(1, std::memory_order_relaxed);
  std::lock_guard lock(mutex_);
  if (!candidate_color_resource_) {
    resource->QueryInterface(IID_PPV_ARGS(&candidate_color_resource_));
  }
}

void GraphicsObserver::recordRenderTargetView(
    ID3D12Resource* resource,
    D3D12_CPU_DESCRIPTOR_HANDLE destination) {
  if (resource == nullptr) {
    return;
  }
  std::lock_guard lock(mutex_);
  if (candidate_color_resource_.Get() == resource) {
    candidate_color_rtv_count_.fetch_add(1, std::memory_order_relaxed);
  }
  for (std::size_t index = 0; index < unorm_candidate_count_; ++index) {
    UnormCandidate& candidate = unorm_candidates_[index];
    if (candidate.resource != resource) {
      continue;
    }
    if (candidate.rtv_count == 0) {
      candidate_unorm_rtv_resource_count_.fetch_add(
          1, std::memory_order_relaxed);
    }
    if (candidate.name[0] == '\0') {
      wchar_t wide_name[64]{};
      UINT name_bytes = sizeof(wide_name);
      if (SUCCEEDED(resource->GetPrivateData(
              WKPDID_D3DDebugObjectNameW, &name_bytes, wide_name))) {
        for (std::size_t character = 0;
             character + 1 < std::size(candidate.name) &&
             wide_name[character] != L'\0';
             ++character) {
          candidate.name[character] =
              wide_name[character] >= 0x20 && wide_name[character] <= 0x7e
                  ? static_cast<char>(wide_name[character])
                  : '?';
        }
      } else {
        name_bytes = static_cast<UINT>(sizeof(candidate.name));
        resource->GetPrivateData(
            WKPDID_D3DDebugObjectName, &name_bytes, candidate.name);
        candidate.name[std::size(candidate.name) - 1] = '\0';
      }
    }
    ++candidate.rtv_count;
    candidate_unorm_rtv_count_.fetch_add(1, std::memory_order_relaxed);
    const std::size_t mapping_count =
        unorm_rtv_mapping_count_.load(std::memory_order_relaxed);
    if (destination.ptr != 0 && mapping_count < unorm_rtv_mappings_.size()) {
      unorm_rtv_mappings_[mapping_count] = {destination.ptr, index};
      unorm_rtv_mapping_count_.store(
          mapping_count + 1, std::memory_order_release);
    }
    break;
  }
}

void GraphicsObserver::recordRenderTargetBindings(
    UINT count,
    const D3D12_CPU_DESCRIPTOR_HANDLE* descriptors,
    BOOL single_range) {
  if (count == 0 || descriptors == nullptr) {
    return;
  }
  const UINT stride = rtv_descriptor_stride_.load(std::memory_order_acquire);
  for (UINT descriptor_index = 0; descriptor_index < count;
       ++descriptor_index) {
    SIZE_T descriptor = descriptors[single_range ? 0 : descriptor_index].ptr;
    if (single_range) {
      descriptor += static_cast<SIZE_T>(descriptor_index) * stride;
    }
    std::size_t candidate_index = 0;
    if (!findCandidateForDescriptor(descriptor, &candidate_index)) {
      continue;
    }
    const std::uint64_t previous =
        unorm_bind_counts_[candidate_index].fetch_add(
            1, std::memory_order_relaxed);
    if (previous == 0) {
      candidate_unorm_bound_resource_count_.fetch_add(
          1, std::memory_order_relaxed);
    }
    candidate_unorm_bind_count_.fetch_add(1, std::memory_order_relaxed);
  }
}

bool GraphicsObserver::findCandidateForDescriptor(
    SIZE_T descriptor,
    std::size_t* candidate_index) const {
  if (descriptor == 0 || candidate_index == nullptr) {
    return false;
  }
  const std::size_t mapping_count =
      unorm_rtv_mapping_count_.load(std::memory_order_acquire);
  // Search newest-first so descriptor reuse resolves to the latest RTV
  // definition without adding a lock to command-list hot paths.
  for (std::size_t mapping_index = mapping_count; mapping_index > 0;
       --mapping_index) {
    const RtvMapping& mapping = unorm_rtv_mappings_[mapping_index - 1];
    if (mapping.descriptor == descriptor &&
        mapping.candidate_index < unorm_candidates_.size()) {
      *candidate_index = mapping.candidate_index;
      return true;
    }
  }
  return false;
}

void GraphicsObserver::recordRenderTargetClear(
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor) {
  std::size_t candidate_index = 0;
  if (!findCandidateForDescriptor(descriptor.ptr, &candidate_index)) {
    return;
  }
  const std::uint64_t previous =
      unorm_clear_counts_[candidate_index].fetch_add(
          1, std::memory_order_relaxed);
  if (previous == 0) {
    candidate_unorm_cleared_resource_count_.fetch_add(
        1, std::memory_order_relaxed);
  }
  candidate_unorm_clear_count_.fetch_add(1, std::memory_order_relaxed);
}

void GraphicsObserver::recordResourceBarriers(
    ID3D12GraphicsCommandList* command_list,
    UINT count,
    const D3D12_RESOURCE_BARRIER* barriers) {
  if (count == 0 || barriers == nullptr) {
    return;
  }
  const std::size_t candidate_count =
      published_unorm_candidate_count_.load(std::memory_order_acquire);
  for (UINT barrier_index = 0; barrier_index < count; ++barrier_index) {
    const D3D12_RESOURCE_BARRIER& barrier = barriers[barrier_index];
    if (barrier.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) {
      continue;
    }
    for (std::size_t candidate_index = 0;
         candidate_index < candidate_count; ++candidate_index) {
      if (unorm_candidates_[candidate_index].resource !=
          barrier.Transition.pResource) {
        continue;
      }
      unorm_last_states_[candidate_index].store(
          static_cast<std::uint32_t>(barrier.Transition.StateAfter),
          std::memory_order_relaxed);
      if ((barrier.Transition.StateBefore &
           D3D12_RESOURCE_STATE_RENDER_TARGET) == 0 ||
          (barrier.Transition.StateAfter &
           (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)) == 0) {
        break;
      }
      const std::uint64_t previous =
          unorm_shader_read_transition_counts_[candidate_index].fetch_add(
              1, std::memory_order_relaxed);
      if (previous == 0) {
        candidate_unorm_shader_read_resource_count_.fetch_add(
            1, std::memory_order_relaxed);
      }
      candidate_unorm_shader_read_transition_count_.fetch_add(
          1, std::memory_order_relaxed);
      updateVrcamSelection(
          candidate_index, barrier.Transition.pResource);
      markCommandListCandidate(command_list, candidate_index);
      vrclient::adapters::unreal::D3D12SceneRelayRenderer* relay = nullptr;
      {
        std::lock_guard lock(mutex_);
        if (selected_vrcam_candidate_index_ == candidate_index &&
            selected_vrcam_resource_.Get() == barrier.Transition.pResource) {
          relay = vrcam_relay_.get();
        }
      }
      if (relay != nullptr &&
          vrcam_capture_requested_.exchange(
              false, std::memory_order_acq_rel)) {
        const VrRuntimeResult capture_result = relay->recordInlineCapture(
            command_list,
            barrier.Transition.pResource,
            barrier.Transition.StateAfter);
        if (capture_result != VR_RUNTIME_OK) {
          vrcam_capture_fail_count_.fetch_add(1, std::memory_order_relaxed);
          vrcam_capture_requested_.store(true, std::memory_order_release);
        }
      }
      break;
    }
  }
}

void GraphicsObserver::markCommandListCandidate(
    ID3D12CommandList* command_list,
    std::size_t candidate_index) {
  if (command_list == nullptr || candidate_index >= 32) {
    return;
  }
  const std::uint32_t bit = 1u << candidate_index;
  std::lock_guard lock(mutex_);
  for (std::size_t index = 0; index < command_list_usage_count_; ++index) {
    if (command_list_usages_[index].command_list == command_list) {
      command_list_usages_[index].candidate_mask |= bit;
      return;
    }
  }
  if (command_list_usage_count_ < command_list_usages_.size()) {
    command_list_usages_[command_list_usage_count_++] = {command_list, bit};
  }
}

void GraphicsObserver::recordExecuteCommandLists(
    ID3D12CommandQueue* queue,
    UINT count,
    ID3D12CommandList* const* command_lists) {
  if (queue == nullptr || count == 0 || command_lists == nullptr) {
    return;
  }
  const D3D12_COMMAND_LIST_TYPE type = queue->GetDesc().Type;
  std::uint32_t candidate_mask = 0;
  bool primary_queue = false;
  {
    std::lock_guard lock(mutex_);
    primary_queue = queue_.Get() == queue;
    for (UINT list_index = 0; list_index < count; ++list_index) {
      for (std::size_t usage_index = 0;
           usage_index < command_list_usage_count_; ++usage_index) {
        if (command_list_usages_[usage_index].command_list ==
            command_lists[list_index]) {
          candidate_mask |= command_list_usages_[usage_index].candidate_mask;
          break;
        }
      }
    }
  }
  for (std::size_t candidate_index = 0;
       candidate_index < kMaxTrackedUnormCandidates; ++candidate_index) {
    if ((candidate_mask & (1u << candidate_index)) == 0) {
      continue;
    }
    if (type == D3D12_COMMAND_LIST_TYPE_DIRECT && primary_queue) {
      unorm_primary_queue_execute_counts_[candidate_index].fetch_add(
          1, std::memory_order_relaxed);
    } else if (type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
      unorm_other_direct_queue_execute_counts_[candidate_index].fetch_add(
          1, std::memory_order_relaxed);
    } else if (type == D3D12_COMMAND_LIST_TYPE_COMPUTE) {
      unorm_compute_queue_execute_counts_[candidate_index].fetch_add(
          1, std::memory_order_relaxed);
    } else if (type == D3D12_COMMAND_LIST_TYPE_COPY) {
      unorm_copy_queue_execute_counts_[candidate_index].fetch_add(
          1, std::memory_order_relaxed);
    }
  }
  vrclient::adapters::unreal::D3D12SceneRelayRenderer* relay = nullptr;
  {
    std::lock_guard lock(mutex_);
    relay = vrcam_relay_.get();
  }
  if (relay != nullptr) {
    for (UINT list_index = 0; list_index < count; ++list_index) {
      if (relay->publishInlineCapture(command_lists[list_index])) {
        vrcam_capture_count_.fetch_add(1, std::memory_order_relaxed);
      }
    }
  }
}

void GraphicsObserver::updateVrcamSelection(
    std::size_t current_candidate_index,
    ID3D12Resource* current_resource) {
  if (current_resource == nullptr) {
    return;
  }
  const std::size_t candidate_count =
      published_unorm_candidate_count_.load(std::memory_order_acquire);
  std::array<VrcamCandidateScore, kMaxTrackedUnormCandidates> scores{};
  for (std::size_t index = 0; index < candidate_count; ++index) {
    scores[index].shader_read_transitions =
        unorm_shader_read_transition_counts_[index].load(
            std::memory_order_relaxed);
    scores[index].binds =
        unorm_bind_counts_[index].load(std::memory_order_relaxed);
    scores[index].clears =
        unorm_clear_counts_[index].load(std::memory_order_relaxed);
  }
  const std::size_t best_index = selectVrcamCandidate(
      std::span<const VrcamCandidateScore>(scores.data(), candidate_count));
  if (best_index >= candidate_count || best_index != current_candidate_index) {
    return;
  }
  std::lock_guard lock(mutex_);
  if (selected_vrcam_candidate_index_ != best_index) {
    selected_vrcam_resource_ = current_resource;
    selected_vrcam_candidate_index_ = static_cast<std::uint32_t>(best_index);
  }
}

void GraphicsObserver::recordCopySource(ID3D12Resource* resource) {
  if (resource == nullptr) {
    return;
  }
  const std::size_t candidate_count =
      published_unorm_candidate_count_.load(std::memory_order_acquire);
  for (std::size_t candidate_index = 0;
       candidate_index < candidate_count; ++candidate_index) {
    if (unorm_candidates_[candidate_index].resource != resource) {
      continue;
    }
    const std::uint64_t previous =
        unorm_copy_source_counts_[candidate_index].fetch_add(
            1, std::memory_order_relaxed);
    if (previous == 0) {
      candidate_unorm_copy_source_resource_count_.fetch_add(
          1, std::memory_order_relaxed);
    }
    candidate_unorm_copy_source_count_.fetch_add(
        1, std::memory_order_relaxed);
    break;
  }
}

void GraphicsObserver::query(VrRedengineGraphicsSnapshot* snapshot) const {
  if (snapshot == nullptr) {
    return;
  }
  const std::uint32_t caller_size = snapshot->size;
  VrRedengineGraphicsSnapshot result{};
  result.size = sizeof(result);
  result.version = VRCLIENT_REDENGINE_GRAPHICS_SERVICE_VERSION;
  result.renderer = VRCLIENT_REDENGINE_RENDERER_D3D12;
  std::uint32_t selected_vrcam_candidate_index = UINT32_MAX;
  {
    std::lock_guard lock(mutex_);
    result.device = device_.Get();
    result.queue = queue_.Get();
    result.swapchain = swapchain_.Get();
    result.candidate_color_resource = candidate_color_resource_.Get();
    result.vrcam_color_resource = selected_vrcam_resource_.Get();
    selected_vrcam_candidate_index = selected_vrcam_candidate_index_;
    result.width = description_.Width;
    result.height = description_.Height;
    result.format = static_cast<std::uint32_t>(description_.Format);
    result.buffer_count = description_.BufferCount;
    result.ready = device_ && queue_ && swapchain_ ? 1u : 0u;
  }
  result.vrcam_color_format =
      static_cast<std::uint32_t>(DXGI_FORMAT_R8G8B8A8_UNORM);
  result.vrcam_candidate_index = selected_vrcam_candidate_index;
  result.vrcam_ready = result.vrcam_color_resource != nullptr &&
          selected_vrcam_candidate_index < unorm_bind_counts_.size()
      ? 1u
      : 0u;
  if (result.vrcam_ready) {
    result.vrcam_bind_count =
        unorm_bind_counts_[selected_vrcam_candidate_index].load(
            std::memory_order_relaxed);
    result.vrcam_shader_read_transition_count =
        unorm_shader_read_transition_counts_[selected_vrcam_candidate_index]
            .load(std::memory_order_relaxed);
    result.vrcam_source_state =
        unorm_last_states_[selected_vrcam_candidate_index].load(
            std::memory_order_relaxed);
  }
  result.vrcam_capture_count =
      vrcam_capture_count_.load(std::memory_order_relaxed);
  result.vrcam_capture_fail_count =
      vrcam_capture_fail_count_.load(std::memory_order_relaxed);
  result.vrcam_capture_ready = result.vrcam_capture_count > 0 ? 1u : 0u;
  result.present_count = present_count_.load(std::memory_order_relaxed);
  result.resize_count = resize_count_.load(std::memory_order_relaxed);
  result.candidate_view_width = kCandidateViewWidth;
  result.candidate_view_height = kCandidateViewHeight;
  result.candidate_view_resource_count =
      candidate_view_resource_count_.load(std::memory_order_relaxed);
  result.candidate_color_format =
      static_cast<std::uint32_t>(kCandidateColorFormat);
  result.candidate_color_resource_count =
      candidate_color_resource_count_.load(std::memory_order_relaxed);
  result.candidate_color_rtv_count =
      candidate_color_rtv_count_.load(std::memory_order_relaxed);
  result.candidate_unorm_resource_count =
      candidate_unorm_resource_count_.load(std::memory_order_relaxed);
  result.candidate_unorm_rtv_resource_count =
      candidate_unorm_rtv_resource_count_.load(std::memory_order_relaxed);
  result.candidate_unorm_rtv_count =
      candidate_unorm_rtv_count_.load(std::memory_order_relaxed);
  result.candidate_unorm_bound_resource_count =
      candidate_unorm_bound_resource_count_.load(std::memory_order_relaxed);
  result.candidate_unorm_bind_count =
      candidate_unorm_bind_count_.load(std::memory_order_relaxed);
  result.candidate_unorm_cleared_resource_count =
      candidate_unorm_cleared_resource_count_.load(
          std::memory_order_relaxed);
  result.candidate_unorm_clear_count =
      candidate_unorm_clear_count_.load(std::memory_order_relaxed);
  result.candidate_unorm_shader_read_resource_count =
      candidate_unorm_shader_read_resource_count_.load(
          std::memory_order_relaxed);
  result.candidate_unorm_shader_read_transition_count =
      candidate_unorm_shader_read_transition_count_.load(
          std::memory_order_relaxed);
  result.candidate_unorm_copy_source_resource_count =
      candidate_unorm_copy_source_resource_count_.load(
          std::memory_order_relaxed);
  result.candidate_unorm_copy_source_count =
      candidate_unorm_copy_source_count_.load(std::memory_order_relaxed);
  {
    std::lock_guard lock(mutex_);
    result.candidate_unorm_tracked_count = static_cast<std::uint32_t>(
        (std::min)(unorm_candidate_count_, unorm_bind_counts_.size()));
    for (std::size_t index = 0;
         index < result.candidate_unorm_tracked_count; ++index) {
      std::memcpy(
          result.candidate_unorm_names[index],
          unorm_candidates_[index].name,
          sizeof(result.candidate_unorm_names[index]));
    }
  }
  for (std::size_t index = 0;
       index < result.candidate_unorm_tracked_count; ++index) {
    result.candidate_unorm_bind_counts[index] =
        unorm_bind_counts_[index].load(std::memory_order_relaxed);
    result.candidate_unorm_clear_counts[index] =
        unorm_clear_counts_[index].load(std::memory_order_relaxed);
    result.candidate_unorm_shader_read_transition_counts[index] =
        unorm_shader_read_transition_counts_[index].load(
            std::memory_order_relaxed);
    result.candidate_unorm_copy_source_counts[index] =
        unorm_copy_source_counts_[index].load(std::memory_order_relaxed);
    result.candidate_unorm_primary_queue_execute_counts[index] =
        unorm_primary_queue_execute_counts_[index].load(
            std::memory_order_relaxed);
    result.candidate_unorm_other_direct_queue_execute_counts[index] =
        unorm_other_direct_queue_execute_counts_[index].load(
            std::memory_order_relaxed);
    result.candidate_unorm_compute_queue_execute_counts[index] =
        unorm_compute_queue_execute_counts_[index].load(
            std::memory_order_relaxed);
    result.candidate_unorm_copy_queue_execute_counts[index] =
        unorm_copy_queue_execute_counts_[index].load(
            std::memory_order_relaxed);
  }
  std::memcpy(snapshot, &result, (std::min)(caller_size, result.size));
}

void GraphicsObserver::reset() {
  observed_swapchain_.store(nullptr, std::memory_order_release);
  std::lock_guard lock(mutex_);
  swapchain_.Reset();
  candidate_color_resource_.Reset();
  selected_vrcam_resource_.Reset();
  selected_vrcam_candidate_index_ = UINT32_MAX;
  vrcam_relay_.reset();
  vrcam_capture_requested_.store(true, std::memory_order_relaxed);
  vrcam_capture_count_.store(0, std::memory_order_relaxed);
  vrcam_capture_fail_count_.store(0, std::memory_order_relaxed);
  queue_.Reset();
  device_.Reset();
  description_ = {};
  present_count_.store(0, std::memory_order_relaxed);
  resize_count_.store(0, std::memory_order_relaxed);
  candidate_view_resource_count_.store(0, std::memory_order_relaxed);
  candidate_color_resource_count_.store(0, std::memory_order_relaxed);
  candidate_color_rtv_count_.store(0, std::memory_order_relaxed);
  candidate_unorm_resource_count_.store(0, std::memory_order_relaxed);
  unorm_candidates_ = {};
  unorm_candidate_count_ = 0;
  published_unorm_candidate_count_.store(0, std::memory_order_release);
  unorm_rtv_mappings_ = {};
  command_list_usages_ = {};
  command_list_usage_count_ = 0;
  unorm_rtv_mapping_count_.store(0, std::memory_order_release);
  rtv_descriptor_stride_.store(0, std::memory_order_release);
  for (auto& bind_count : unorm_bind_counts_) {
    bind_count.store(0, std::memory_order_relaxed);
  }
  for (auto& clear_count : unorm_clear_counts_) {
    clear_count.store(0, std::memory_order_relaxed);
  }
  for (auto& transition_count : unorm_shader_read_transition_counts_) {
    transition_count.store(0, std::memory_order_relaxed);
  }
  for (auto& copy_count : unorm_copy_source_counts_) {
    copy_count.store(0, std::memory_order_relaxed);
  }
  for (auto& state : unorm_last_states_) {
    state.store(0, std::memory_order_relaxed);
  }
  for (auto* counts : {&unorm_primary_queue_execute_counts_,
                       &unorm_other_direct_queue_execute_counts_,
                       &unorm_compute_queue_execute_counts_,
                       &unorm_copy_queue_execute_counts_}) {
    for (auto& count : *counts) {
      count.store(0, std::memory_order_relaxed);
    }
  }
  candidate_unorm_rtv_resource_count_.store(0, std::memory_order_relaxed);
  candidate_unorm_rtv_count_.store(0, std::memory_order_relaxed);
  candidate_unorm_bound_resource_count_.store(0, std::memory_order_relaxed);
  candidate_unorm_bind_count_.store(0, std::memory_order_relaxed);
  candidate_unorm_cleared_resource_count_.store(0, std::memory_order_relaxed);
  candidate_unorm_clear_count_.store(0, std::memory_order_relaxed);
  candidate_unorm_shader_read_resource_count_.store(
      0, std::memory_order_relaxed);
  candidate_unorm_shader_read_transition_count_.store(
      0, std::memory_order_relaxed);
  candidate_unorm_copy_source_resource_count_.store(
      0, std::memory_order_relaxed);
  candidate_unorm_copy_source_count_.store(0, std::memory_order_relaxed);
}

}  // namespace vrclient::adapters::redengine

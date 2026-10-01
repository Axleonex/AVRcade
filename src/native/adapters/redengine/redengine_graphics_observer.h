#pragma once

#include "plugins/sdk/redengine_services.h"
#include "public/vr_runtime_api.h"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <memory>
#include <vector>

namespace vrclient::adapters::unreal {
class D3D12SceneRelayRenderer;
}

namespace vrclient::adapters::redengine {

class GraphicsObserver {
 public:
  GraphicsObserver();
  ~GraphicsObserver();
  bool observe(IUnknown* device_or_queue, IUnknown* swapchain);
  void recordPresent(IDXGISwapChain* swapchain);
  void recordResize(IDXGISwapChain* swapchain);
  void recordResource(
      const D3D12_RESOURCE_DESC* description,
      IUnknown* resource);
  void recordRenderTargetView(
      ID3D12Resource* resource,
      D3D12_CPU_DESCRIPTOR_HANDLE destination);
  void recordRenderTargetBindings(
      UINT count,
      const D3D12_CPU_DESCRIPTOR_HANDLE* descriptors,
      BOOL single_range);
  void recordRenderTargetClear(D3D12_CPU_DESCRIPTOR_HANDLE descriptor);
  void recordResourceBarriers(
      ID3D12GraphicsCommandList* command_list,
      UINT count,
      const D3D12_RESOURCE_BARRIER* barriers);
  void recordExecuteCommandLists(
      ID3D12CommandQueue* queue,
      UINT count,
      ID3D12CommandList* const* command_lists);
  void recordCopySource(ID3D12Resource* resource);
  void requestVrcamCapture();
  bool makeD3D12Binding(VrRuntimeD3D12Binding* binding) const;
  VrRuntimeResult renderCapturedVrcamFrame(
      const VrRuntimeFrameData& frame,
      const VrRuntimeRenderTarget* targets,
      std::uint32_t target_count);
  VrRuntimeResult readbackCapturedVrcam(
      std::vector<std::uint8_t>* pixels,
      std::uint32_t* width,
      std::uint32_t* height);
  void query(VrRedengineGraphicsSnapshot* snapshot) const;
  void reset();

 private:
  struct UnormCandidate {
    ID3D12Resource* resource = nullptr;
    std::uint64_t rtv_count = 0;
    char name[64]{};
  };

  struct RtvMapping {
    SIZE_T descriptor = 0;
    std::size_t candidate_index = 0;
  };

  struct CommandListUsage {
    ID3D12CommandList* command_list = nullptr;
    std::uint32_t candidate_mask = 0;
  };

  static constexpr std::size_t kMaxTrackedUnormCandidates = 32;
  static constexpr std::size_t kMaxTrackedUnormRtvs = 64;
  static constexpr std::size_t kMaxTrackedCommandLists = 128;
  bool findCandidateForDescriptor(
      SIZE_T descriptor,
      std::size_t* candidate_index) const;
  void updateVrcamSelection(
      std::size_t current_candidate_index,
      ID3D12Resource* current_resource);
  void markCommandListCandidate(
      ID3D12CommandList* command_list,
      std::size_t candidate_index);
  mutable std::mutex mutex_;
  Microsoft::WRL::ComPtr<ID3D12Device> device_;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
  Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain_;
  Microsoft::WRL::ComPtr<ID3D12Resource> candidate_color_resource_;
  Microsoft::WRL::ComPtr<ID3D12Resource> selected_vrcam_resource_;
  std::uint32_t selected_vrcam_candidate_index_ = UINT32_MAX;
  DXGI_SWAP_CHAIN_DESC1 description_{};
  std::array<UnormCandidate, kMaxTrackedUnormCandidates> unorm_candidates_{};
  std::array<RtvMapping, kMaxTrackedUnormRtvs> unorm_rtv_mappings_{};
  std::array<CommandListUsage, kMaxTrackedCommandLists>
      command_list_usages_{};
  std::size_t command_list_usage_count_ = 0;
  std::array<std::atomic<std::uint64_t>, kMaxTrackedUnormCandidates>
      unorm_bind_counts_{};
  std::array<std::atomic<std::uint64_t>, kMaxTrackedUnormCandidates>
      unorm_clear_counts_{};
  std::array<std::atomic<std::uint64_t>, kMaxTrackedUnormCandidates>
      unorm_shader_read_transition_counts_{};
  std::array<std::atomic<std::uint64_t>, kMaxTrackedUnormCandidates>
      unorm_copy_source_counts_{};
  std::array<std::atomic<std::uint32_t>, kMaxTrackedUnormCandidates>
      unorm_last_states_{};
  std::array<std::atomic<std::uint64_t>, kMaxTrackedUnormCandidates>
      unorm_primary_queue_execute_counts_{};
  std::array<std::atomic<std::uint64_t>, kMaxTrackedUnormCandidates>
      unorm_other_direct_queue_execute_counts_{};
  std::array<std::atomic<std::uint64_t>, kMaxTrackedUnormCandidates>
      unorm_compute_queue_execute_counts_{};
  std::array<std::atomic<std::uint64_t>, kMaxTrackedUnormCandidates>
      unorm_copy_queue_execute_counts_{};
  std::size_t unorm_candidate_count_ = 0;
  std::atomic<std::size_t> published_unorm_candidate_count_{0};
  std::atomic<std::size_t> unorm_rtv_mapping_count_{0};
  std::atomic<UINT> rtv_descriptor_stride_{0};
  std::atomic<IDXGISwapChain*> observed_swapchain_{nullptr};
  std::atomic<std::uint64_t> present_count_{0};
  std::atomic<std::uint64_t> resize_count_{0};
  std::atomic<std::uint64_t> candidate_view_resource_count_{0};
  std::atomic<std::uint64_t> candidate_color_resource_count_{0};
  std::atomic<std::uint64_t> candidate_color_rtv_count_{0};
  std::atomic<std::uint64_t> candidate_unorm_resource_count_{0};
  std::atomic<std::uint64_t> candidate_unorm_rtv_resource_count_{0};
  std::atomic<std::uint64_t> candidate_unorm_rtv_count_{0};
  std::atomic<std::uint64_t> candidate_unorm_bound_resource_count_{0};
  std::atomic<std::uint64_t> candidate_unorm_bind_count_{0};
  std::atomic<std::uint64_t> candidate_unorm_cleared_resource_count_{0};
  std::atomic<std::uint64_t> candidate_unorm_clear_count_{0};
  std::atomic<std::uint64_t> candidate_unorm_shader_read_resource_count_{0};
  std::atomic<std::uint64_t>
      candidate_unorm_shader_read_transition_count_{0};
  std::atomic<std::uint64_t> candidate_unorm_copy_source_resource_count_{0};
  std::atomic<std::uint64_t> candidate_unorm_copy_source_count_{0};
  std::unique_ptr<vrclient::adapters::unreal::D3D12SceneRelayRenderer>
      vrcam_relay_;
  std::atomic<bool> vrcam_capture_requested_{true};
  std::atomic<std::uint64_t> vrcam_capture_count_{0};
  std::atomic<std::uint64_t> vrcam_capture_fail_count_{0};
};

}  // namespace vrclient::adapters::redengine

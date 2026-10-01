#pragma once

#include "diagnostics/diagnostics_system.h"
#include "frame/frame_loop.h"
#include "frame/test_scene_renderer.h"
#include "perf/dynamic_resolution.h"
#include "perf/foveation.h"
#include "perf/frame_pacing.h"
#include "perf/runtime_profile.h"
#include "public/vr_runtime_api.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// <vulkan/vulkan.h> MUST precede <openxr/openxr_platform.h>: with
// XR_USE_GRAPHICS_API_VULKAN the platform header references Vk* types. Under
// XR_USE_GRAPHICS_API_D3D12 it references D3D12 types. Include both graphics
// headers and Win32 COM definitions before the platform header.
#if defined(XR_USE_PLATFORM_WIN32)
#  include <unknwn.h>
#endif
#include <d3d12.h>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "openxr/d3d12_device.h"
#include "openxr/vulkan_device.h"

namespace vrclient::runtime::openxr {

class OpenXrRuntime {
 public:
  explicit OpenXrRuntime(const VrRuntimeDesc& desc);
  ~OpenXrRuntime();

  OpenXrRuntime(const OpenXrRuntime&) = delete;
  OpenXrRuntime& operator=(const OpenXrRuntime&) = delete;

  VrRuntimeResult start();
  VrRuntimeResult pollEvents();
  VrRuntimeResult runFrame(VrRuntimeRenderCallback callback, void* user_data);
  VrRuntimeResult sampleHeadPose(VrRuntimePose* pose);
  VrRuntimeResult stop();

  // Snapshot accessors are safe from any thread: they copy the published
  // structs under data_mutex_. Everything else on this class must be
  // externally serialized on one thread.
  VrRuntimeState state() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return published_headset_state_.runtime_state;
  }
  VrRuntimeHeadsetState headsetState() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return published_headset_state_;
  }
  VrRuntimeFrameData frameData() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return published_frame_;
  }

 private:
  struct EyeSwapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    int32_t width = 0;
    int32_t height = 0;
    int64_t format = 0;
    std::vector<XrSwapchainImageVulkanKHR> vulkan_images;
    std::vector<XrSwapchainImageD3D12KHR> d3d12_images;
  };

  VrRuntimeResult createInstance();
  VrRuntimeResult createSystem();
  VrRuntimeResult createGraphicsDevice();
  VrRuntimeResult createSession();
  VrRuntimeResult createSpaces();
  VrRuntimeResult createSwapchains();
  VrRuntimeResult beginSession();
  VrRuntimeResult endSession();
  VrRuntimeResult renderFrame(
      const XrFrameState& frame_state,
      VrRuntimeRenderCallback callback,
      void* user_data,
      int32_t* out_detail_code);
  VrRuntimeResult updateFrameData(
      XrTime predicted_display_time,
      XrDuration predicted_display_period,
      uint32_t* out_view_count,
      int32_t* out_detail_code);
  VrRuntimeResult endEmptyFrame(XrTime display_time, int32_t* out_detail_code);
  void destroyOpenXrObjects();
  void publishSnapshots();
  void setState(VrRuntimeState state, int32_t detail_code);
  VrRuntimeResult fail(VrRuntimeResult result, int32_t detail_code);

  std::string application_name_;
  std::string profile_path_;
  VrRuntimeGraphicsBackend graphics_backend_ = VR_RUNTIME_GRAPHICS_BACKEND_VULKAN;
  Microsoft::WRL::ComPtr<ID3D12Device> d3d12_borrowed_device_;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> d3d12_borrowed_queue_;
  D3D12BorrowedBinding d3d12_borrowed_binding_{};
  bool has_d3d12_borrowed_binding_ = false;
  VrRuntimeStateCallback state_callback_ = nullptr;
  void* state_callback_user_data_ = nullptr;
  vrclient::diagnostics::DiagnosticsSystem diagnostics_;

  perf::RuntimeProfile profile_;
  perf::DynamicResolutionController dynamic_resolution_;
  perf::FoveationSettings foveation_;
  perf::FramePacingGuardrails pacing_;
  frame::LifecycleStateMachine lifecycle_;

  XrInstance instance_ = XR_NULL_HANDLE;
  XrSystemId system_id_ = XR_NULL_SYSTEM_ID;
  XrSession session_ = XR_NULL_HANDLE;
  XrSpace local_space_ = XR_NULL_HANDLE;
  XrSpace view_space_ = XR_NULL_HANDLE;
  bool session_begun_ = false;
  bool exit_requested_ = false;
  bool has_win32_time_conversion_ = false;
  PFN_xrConvertWin32PerformanceCounterToTimeKHR convert_qpc_to_time_ = nullptr;

  VulkanDeviceContext vulkan_graphics_;
  D3D12DeviceContext d3d12_graphics_;
  frame::VulkanTestSceneRenderer test_renderer_;

  std::array<XrViewConfigurationView, 2> view_configurations_{};
  std::array<XrView, 2> views_{};
  std::array<EyeSwapchain, 2> swapchains_{};

  // Working structs, mutated field-by-field on the run-frame thread only.
  VrRuntimeHeadsetState headset_state_{};
  VrRuntimeFrameData current_frame_{};
  // Cross-thread copies for the get_* entry points. publishSnapshots() assigns
  // the working structs to these under data_mutex_; the lock is never held
  // across an XR or graphics call.
  mutable std::mutex data_mutex_;
  VrRuntimeHeadsetState published_headset_state_{};
  VrRuntimeFrameData published_frame_{};
  uint64_t frame_index_ = 0;
  VrRuntimeResult last_result_ = VR_RUNTIME_OK;
};

}  // namespace vrclient::runtime::openxr

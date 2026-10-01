#include "adapters/redengine/redengine_openxr_bridge.h"

#include "adapters/redengine/redengine_graphics_hooks.h"

#include <Windows.h>

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <thread>

namespace vrclient::adapters::redengine {
namespace {

std::mutex worker_mutex;
std::thread worker;
std::atomic<bool> stop_requested{false};
std::atomic<bool> worker_started{false};
std::atomic<bool> session_started{false};
std::atomic<int> start_result{VR_RUNTIME_SKIPPED};
std::atomic<int> frame_result{VR_RUNTIME_SKIPPED};
std::atomic<std::uint64_t> frame_count{0};
std::atomic<std::uint32_t> eye_count{0};
std::atomic<bool> orientation_valid{false};
std::atomic<bool> position_valid{false};

std::string runtimeProfilePath() {
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCWSTR>(&startRedengineOpenXrBridge),
          &module)) {
    return {};
  }
  wchar_t path[32768]{};
  const DWORD length = GetModuleFileNameW(module, path, std::size(path));
  if (length == 0 || length >= std::size(path)) {
    return {};
  }
  wchar_t* separator = wcsrchr(path, L'\\');
  if (separator == nullptr) {
    return {};
  }
  wcscpy_s(
      separator + 1,
      std::size(path) - static_cast<std::size_t>(separator + 1 - path),
      L"runtime-profile.json");
  const int needed = WideCharToMultiByte(
      CP_ACP, WC_NO_BEST_FIT_CHARS, path, -1, nullptr, 0, nullptr, nullptr);
  if (needed <= 1) {
    return {};
  }
  std::string narrow(static_cast<std::size_t>(needed), '\0');
  if (WideCharToMultiByte(
          CP_ACP, WC_NO_BEST_FIT_CHARS, path, -1, narrow.data(), needed,
          nullptr, nullptr) <= 0) {
    return {};
  }
  narrow.resize(static_cast<std::size_t>(needed) - 1);
  return narrow;
}

VrRuntimeResult renderFrame(
    void*,
    const VrRuntimeFrameData* frame,
    const VrRuntimeRenderTarget* targets,
    std::uint32_t target_count) {
  if (frame == nullptr) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }
  const VrRuntimeResult result =
      renderCapturedVrcamFrame(*frame, targets, target_count);
  if (result == VR_RUNTIME_OK) {
    frame_count.fetch_add(1, std::memory_order_relaxed);
    eye_count.store(target_count, std::memory_order_relaxed);
    orientation_valid.store(
        frame->head_pose.orientation_valid != 0, std::memory_order_relaxed);
    position_valid.store(
        frame->head_pose.position_valid != 0, std::memory_order_relaxed);
    requestVrcamCapture();
  }
  return result;
}

void runBridge() {
  worker_started.store(true, std::memory_order_release);
  VrRedengineGraphicsSnapshot snapshot{};
  constexpr DWORD kWaitSliceMs = 20;
  while (!stop_requested.load(std::memory_order_acquire)) {
    snapshot = {};
    snapshot.size = sizeof(snapshot);
    queryGraphicsObservation(&snapshot);
    if (snapshot.ready != 0 && snapshot.vrcam_capture_ready != 0) {
      break;
    }
    Sleep(kWaitSliceMs);
  }
  if (stop_requested.load(std::memory_order_acquire)) {
    return;
  }

  VrRuntimeD3D12Binding binding{};
  const std::string profile = runtimeProfilePath();
  if (!makeD3D12Binding(&binding) || profile.empty()) {
    start_result.store(VR_RUNTIME_ERROR_PROFILE, std::memory_order_release);
    return;
  }
  VrRuntimeDesc desc{};
  desc.size = sizeof(desc);
  desc.application_name = "VRClient Cyberpunk 2077 VRCAM Relay";
  desc.runtime_profile_path = profile.c_str();
  desc.preferred_graphics_backend = VR_RUNTIME_GRAPHICS_BACKEND_D3D12;
  desc.d3d12_binding = &binding;

  VrRuntime* runtime = nullptr;
  VrRuntimeResult result = vr_runtime_create(&desc, &runtime);
  if (result == VR_RUNTIME_OK) {
    result = vr_runtime_start(runtime);
  }
  start_result.store(result, std::memory_order_release);
  session_started.store(result == VR_RUNTIME_OK, std::memory_order_release);
  while (result == VR_RUNTIME_OK &&
         !stop_requested.load(std::memory_order_acquire)) {
    result = vr_runtime_run_frame(runtime, &renderFrame, nullptr);
    frame_result.store(result, std::memory_order_release);
    if (result != VR_RUNTIME_OK && result != VR_RUNTIME_SKIPPED) {
      break;
    }
    const VrRuntimeState state = vr_runtime_get_state(runtime);
    if (state == VR_RUNTIME_STATE_EXITING ||
        state == VR_RUNTIME_STATE_LOSS_PENDING ||
        state == VR_RUNTIME_STATE_ERROR ||
        state == VR_RUNTIME_STATE_STOPPED) {
      break;
    }
    if (result == VR_RUNTIME_SKIPPED) {
      Sleep(10);
      result = VR_RUNTIME_OK;
    }
  }
  if (runtime != nullptr) {
    vr_runtime_stop(runtime);
    vr_runtime_destroy(runtime);
  }
  session_started.store(false, std::memory_order_release);
}

}  // namespace

bool startRedengineOpenXrBridge() {
  std::lock_guard lock(worker_mutex);
  if (worker.joinable()) {
    return false;
  }
  stop_requested.store(false, std::memory_order_release);
  worker_started.store(false, std::memory_order_relaxed);
  session_started.store(false, std::memory_order_relaxed);
  start_result.store(VR_RUNTIME_SKIPPED, std::memory_order_relaxed);
  frame_result.store(VR_RUNTIME_SKIPPED, std::memory_order_relaxed);
  frame_count.store(0, std::memory_order_relaxed);
  eye_count.store(0, std::memory_order_relaxed);
  orientation_valid.store(false, std::memory_order_relaxed);
  position_valid.store(false, std::memory_order_relaxed);
  worker = std::thread(&runBridge);
  return true;
}

void stopRedengineOpenXrBridge() {
  std::thread stopping;
  {
    std::lock_guard lock(worker_mutex);
    stop_requested.store(true, std::memory_order_release);
    stopping = std::move(worker);
  }
  if (stopping.joinable()) {
    stopping.join();
  }
}

RedengineOpenXrStatus queryRedengineOpenXrStatus() {
  return {
      worker_started.load(std::memory_order_acquire),
      session_started.load(std::memory_order_acquire),
      static_cast<VrRuntimeResult>(start_result.load(std::memory_order_acquire)),
      static_cast<VrRuntimeResult>(frame_result.load(std::memory_order_acquire)),
      frame_count.load(std::memory_order_acquire),
      eye_count.load(std::memory_order_acquire),
      orientation_valid.load(std::memory_order_acquire),
      position_valid.load(std::memory_order_acquire)};
}

}  // namespace vrclient::adapters::redengine

#include <windows.h>

#include "adapters/unreal/d3d12_observer.h"
#include "adapters/unreal/meccha_detour_transaction.h"
#include "adapters/unreal/meccha_evidence_dispatch_state.h"

#if defined(VRCLIENT_MECCHA_HEADSET_BRIDGE) || \
    defined(VRCLIENT_MECCHA_SCENE_RELAY)
#include "adapters/unreal/d3d12_stereo_diagnostic_renderer.h"
#include "adapters/unreal/d3d12_scene_relay_renderer.h"
#include "adapters/unreal/unreal_openxr_bridge.h"
#include "public/vr_runtime_api.h"
#endif

#if defined(VRCLIENT_MECCHA_ORIENTATION_ONLY)
#include "adapters/unreal/meccha_orientation_service.h"
#endif

#include <detours.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <string>

namespace {

using vrclient::adapters::unreal::D3D12ObservationResult;
using vrclient::adapters::unreal::D3D12ObservationSnapshot;
using vrclient::adapters::unreal::D3D12Observer;

constexpr wchar_t kEvidenceEnvironment[] =
    L"VRCLIENT_MECCHA_OBSERVER_EVIDENCE";
constexpr DWORD kEvidenceDispatchPollMs = 100;
constexpr DWORD kEvidenceRetryInitialMs = 10;
constexpr DWORD kEvidenceRetryMaximumMs = 250;
constexpr DWORD kEvidenceTerminalFlushMs = 2'000;
constexpr DWORD kEvidenceDispatcherStartTimeoutMs = 5'000;

using CreateFactory2Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);
using CreateSwapChainFn = HRESULT(STDMETHODCALLTYPE*)(
    IDXGIFactory*,
    IUnknown*,
    DXGI_SWAP_CHAIN_DESC*,
    IDXGISwapChain**);
using CreateSwapChainForHwndFn = HRESULT(STDMETHODCALLTYPE*)(
    IDXGIFactory2*,
    IUnknown*,
    HWND,
    const DXGI_SWAP_CHAIN_DESC1*,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*,
    IDXGIOutput*,
    IDXGISwapChain1**);
using CreateSwapChainForCoreWindowFn = HRESULT(STDMETHODCALLTYPE*)(
    IDXGIFactory2*,
    IUnknown*,
    IUnknown*,
    const DXGI_SWAP_CHAIN_DESC1*,
    IDXGIOutput*,
    IDXGISwapChain1**);
using CreateSwapChainForCompositionFn = HRESULT(STDMETHODCALLTYPE*)(
    IDXGIFactory2*,
    IUnknown*,
    const DXGI_SWAP_CHAIN_DESC1*,
    IDXGIOutput*,
    IDXGISwapChain1**);
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(
    IDXGISwapChain*,
    UINT,
    UINT,
    UINT,
    DXGI_FORMAT,
    UINT);
using ExecuteCommandListsFn = void(STDMETHODCALLTYPE*)(
    ID3D12CommandQueue*,
    UINT,
    ID3D12CommandList* const*);

using vrclient::adapters::unreal::MecchaEvidenceDispatchOperations;
using vrclient::adapters::unreal::MecchaEvidenceDispatcher;
using vrclient::adapters::unreal::MecchaEvidencePersistResult;
using vrclient::adapters::unreal::MecchaEvidenceWorkerEntry;

MecchaEvidencePersistResult writeEvidence();

class WindowsEvidenceDispatchOperations final
    : public MecchaEvidenceDispatchOperations {
 public:
  bool createRequestSignal() override {
    if (request_event_ == nullptr) {
      request_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }
    return request_event_ != nullptr;
  }

  bool createAcknowledgedSignal(bool initially_signaled) override {
    if (acknowledged_event_ == nullptr) {
      acknowledged_event_ = CreateEventW(
          nullptr, TRUE, initially_signaled ? TRUE : FALSE, nullptr);
    }
    return acknowledged_event_ != nullptr;
  }

  bool startWorker(
      MecchaEvidenceWorkerEntry entry, void* context) override {
    if (worker_thread_ != nullptr) {
      return true;
    }
    worker_entry_ = entry;
    worker_context_ = context;
    worker_thread_ =
        CreateThread(nullptr, 0, &WindowsEvidenceDispatchOperations::workerThunk,
                     this, 0, nullptr);
    return worker_thread_ != nullptr;
  }

  bool signalRequest() override {
    return request_event_ != nullptr && SetEvent(request_event_) != FALSE;
  }

  bool resetAcknowledged() override {
    return acknowledged_event_ != nullptr &&
        ResetEvent(acknowledged_event_) != FALSE;
  }

  bool signalAcknowledged() override {
    return acknowledged_event_ != nullptr &&
        SetEvent(acknowledged_event_) != FALSE;
  }

  void waitForRequest(std::uint32_t timeout_ms) override {
    if (request_event_ != nullptr) {
      WaitForSingleObject(request_event_, timeout_ms);
    } else {
      Sleep(timeout_ms);
    }
  }

  void waitForAcknowledged(std::uint32_t timeout_ms) override {
    if (acknowledged_event_ != nullptr) {
      WaitForSingleObject(acknowledged_event_, timeout_ms);
    } else {
      Sleep(timeout_ms);
    }
  }

  void sleepFor(std::uint32_t timeout_ms) override { Sleep(timeout_ms); }

  std::uint64_t monotonicMilliseconds() override { return GetTickCount64(); }

  MecchaEvidencePersistResult persist() override { return writeEvidence(); }

  bool joinWorker(std::uint32_t timeout_ms) override {
    return worker_thread_ == nullptr ||
        WaitForSingleObject(worker_thread_, timeout_ms) == WAIT_OBJECT_0;
  }

 private:
  static DWORD WINAPI workerThunk(void* context) {
    auto* operations =
        static_cast<WindowsEvidenceDispatchOperations*>(context);
    operations->worker_entry_(operations->worker_context_);
    return 0;
  }

  HANDLE request_event_ = nullptr;
  HANDLE acknowledged_event_ = nullptr;
  HANDLE worker_thread_ = nullptr;
  MecchaEvidenceWorkerEntry worker_entry_ = nullptr;
  void* worker_context_ = nullptr;
};

CreateFactory2Fn realCreateDXGIFactory2 = ::CreateDXGIFactory2;
CreateSwapChainFn realCreateSwapChain = nullptr;
CreateSwapChainForHwndFn realCreateSwapChainForHwnd = nullptr;
CreateSwapChainForCoreWindowFn realCreateSwapChainForCoreWindow = nullptr;
CreateSwapChainForCompositionFn realCreateSwapChainForComposition = nullptr;
PresentFn realPresent = nullptr;
ResizeBuffersFn realResizeBuffers = nullptr;
ExecuteCommandListsFn realExecuteCommandLists = nullptr;

// Process-lifetime by design. Releasing retained DXGI/D3D12 COM objects from a
// C++ global destructor can run after the graphics DLLs begin process teardown.
// The OS reclaims these references at process exit; live detach is not supported.
D3D12Observer* observer = new D3D12Observer();
SRWLOCK hookLock = SRWLOCK_INIT;
SRWLOCK evidenceLock = SRWLOCK_INIT;
WindowsEvidenceDispatchOperations* evidenceOperations =
    new WindowsEvidenceDispatchOperations();
MecchaEvidenceDispatcher* evidenceDispatcher = new MecchaEvidenceDispatcher(
    *evidenceOperations,
    kEvidenceDispatchPollMs,
    kEvidenceRetryInitialMs,
    kEvidenceRetryMaximumMs);
std::atomic<bool> factoryHooksInstalled{false};
std::atomic<bool> swapchainHooksInstalled{false};
std::atomic<bool> queueHooksInstalled{false};
std::atomic<bool> primingLateHooks{false};

#if defined(VRCLIENT_MECCHA_HEADSET_BRIDGE) || \
    defined(VRCLIENT_MECCHA_SCENE_RELAY)
HMODULE payloadModule = nullptr;
vrclient::adapters::unreal::MecchaEvidenceResultState headsetStartStatus{
    VR_RUNTIME_ERROR_STATE};
vrclient::adapters::unreal::MecchaEvidenceResultState headsetFrameStatus{
    VR_RUNTIME_SKIPPED};
std::atomic<unsigned long long> headsetFrameCount{0};
std::atomic<unsigned> headsetEyeCount{0};
std::atomic<unsigned> headsetOrientationValid{0};
std::atomic<unsigned> headsetPositionValid{0};
std::atomic<bool> headsetReady{false};
#if defined(VRCLIENT_MECCHA_ORIENTATION_ONLY)
std::atomic<bool> orientationPoseSamplerStop{false};
#endif
#if defined(VRCLIENT_MECCHA_SCENE_RELAY)
std::atomic<vrclient::adapters::unreal::D3D12SceneRelayRenderer*>
    sceneRelayRenderer{nullptr};
std::atomic<unsigned long long> sceneCaptureCount{0};
std::atomic<unsigned long long> sceneCaptureFailCount{0};
#endif
#endif

bool resolveEvidencePath(wchar_t* path, DWORD capacity) {
  const DWORD environment_length = GetEnvironmentVariableW(
      kEvidenceEnvironment, path, capacity);
  if (environment_length > 0 && environment_length < capacity) {
    return true;
  }

  wchar_t temp[MAX_PATH]{};
  if (GetTempPathW(MAX_PATH, temp) == 0) {
    return false;
  }
  wchar_t sidecar[MAX_PATH]{};
  swprintf_s(
      sidecar,
      L"%svrclient-meccha-observer-%lu.path",
      temp,
      GetCurrentProcessId());
  const HANDLE file = CreateFileW(
      sidecar,
      GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }
  DWORD bytes_read = 0;
  const BOOL read = ReadFile(
      file,
      path,
      (capacity - 1) * sizeof(wchar_t),
      &bytes_read,
      nullptr);
  CloseHandle(file);
  if (!read || bytes_read < sizeof(wchar_t)) {
    return false;
  }
  path[bytes_read / sizeof(wchar_t)] = L'\0';
  return path[0] != L'\0';
}

class CheckedEvidenceFile {
 public:
  explicit CheckedEvidenceFile(HANDLE handle) : handle_(handle) {}

  bool write(const void* data, DWORD size) {
    if (!ok_) {
      return false;
    }
    const auto* cursor = static_cast<const std::uint8_t*>(data);
    DWORD remaining = size;
    while (remaining > 0) {
      DWORD written = 0;
      if (!WriteFile(handle_, cursor, remaining, &written, nullptr) ||
          written == 0) {
        ok_ = false;
        return false;
      }
      cursor += written;
      remaining -= written;
    }
    return true;
  }

  HANDLE handle() const { return handle_; }
  bool ok() const { return ok_; }
  void fail() { ok_ = false; }

 private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
  bool ok_ = true;
};

void writeUnsigned(
    CheckedEvidenceFile& file,
    const char* prefix,
    unsigned long long value) {
  char buffer[96]{};
  const int length = sprintf_s(buffer, "%s%llu", prefix, value);
  if (length <= 0) {
    file.fail();
    return;
  }
  file.write(buffer, static_cast<DWORD>(length));
}

void writeSigned(
    CheckedEvidenceFile& file,
    const char* prefix,
    long long value) {
  char buffer[96]{};
  const int length = sprintf_s(buffer, "%s%lld", prefix, value);
  if (length <= 0) {
    file.fail();
    return;
  }
  file.write(buffer, static_cast<DWORD>(length));
}

void writeDouble(
    CheckedEvidenceFile& file,
    const char* prefix,
    double value) {
  char buffer[128]{};
  const int length = sprintf_s(buffer, "%s%.17g", prefix, value);
  if (length <= 0) {
    file.fail();
    return;
  }
  file.write(buffer, static_cast<DWORD>(length));
}

void writeLiteral(CheckedEvidenceFile& file, const char* text) {
  if (text == nullptr) {
    file.fail();
    return;
  }
  file.write(text, static_cast<DWORD>(lstrlenA(text)));
}

MecchaEvidencePersistResult writeEvidence() {
  AcquireSRWLockExclusive(&evidenceLock);
  wchar_t path[32768]{};
  if (!resolveEvidencePath(path, static_cast<DWORD>(std::size(path)))) {
    ReleaseSRWLockExclusive(&evidenceLock);
    return MecchaEvidencePersistResult::WriteFailed;
  }
  wchar_t temporary_path[32768]{};
  if (wcscpy_s(temporary_path, path) != 0 ||
      wcscat_s(temporary_path, L".tmp") != 0) {
    ReleaseSRWLockExclusive(&evidenceLock);
    return MecchaEvidencePersistResult::WriteFailed;
  }

  const auto snapshot = observer->snapshot();
  const HANDLE file_handle = CreateFileW(
      temporary_path,
      GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr,
      CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);
  if (file_handle == INVALID_HANDLE_VALUE) {
    ReleaseSRWLockExclusive(&evidenceLock);
    return MecchaEvidencePersistResult::WriteFailed;
  }
  CheckedEvidenceFile file(file_handle);

#if defined(VRCLIENT_MECCHA_ORIENTATION_ONLY)
  writeLiteral(file, "{\n  \"schema\":\"vrclient-meccha-orientation-only/1\",\n");
#elif defined(VRCLIENT_MECCHA_SCENE_RELAY)
  writeLiteral(file, "{\n  \"schema\":\"vrclient-meccha-scene-relay/1\",\n");
#elif defined(VRCLIENT_MECCHA_HEADSET_BRIDGE)
  writeLiteral(file, "{\n  \"schema\":\"vrclient-meccha-headset-bridge/1\",\n");
#else
  writeLiteral(file, "{\n  \"schema\":\"vrclient-meccha-observer/1\",\n");
#endif
  writeUnsigned(file, "  \"pid\":", GetCurrentProcessId());
  writeLiteral(file, ",\n  \"renderer\":\"d3d12\",\n");
  writeLiteral(file, snapshot.ready ? "  \"ready\":true,\n" : "  \"ready\":false,\n");
  writeUnsigned(file, "  \"width\":", snapshot.width);
  writeLiteral(file, ",\n");
  writeUnsigned(file, "  \"height\":", snapshot.height);
  writeLiteral(file, ",\n");
  writeUnsigned(file, "  \"buffer_count\":", snapshot.buffer_count);
  writeLiteral(file, ",\n");
  writeUnsigned(file, "  \"format\":", static_cast<unsigned>(snapshot.format));
  writeLiteral(file, ",\n");
  writeUnsigned(file, "  \"present_count\":", snapshot.present_count);
  writeLiteral(file, ",\n");
  writeUnsigned(file, "  \"resize_count\":", snapshot.resize_count);
#if defined(VRCLIENT_MECCHA_HEADSET_BRIDGE) || \
    defined(VRCLIENT_MECCHA_SCENE_RELAY)
  const auto start_status = headsetStartStatus.snapshot();
  const auto frame_status = headsetFrameStatus.snapshot();
  writeLiteral(file, ",\n");
  writeSigned(
      file,
      "  \"runtime_start_result\":",
      start_status.result);
  writeLiteral(
      file,
      start_status.attempted
          ? ",\n  \"runtime_start_attempted\":true"
          : ",\n  \"runtime_start_attempted\":false");
  writeLiteral(file, ",\n");
  writeSigned(
      file,
      "  \"runtime_frame_result\":",
      frame_status.result);
  writeLiteral(
      file,
      frame_status.attempted
          ? ",\n  \"runtime_frame_attempted\":true"
          : ",\n  \"runtime_frame_attempted\":false");
  writeLiteral(
      file,
      headsetReady.load() ? ",\n  \"headset_ready\":true,\n"
                          : ",\n  \"headset_ready\":false,\n");
  writeUnsigned(
      file,
      "  \"headset_frame_count\":",
      headsetFrameCount.load());
  writeLiteral(file, ",\n");
  writeUnsigned(file, "  \"eye_count\":", headsetEyeCount.load());
  writeLiteral(file, ",\n");
  writeUnsigned(
      file,
      "  \"head_pose_orientation_valid\":",
      headsetOrientationValid.load());
  writeLiteral(file, ",\n");
  writeUnsigned(
      file,
      "  \"head_pose_position_valid\":",
      headsetPositionValid.load());
#if defined(VRCLIENT_MECCHA_SCENE_RELAY)
  writeLiteral(file, ",\n");
  writeUnsigned(
      file,
      "  \"scene_capture_count\":",
      sceneCaptureCount.load());
  writeLiteral(file, ",\n");
  writeUnsigned(
      file,
      "  \"scene_capture_fail_count\":",
      sceneCaptureFailCount.load());
#if defined(VRCLIENT_MECCHA_STEREO_RELAY)
  writeLiteral(
      file,
      ",\n  \"scene_source_layout\":\"side_by_side_stereo\","
      "\n  \"flat_output_restored_from_eye\":\"left\","
      "\n  \"stereo_source_vertical_fov_degrees\":110,"
      "\n  \"stereo_source_eye_aspect_mode\":\"configured_projection\"");
#else
  writeLiteral(
      file,
      ",\n  \"scene_source_layout\":\"monoscopic\"");
#endif
#endif
#if defined(VRCLIENT_MECCHA_ORIENTATION_ONLY)
  const auto orientation =
      vrclient::adapters::unreal::mecchaOrientationSnapshot();
  writeLiteral(file, ",\n  \"orientation_status\":\"");
  writeLiteral(
      file,
      vrclient::adapters::unreal::mecchaOrientationStatusName(
          orientation.status));
  writeLiteral(file, "\",\n");
  writeLiteral(
      file,
      orientation.signature_verified
          ? "  \"orientation_signature_verified\":true,\n"
          : "  \"orientation_signature_verified\":false,\n");
  writeLiteral(
      file,
      orientation.hook_installed
          ? "  \"orientation_hook_installed\":true,\n"
          : "  \"orientation_hook_installed\":false,\n");
  writeUnsigned(file, "  \"orientation_constructor_rva\":", orientation.constructor_rva);
  writeLiteral(file, ",\n");
  writeUnsigned(
      file, "  \"orientation_constructor_hit_count\":",
      orientation.constructor_hit_count);
  writeLiteral(file, ",\n");
  writeUnsigned(
      file, "  \"orientation_decoded_camera_count\":",
      orientation.decoded_camera_count);
  writeLiteral(file, ",\n");
  writeUnsigned(
      file, "  \"orientation_rejected_camera_count\":",
      orientation.rejected_camera_count);
  writeLiteral(file, ",\n");
  writeUnsigned(
      file, "  \"orientation_stable_camera_id\":",
      orientation.stable_camera_id);
  writeLiteral(file, ",\n");
#if defined(VRCLIENT_MECCHA_STEREO_RELAY)
  writeUnsigned(
      file, "  \"orientation_secondary_stable_camera_id\":",
      orientation.secondary_stable_camera_id);
  writeLiteral(file, ",\n");
  writeLiteral(
      file,
      orientation.stereo_pair_snapshot_available
          ? "  \"orientation_stereo_pair_snapshot_available\":true,\n"
          : "  \"orientation_stereo_pair_snapshot_available\":false,\n");
  writeUnsigned(
      file, "  \"orientation_stereo_completed_pair_count\":",
      orientation.stereo_completed_pair_count);
  writeLiteral(file, ",\n");
  writeUnsigned(
      file, "  \"orientation_stereo_incomplete_pair_count\":",
      orientation.stereo_incomplete_pair_count);
  writeLiteral(file, ",\n");
  writeUnsigned(
      file, "  \"orientation_stereo_duplicate_eye_count\":",
      orientation.stereo_duplicate_eye_count);
  writeLiteral(file, ",\n");
  writeUnsigned(
      file, "  \"orientation_stereo_pose_frame_mismatch_count\":",
      orientation.stereo_pose_frame_mismatch_count);
  writeLiteral(file, ",\n");
  writeUnsigned(
      file, "  \"orientation_stereo_pair_pose_frame_index\":",
      orientation.stereo_pair_pose_frame_index);
  writeLiteral(file, ",\n");
  writeUnsigned(
      file, "  \"orientation_stereo_pair_render_generation\":",
      orientation.stereo_pair_render_generation);
  writeLiteral(file, ",\n");
  writeUnsigned(
      file, "  \"orientation_stereo_eye_0_camera_id\":",
      orientation.stereo_eye_camera_ids[0]);
  writeLiteral(file, ",\n");
  writeUnsigned(
      file, "  \"orientation_stereo_eye_1_camera_id\":",
      orientation.stereo_eye_camera_ids[1]);
  writeLiteral(file, ",\n");
  constexpr const char* kEyeAxes[3] = {"x", "y", "z"};
  for (std::size_t eye = 0; eye < orientation.stereo_eye_locations_uu.size();
       ++eye) {
    const double values[3] = {
        orientation.stereo_eye_locations_uu[eye].x,
        orientation.stereo_eye_locations_uu[eye].y,
        orientation.stereo_eye_locations_uu[eye].z};
    for (std::size_t axis = 0; axis < std::size(values); ++axis) {
      char prefix[128]{};
      sprintf_s(
          prefix, "  \"orientation_stereo_eye_%zu_location_%s_uu\":",
          eye, kEyeAxes[axis]);
      writeDouble(file, prefix, values[axis]);
      writeLiteral(file, ",\n");
    }
  }
  writeSigned(
      file, "  \"orientation_stereo_eye_observation_delta_ns\":",
      orientation.stereo_eye_observation_delta_ns);
  writeLiteral(file, ",\n");
  writeDouble(
      file, "  \"orientation_stereo_eye_separation_uu\":",
      orientation.stereo_eye_separation_uu);
  writeLiteral(file, ",\n");
  writeDouble(
      file, "  \"orientation_observed_vertical_fov_degrees\":",
      orientation.observed_vertical_fov_degrees);
  writeLiteral(file, ",\n");
  writeDouble(
      file, "  \"orientation_observed_aspect_ratio\":",
      orientation.observed_aspect_ratio);
  writeLiteral(file, ",\n");
#endif
  writeLiteral(
      file,
      orientation.pose_snapshot_available
          ? "  \"orientation_pose_snapshot_available\":true,\n"
          : "  \"orientation_pose_snapshot_available\":false,\n");
  writeLiteral(
      file,
      orientation.operational_snapshot_available
          ? "  \"orientation_operational_snapshot_available\":true,\n"
          : "  \"orientation_operational_snapshot_available\":false,\n");
  writeLiteral(
      file,
      orientation.pose_sample_valid
          ? "  \"orientation_pose_sample_valid\":true,\n"
          : "  \"orientation_pose_sample_valid\":false,\n");
  writeLiteral(
      file,
      orientation.tracking_ready
          ? "  \"orientation_tracking_ready\":true,\n"
          : "  \"orientation_tracking_ready\":false,\n");
  writeLiteral(
      file,
      orientation.tracking_fault_latched
          ? "  \"orientation_tracking_fault_latched\":true,\n"
          : "  \"orientation_tracking_fault_latched\":false,\n");
  writeLiteral(
      file,
      orientation.sampler_active
          ? "  \"orientation_pose_sampler_active\":true,\n"
          : "  \"orientation_pose_sampler_active\":false,\n");
  writeSigned(
      file, "  \"orientation_pose_sample_result\":",
      orientation.sampler_result);
  writeLiteral(file, ",\n");
  writeUnsigned(
      file, "  \"orientation_pose_sample_attempt_count\":",
      orientation.sampler_attempt_count);
  writeLiteral(file, ",\n");
  writeUnsigned(
      file, "  \"orientation_pose_sample_success_count\":",
      orientation.sampler_success_count);
  writeLiteral(file, ",\n");
  writeUnsigned(
      file, "  \"orientation_pose_publish_count\":",
      orientation.pose_publish_count);
  writeLiteral(file, ",\n");
  writeUnsigned(
      file, "  \"orientation_pose_frame_index\":",
      orientation.pose_frame_index);
  writeLiteral(file, ",\n");
  writeSigned(
      file, "  \"orientation_pose_publish_time_ns\":",
      orientation.pose_publish_time_ns);
  writeLiteral(file, ",\n");
  constexpr const char* kDeltaNames[9] = {
      "m00", "m01", "m02", "m10", "m11", "m12", "m20", "m21", "m22"};
  for (std::size_t index = 0; index < orientation.pose_unreal_delta.size(); ++index) {
    char prefix[96]{};
    sprintf_s(
        prefix,
        "  \"orientation_unreal_delta_%s\":",
        kDeltaNames[index]);
    writeDouble(file, prefix, orientation.pose_unreal_delta[index]);
    writeLiteral(file, ",\n");
  }
  writeUnsigned(
      file, "  \"orientation_apply_count\":",
      orientation.orientation_apply_count);
  writeLiteral(file, ",\n");
  writeUnsigned(
      file, "  \"consecutive_orientation_apply_count\":",
      orientation.consecutive_orientation_apply_count);
  writeLiteral(file, ",\n");
  writeUnsigned(
      file, "  \"orientation_missing_pose_skip_count\":",
      orientation.missing_pose_skip_count);
  writeLiteral(file, ",\n");
  writeUnsigned(
      file, "  \"orientation_stale_pose_skip_count\":",
      orientation.stale_pose_skip_count);
  writeLiteral(file, ",\n");
  writeUnsigned(
      file, "  \"orientation_write_reject_count\":",
      orientation.write_reject_count);
#endif
#endif
  writeLiteral(file, "\n}\n");
  const bool written = file.ok();
  const bool flushed = written && FlushFileBuffers(file.handle()) != FALSE;
  CloseHandle(file.handle());
  MecchaEvidencePersistResult result = MecchaEvidencePersistResult::Success;
  if (!written) {
    result = MecchaEvidencePersistResult::WriteFailed;
  } else if (!flushed) {
    result = MecchaEvidencePersistResult::FlushFailed;
  } else if (!MoveFileExW(
                 temporary_path,
                 path,
                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    result = MecchaEvidencePersistResult::RenameFailed;
  }
  if (result != MecchaEvidencePersistResult::Success) {
    DeleteFileW(temporary_path);
  }
  ReleaseSRWLockExclusive(&evidenceLock);
  return result;
}

bool ensureEvidenceDispatcher() {
  return evidenceDispatcher->initializeOnce();
}

bool ensureEvidenceDispatcherUntilReady() {
  return evidenceDispatcher->initializeUntilReady(
      kEvidenceDispatcherStartTimeoutMs,
      kEvidenceRetryInitialMs,
      kEvidenceRetryMaximumMs);
}

std::uint64_t requestEvidenceWrite() {
  return evidenceDispatcher->request();
}

bool flushEvidenceGeneration(std::uint64_t generation, DWORD timeout_ms) {
  return evidenceDispatcher->flush(generation, timeout_ms);
}

DWORD finishHeadsetBridge(DWORD exit_code) {
  const std::uint64_t generation = requestEvidenceWrite();
  flushEvidenceGeneration(generation, kEvidenceTerminalFlushMs);
  return exit_code;
}

template <typename Function>
bool attachDetour(Function* original, Function hook) {
  const vrclient::adapters::unreal::MecchaDetourAttachment attachment{
      reinterpret_cast<PVOID*>(original), reinterpret_cast<PVOID>(hook)};
  return vrclient::adapters::unreal::commitMecchaDetourTransaction(
      &attachment, 1);
}

template <typename Interface, typename Function>
Function vtableFunction(Interface* object, std::size_t index) {
  if (object == nullptr) {
    return nullptr;
  }
  void** table = *reinterpret_cast<void***>(object);
  return reinterpret_cast<Function>(table[index]);
}

void installSwapchainHooks(IDXGISwapChain3* swapchain);

void observeSwapchain(IUnknown* device_or_queue, IUnknown* created_swapchain) {
  if (device_or_queue == nullptr || created_swapchain == nullptr) {
    return;
  }

  Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain;
  if (FAILED(created_swapchain->QueryInterface(IID_PPV_ARGS(&swapchain)))) {
    return;
  }
  if (primingLateHooks.load(std::memory_order_acquire)) {
    installSwapchainHooks(swapchain.Get());
    return;
  }

  Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
  Microsoft::WRL::ComPtr<ID3D12Device> device;
  if (FAILED(device_or_queue->QueryInterface(IID_PPV_ARGS(&queue))) ||
      FAILED(queue->GetDevice(IID_PPV_ARGS(&device)))) {
    return;
  }

  if (observer->observeDeviceQueue(device.Get(), queue.Get()) !=
          D3D12ObservationResult::Pending ||
      observer->observeSwapchain(swapchain.Get()) !=
          D3D12ObservationResult::Ready) {
    return;
  }
  installSwapchainHooks(swapchain.Get());
  requestEvidenceWrite();
}

HRESULT STDMETHODCALLTYPE hookedCreateSwapChain(
    IDXGIFactory* factory,
    IUnknown* device,
    DXGI_SWAP_CHAIN_DESC* desc,
    IDXGISwapChain** swapchain) {
  const HRESULT result =
      realCreateSwapChain(factory, device, desc, swapchain);
  if (SUCCEEDED(result) && swapchain != nullptr) {
    observeSwapchain(device, *swapchain);
  }
  return result;
}

HRESULT STDMETHODCALLTYPE hookedCreateSwapChainForHwnd(
    IDXGIFactory2* factory,
    IUnknown* device,
    HWND window,
    const DXGI_SWAP_CHAIN_DESC1* desc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen,
    IDXGIOutput* output,
    IDXGISwapChain1** swapchain) {
  const HRESULT result = realCreateSwapChainForHwnd(
      factory, device, window, desc, fullscreen, output, swapchain);
  if (SUCCEEDED(result) && swapchain != nullptr) {
    observeSwapchain(device, *swapchain);
  }
  return result;
}

HRESULT STDMETHODCALLTYPE hookedCreateSwapChainForCoreWindow(
    IDXGIFactory2* factory,
    IUnknown* device,
    IUnknown* window,
    const DXGI_SWAP_CHAIN_DESC1* desc,
    IDXGIOutput* output,
    IDXGISwapChain1** swapchain) {
  const HRESULT result = realCreateSwapChainForCoreWindow(
      factory, device, window, desc, output, swapchain);
  if (SUCCEEDED(result) && swapchain != nullptr) {
    observeSwapchain(device, *swapchain);
  }
  return result;
}

HRESULT STDMETHODCALLTYPE hookedCreateSwapChainForComposition(
    IDXGIFactory2* factory,
    IUnknown* device,
    const DXGI_SWAP_CHAIN_DESC1* desc,
    IDXGIOutput* output,
    IDXGISwapChain1** swapchain) {
  const HRESULT result = realCreateSwapChainForComposition(
      factory, device, desc, output, swapchain);
  if (SUCCEEDED(result) && swapchain != nullptr) {
    observeSwapchain(device, *swapchain);
  }
  return result;
}

HRESULT STDMETHODCALLTYPE hookedPresent(
    IDXGISwapChain* swapchain,
    UINT sync_interval,
    UINT flags) {
#if defined(VRCLIENT_MECCHA_SCENE_RELAY)
  auto* relay = sceneRelayRenderer.load(std::memory_order_acquire);
  if (relay != nullptr && swapchain != nullptr) {
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain3;
    const auto observed = observer->snapshot();
    if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain3))) &&
        observed.ready && observed.swapchain == swapchain3.Get()) {
      Microsoft::WRL::ComPtr<ID3D12Resource> back_buffer;
      const UINT index = swapchain3->GetCurrentBackBufferIndex();
      VrRuntimeResult capture_result = VR_RUNTIME_ERROR_GRAPHICS;
      if (SUCCEEDED(swapchain3->GetBuffer(
              index, IID_PPV_ARGS(&back_buffer)))) {
#if defined(VRCLIENT_MECCHA_STEREO_RELAY)
        capture_result = relay->captureSideBySideStereo(back_buffer.Get());
#else
        capture_result = relay->capture(back_buffer.Get());
#endif
      }
      if (capture_result == VR_RUNTIME_OK) {
        sceneCaptureCount.fetch_add(1, std::memory_order_relaxed);
      } else {
        sceneCaptureFailCount.fetch_add(1, std::memory_order_relaxed);
      }
    }
  }
#endif
  const HRESULT result = realPresent(swapchain, sync_interval, flags);
  if (SUCCEEDED(result)) {
    const auto before = observer->snapshot();
    if (before.queue != nullptr && before.swapchain != swapchain) {
      Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain3;
      if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain3)))) {
        observer->observeSwapchain(swapchain3.Get());
      }
    }
    observer->recordPresent();
    const auto snapshot = observer->snapshot();
#if defined(VRCLIENT_MECCHA_STEREO_RELAY) && \
    defined(VRCLIENT_MECCHA_ORIENTATION_ONLY)
    vrclient::adapters::unreal::publishMecchaStereoRenderGeneration(
        snapshot.present_count);
#endif
    if (snapshot.present_count == 1 || snapshot.present_count == 4 ||
        snapshot.present_count == 60) {
      requestEvidenceWrite();
    }
  }
  return result;
}

void STDMETHODCALLTYPE hookedExecuteCommandLists(
    ID3D12CommandQueue* queue,
    UINT count,
    ID3D12CommandList* const* lists) {
  const auto snapshot = observer->snapshot();
  if (snapshot.queue != queue) {
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    if (SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&device)))) {
      observer->observeDeviceQueue(device.Get(), queue);
    }
  }
  realExecuteCommandLists(queue, count, lists);
}

void installQueueHooks(ID3D12CommandQueue* queue) {
  if (queueHooksInstalled.load(std::memory_order_acquire) || queue == nullptr) {
    return;
  }
  AcquireSRWLockExclusive(&hookLock);
  if (!queueHooksInstalled.load(std::memory_order_relaxed)) {
    realExecuteCommandLists =
        vtableFunction<ID3D12CommandQueue, ExecuteCommandListsFn>(queue, 10);
    queueHooksInstalled.store(
        attachDetour(&realExecuteCommandLists, &hookedExecuteCommandLists),
        std::memory_order_release);
  }
  ReleaseSRWLockExclusive(&hookLock);
}

HRESULT STDMETHODCALLTYPE hookedResizeBuffers(
    IDXGISwapChain* swapchain,
    UINT buffer_count,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    UINT flags) {
  const HRESULT result = realResizeBuffers(
      swapchain, buffer_count, width, height, format, flags);
  if (SUCCEEDED(result)) {
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain3;
    if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain3)))) {
      observer->observeSwapchain(swapchain3.Get());
      observer->recordResize();
      requestEvidenceWrite();
    }
  }
  return result;
}

void installSwapchainHooks(IDXGISwapChain3* swapchain) {
  if (swapchainHooksInstalled.load(std::memory_order_acquire) ||
      swapchain == nullptr) {
    return;
  }
  AcquireSRWLockExclusive(&hookLock);
  if (!swapchainHooksInstalled.load(std::memory_order_relaxed)) {
    realPresent = vtableFunction<IDXGISwapChain3, PresentFn>(swapchain, 8);
    realResizeBuffers =
        vtableFunction<IDXGISwapChain3, ResizeBuffersFn>(swapchain, 13);
    const std::array<vrclient::adapters::unreal::MecchaDetourAttachment, 2>
        attachments{{
            {reinterpret_cast<PVOID*>(&realPresent),
             reinterpret_cast<PVOID>(hookedPresent)},
            {reinterpret_cast<PVOID*>(&realResizeBuffers),
             reinterpret_cast<PVOID>(hookedResizeBuffers)},
        }};
    swapchainHooksInstalled.store(
        vrclient::adapters::unreal::commitMecchaDetourTransaction(
            attachments.data(), attachments.size()),
        std::memory_order_release);
  }
  ReleaseSRWLockExclusive(&hookLock);
}

void installFactoryHooks(IDXGIFactory2* factory) {
  if (factoryHooksInstalled.load(std::memory_order_acquire) || factory == nullptr) {
    return;
  }
  AcquireSRWLockExclusive(&hookLock);
  if (!factoryHooksInstalled.load(std::memory_order_relaxed)) {
    realCreateSwapChain =
        vtableFunction<IDXGIFactory2, CreateSwapChainFn>(factory, 10);
    realCreateSwapChainForHwnd =
        vtableFunction<IDXGIFactory2, CreateSwapChainForHwndFn>(factory, 15);
    realCreateSwapChainForCoreWindow =
        vtableFunction<IDXGIFactory2, CreateSwapChainForCoreWindowFn>(factory, 16);
    realCreateSwapChainForComposition =
        vtableFunction<IDXGIFactory2, CreateSwapChainForCompositionFn>(factory, 24);
    const std::array<vrclient::adapters::unreal::MecchaDetourAttachment, 4>
        attachments{{
            {reinterpret_cast<PVOID*>(&realCreateSwapChain),
             reinterpret_cast<PVOID>(hookedCreateSwapChain)},
            {reinterpret_cast<PVOID*>(&realCreateSwapChainForHwnd),
             reinterpret_cast<PVOID>(hookedCreateSwapChainForHwnd)},
            {reinterpret_cast<PVOID*>(&realCreateSwapChainForCoreWindow),
             reinterpret_cast<PVOID>(hookedCreateSwapChainForCoreWindow)},
            {reinterpret_cast<PVOID*>(&realCreateSwapChainForComposition),
             reinterpret_cast<PVOID>(hookedCreateSwapChainForComposition)},
        }};
    factoryHooksInstalled.store(
        vrclient::adapters::unreal::commitMecchaDetourTransaction(
            attachments.data(), attachments.size()),
        std::memory_order_release);
  }
  ReleaseSRWLockExclusive(&hookLock);
}

void observeFactory(void* object) {
  if (object == nullptr) {
    return;
  }
  IUnknown* unknown = static_cast<IUnknown*>(object);
  Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
  if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&factory)))) {
    installFactoryHooks(factory.Get());
  }
}

HRESULT WINAPI hookedCreateDXGIFactory2(
    UINT flags,
    REFIID iid,
    void** factory) {
  const HRESULT result = realCreateDXGIFactory2(flags, iid, factory);
  if (SUCCEEDED(result) && factory != nullptr) {
    observeFactory(*factory);
  }
  return result;
}

bool installExportHooks() {
  const vrclient::adapters::unreal::MecchaDetourAttachment attachment{
      reinterpret_cast<PVOID*>(&realCreateDXGIFactory2),
      reinterpret_cast<PVOID>(hookedCreateDXGIFactory2)};
  return vrclient::adapters::unreal::commitMecchaDetourTransaction(
      &attachment, 1);
}

DWORD WINAPI primeLateObservationHooks(void*) {
  if (!ensureEvidenceDispatcherUntilReady()) {
    return ERROR_NOT_READY;
  }
  primingLateHooks.store(true, std::memory_order_release);

  Microsoft::WRL::ComPtr<ID3D12Device> device;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
  Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
  Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain;

  if (SUCCEEDED(D3D12CreateDevice(
          nullptr,
          D3D_FEATURE_LEVEL_11_0,
          IID_PPV_ARGS(&device)))) {
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (SUCCEEDED(device->CreateCommandQueue(
            &queue_desc,
            IID_PPV_ARGS(&queue)))) {
      installQueueHooks(queue.Get());
    }
  }

  if (queue && SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
    HWND window = CreateWindowExW(
        0,
        L"STATIC",
        L"",
        WS_POPUP,
        0,
        0,
        16,
        16,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    if (window != nullptr) {
      DXGI_SWAP_CHAIN_DESC1 desc{};
      desc.Width = 16;
      desc.Height = 16;
      desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      desc.SampleDesc.Count = 1;
      desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
      desc.BufferCount = 2;
      desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
      factory->CreateSwapChainForHwnd(
          queue.Get(),
          window,
          &desc,
          nullptr,
          nullptr,
          &swapchain);
      DestroyWindow(window);
    }
  }

  primingLateHooks.store(false, std::memory_order_release);
#if defined(VRCLIENT_MECCHA_ORIENTATION_ONLY)
  vrclient::adapters::unreal::installMecchaOrientationOnlyHook();
  requestEvidenceWrite();
#endif
  return 0;
}

#if defined(VRCLIENT_MECCHA_HEADSET_BRIDGE) || \
    defined(VRCLIENT_MECCHA_SCENE_RELAY)
// The runtime profile loader opens narrow paths through the CRT, which uses
// the ANSI codepage. Convert losslessly or fail closed (empty string) instead
// of silently mangling non-ANSI install paths.
std::string narrowPathLossless(const wchar_t* value) {
  if (value == nullptr || *value == L'\0') {
    return {};
  }
  BOOL used_default = FALSE;
  int needed = WideCharToMultiByte(
      CP_ACP, WC_NO_BEST_FIT_CHARS, value, -1, nullptr, 0, nullptr,
      &used_default);
  if (needed > 0 && !used_default) {
    std::string out(static_cast<std::size_t>(needed) - 1, '\0');
    if (WideCharToMultiByte(
            CP_ACP, WC_NO_BEST_FIT_CHARS, value, -1, out.data(), needed,
            nullptr, nullptr) > 0) {
      return out;
    }
    return {};
  }
  // Fall back to the 8.3 short path, which is usually plain ASCII.
  static wchar_t short_path[32768]{};
  const DWORD short_length = GetShortPathNameW(
      value, short_path, static_cast<DWORD>(std::size(short_path)));
  if (short_length == 0 || short_length >= std::size(short_path)) {
    return {};
  }
  used_default = FALSE;
  needed = WideCharToMultiByte(
      CP_ACP, WC_NO_BEST_FIT_CHARS, short_path, -1, nullptr, 0, nullptr,
      &used_default);
  if (needed <= 0 || used_default) {
    return {};
  }
  std::string out(static_cast<std::size_t>(needed) - 1, '\0');
  if (WideCharToMultiByte(
          CP_ACP, WC_NO_BEST_FIT_CHARS, short_path, -1, out.data(), needed,
          nullptr, nullptr) <= 0) {
    return {};
  }
  return out;
}

VrRuntimeResult renderHeadsetFrame(
    void* user_data,
    const VrRuntimeFrameData* frame,
    const VrRuntimeRenderTarget* targets,
    uint32_t target_count) {
  if (user_data == nullptr || frame == nullptr) {
    return VR_RUNTIME_ERROR_INVALID_ARGUMENT;
  }
#if defined(VRCLIENT_MECCHA_SCENE_RELAY)
  auto* renderer = static_cast<
      vrclient::adapters::unreal::D3D12SceneRelayRenderer*>(user_data);
#else
  auto* renderer = static_cast<
      vrclient::adapters::unreal::D3D12StereoDiagnosticRenderer*>(user_data);
#endif
  const VrRuntimeResult result =
      renderer->render(*frame, targets, target_count);
  if (result == VR_RUNTIME_OK) {
    const auto completed =
        headsetFrameCount.fetch_add(1, std::memory_order_relaxed) + 1;
    headsetEyeCount.store(target_count, std::memory_order_relaxed);
    headsetOrientationValid.store(
        frame->head_pose.orientation_valid,
        std::memory_order_relaxed);
    headsetPositionValid.store(
        frame->head_pose.position_valid,
        std::memory_order_relaxed);
    if (completed >= 2 && target_count == 2 &&
        frame->head_pose.orientation_valid != 0) {
#if defined(VRCLIENT_MECCHA_SCENE_RELAY)
      if (sceneCaptureCount.load(std::memory_order_acquire) > 0) {
        headsetReady.store(true, std::memory_order_release);
      }
#else
      headsetReady.store(true, std::memory_order_release);
#endif
    }
    if (completed <= 2 || completed % 60 == 0) {
      requestEvidenceWrite();
    }
  }
  return result;
}

#if defined(VRCLIENT_MECCHA_ORIENTATION_ONLY)
DWORD WINAPI runOrientationPoseSampler(void* parameter) {
  auto* runtime = static_cast<VrRuntime*>(parameter);
  std::uint64_t sample_index = 0;
  std::uint64_t attempt_count = 0;
  std::uint64_t success_count = 0;
  VrRuntimeResult last_result = VR_RUNTIME_SKIPPED;
  vrclient::adapters::unreal::publishMecchaOrientationSamplerState(
      true, last_result, attempt_count, success_count);
  while (!orientationPoseSamplerStop.load(std::memory_order_acquire)) {
    VrRuntimePose pose{};
    const VrRuntimeResult result =
        vr_runtime_sample_head_pose(runtime, &pose);
    last_result = result;
    ++attempt_count;
    if (result == VR_RUNTIME_OK) {
      ++success_count;
    } else {
      pose = {};
    }
    vrclient::adapters::unreal::publishMecchaOrientationOnlyPoseSample(
        pose, sample_index++, true, result, attempt_count, success_count);
    Sleep(10);
  }
  vrclient::adapters::unreal::publishMecchaOrientationSamplerState(
      false, last_result, attempt_count, success_count);
  return 0;
}
#endif

DWORD WINAPI runHeadsetBridge(void*) {
  if (!ensureEvidenceDispatcherUntilReady()) {
    headsetStartStatus.publish(VR_RUNTIME_ERROR_STATE);
    return finishHeadsetBridge(7);
  }
  // Bounded wait: if the game never produces an observable swapchain, record
  // the failure instead of spinning for the life of the process.
  constexpr DWORD kSwapchainWaitSliceMs = 100;
  constexpr DWORD kSwapchainWaitLimitMs = 300'000;
  D3D12ObservationSnapshot snapshot{};
  for (DWORD waited = 0; !snapshot.ready; waited += kSwapchainWaitSliceMs) {
    if (waited >= kSwapchainWaitLimitMs) {
      headsetStartStatus.publish(VR_RUNTIME_ERROR_STATE);
      return finishHeadsetBridge(5);
    }
    Sleep(kSwapchainWaitSliceMs);
    snapshot = observer->snapshot();
  }

  VrRuntimeD3D12Binding binding{};
  if (vrclient::adapters::unreal::makeOpenXrD3D12Binding(
          snapshot,
          &binding) !=
      vrclient::adapters::unreal::UnrealOpenXrBridgeResult::Ready) {
    headsetStartStatus.publish(VR_RUNTIME_ERROR_GRAPHICS);
    return finishHeadsetBridge(1);
  }

#if defined(VRCLIENT_MECCHA_SCENE_RELAY)
  auto* renderer =
      new vrclient::adapters::unreal::D3D12SceneRelayRenderer();
#else
  auto* renderer =
      new vrclient::adapters::unreal::D3D12StereoDiagnosticRenderer();
#endif
  if (!renderer->initialize(snapshot.device, snapshot.queue)) {
    headsetStartStatus.publish(VR_RUNTIME_ERROR_GRAPHICS);
    return finishHeadsetBridge(2);
  }
#if defined(VRCLIENT_MECCHA_STEREO_RELAY)
  // The exact-build per-view seam normalizes Unreal's stock fake-stereo source
  // to this symmetric angular projection. Packed pixel dimensions select each
  // eye's UV region but do not redefine the source projection aspect.
  if (!renderer->configureSideBySideProjection({110.0F, 1.0F})) {
    headsetStartStatus.publish(VR_RUNTIME_ERROR_GRAPHICS);
    return finishHeadsetBridge(7);
  }
#endif
#if defined(VRCLIENT_MECCHA_SCENE_RELAY)
  sceneRelayRenderer.store(renderer, std::memory_order_release);
#endif

  wchar_t profile_path[32768]{};
  const DWORD module_length =
      GetModuleFileNameW(payloadModule, profile_path, std::size(profile_path));
  if (module_length == 0 || module_length >= std::size(profile_path)) {
    headsetStartStatus.publish(VR_RUNTIME_ERROR_PROFILE);
    return finishHeadsetBridge(3);
  }
  wchar_t* separator = wcsrchr(profile_path, L'\\');
  if (separator == nullptr) {
    headsetStartStatus.publish(VR_RUNTIME_ERROR_PROFILE);
    return finishHeadsetBridge(4);
  }
  wcscpy_s(separator + 1, std::size(profile_path) - (separator + 1 - profile_path),
           L"runtime-profile.json");
  const std::string profile = narrowPathLossless(profile_path);
  if (profile.empty()) {
    headsetStartStatus.publish(VR_RUNTIME_ERROR_PROFILE);
    return finishHeadsetBridge(6);
  }

  VrRuntimeDesc desc{};
  desc.size = sizeof(VrRuntimeDesc);
#if defined(VRCLIENT_MECCHA_STEREO_RELAY)
  desc.application_name = "VRClient Meccha Stereo Relay";
#elif defined(VRCLIENT_MECCHA_ORIENTATION_ONLY)
  desc.application_name = "VRClient Meccha Orientation Only";
#elif defined(VRCLIENT_MECCHA_SCENE_RELAY)
  desc.application_name = "VRClient Meccha Scene Relay";
#else
  desc.application_name = "VRClient Meccha Headset Bridge";
#endif
  desc.runtime_profile_path = profile.c_str();
  desc.preferred_graphics_backend = VR_RUNTIME_GRAPHICS_BACKEND_D3D12;
  desc.d3d12_binding = &binding;

  VrRuntime* runtime = nullptr;
  VrRuntimeResult result = vr_runtime_create(&desc, &runtime);
#if defined(VRCLIENT_MECCHA_ORIENTATION_ONLY)
  bool runtime_cleanup_safe = true;
#endif
  if (result == VR_RUNTIME_OK) {
    result = vr_runtime_start(runtime);
  }
  headsetStartStatus.publish(result);
  requestEvidenceWrite();

  if (result == VR_RUNTIME_OK) {
#if defined(VRCLIENT_MECCHA_ORIENTATION_ONLY)
    orientationPoseSamplerStop.store(false, std::memory_order_release);
    HANDLE orientation_pose_sampler = CreateThread(
        nullptr, 0, runOrientationPoseSampler, runtime, 0, nullptr);
    if (orientation_pose_sampler == nullptr) {
      result = VR_RUNTIME_ERROR_STATE;
      headsetFrameStatus.publish(result);
      requestEvidenceWrite();
    }
#endif
    if (result == VR_RUNTIME_OK) {
      while (true) {
        result = vr_runtime_run_frame(
            runtime,
            &renderHeadsetFrame,
            renderer);
        headsetFrameStatus.publish(result);
        if (result != VR_RUNTIME_OK && result != VR_RUNTIME_SKIPPED) {
          requestEvidenceWrite();
          break;
        }
        const VrRuntimeState runtime_state = vr_runtime_get_state(runtime);
        if (runtime_state == VR_RUNTIME_STATE_EXITING ||
            runtime_state == VR_RUNTIME_STATE_LOSS_PENDING ||
            runtime_state == VR_RUNTIME_STATE_ERROR ||
            runtime_state == VR_RUNTIME_STATE_STOPPED) {
          requestEvidenceWrite();
          break;
        }
        if (result == VR_RUNTIME_SKIPPED) {
          Sleep(10);
        }
      }
    }
#if defined(VRCLIENT_MECCHA_ORIENTATION_ONLY)
    orientationPoseSamplerStop.store(true, std::memory_order_release);
    if (orientation_pose_sampler != nullptr) {
      const DWORD sampler_wait =
          WaitForSingleObject(orientation_pose_sampler, 5'000);
      if (sampler_wait != WAIT_OBJECT_0) {
        // Never tear down OpenXR while the sampler may still be inside it.
        // The process owns the remaining lifetime, so leaking here is the
        // fail-closed alternative to a use-after-free during abnormal shutdown.
        runtime_cleanup_safe = false;
        result = VR_RUNTIME_ERROR_STATE;
        headsetFrameStatus.publish(result);
        requestEvidenceWrite();
      }
      CloseHandle(orientation_pose_sampler);
    }
#endif
  }

  if (runtime != nullptr) {
#if defined(VRCLIENT_MECCHA_ORIENTATION_ONLY)
    if (runtime_cleanup_safe) {
      vr_runtime_stop(runtime);
      vr_runtime_destroy(runtime);
    }
#else
    vr_runtime_stop(runtime);
    vr_runtime_destroy(runtime);
#endif
  }
  return finishHeadsetBridge(0);
}
#endif

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
  if (DetourIsHelperProcess()) {
    return TRUE;
  }
  if (reason == DLL_PROCESS_ATTACH) {
#if defined(VRCLIENT_MECCHA_HEADSET_BRIDGE) || \
    defined(VRCLIENT_MECCHA_SCENE_RELAY)
    payloadModule = module;
#endif
    DisableThreadLibraryCalls(module);
    DetourRestoreAfterWith();
    ensureEvidenceDispatcher();
    installExportHooks();
    const HANDLE worker =
        CreateThread(nullptr, 0, &primeLateObservationHooks, nullptr, 0, nullptr);
    if (worker != nullptr) {
      CloseHandle(worker);
    }
#if defined(VRCLIENT_MECCHA_HEADSET_BRIDGE) || \
    defined(VRCLIENT_MECCHA_SCENE_RELAY)
    const HANDLE headset_worker =
        CreateThread(nullptr, 0, &runHeadsetBridge, nullptr, 0, nullptr);
    if (headset_worker != nullptr) {
      CloseHandle(headset_worker);
    }
#endif
  }
  return TRUE;
}

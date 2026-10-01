#include <windows.h>
#include <bcrypt.h>

#include "adapters/unreal/meccha_camera_pin.h"
#include "adapters/unreal/unreal_camera_observer.h"
#include "adapters/unreal/unreal_scene_view_decoder.h"
#include "adapters/unreal/unreal_view_seam_locator.h"
#include "adapters/unreal/unreal_view_seam_pe.h"
#include "tooling/hookdisc/observe.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace vrclient::adapters::unreal;
using vrclient::tooling::hookdisc::HardwareBreakpointObserver;
using vrclient::tooling::hookdisc::HwBreakSlot;
using vrclient::tooling::hookdisc::ObservationRegisters;

constexpr wchar_t kEvidenceEnvironment[] = L"VRCLIENT_MECCHA_OBSERVER_EVIDENCE";
constexpr std::size_t kRingCapacity = 128;

enum class ProviderStatus : unsigned {
  Starting,
  HashFailed,
  PeFailed,
  PinRejected,
  BreakpointFailed,
  Observing,
};

struct RawSlot {
  std::atomic<std::uint64_t> guard{0};
  std::array<std::atomic<std::uint8_t>,
             kMeccha24508135SceneViewCaptureSize> bytes{};
  std::atomic<std::int64_t> qpc_ticks{0};
  std::atomic<std::uint32_t> thread_id{0};
};

struct ProviderState {
  std::array<RawSlot, kRingCapacity> slots{};
  std::atomic<std::uint64_t> write_count{0};
  std::atomic<std::uint64_t> capture_faults{0};
  std::atomic<ProviderStatus> status{ProviderStatus::Starting};
  HardwareBreakpointObserver observer;
};

ProviderState* state = new ProviderState();

const char* statusName(ProviderStatus status) {
  switch (status) {
    case ProviderStatus::Starting: return "starting";
    case ProviderStatus::HashFailed: return "hash_failed";
    case ProviderStatus::PeFailed: return "pe_failed";
    case ProviderStatus::PinRejected: return "pin_rejected";
    case ProviderStatus::BreakpointFailed: return "breakpoint_failed";
    case ProviderStatus::Observing: return "observing";
  }
  return "unknown";
}

bool resolveEvidencePath(wchar_t* path, DWORD capacity) {
  const DWORD length = GetEnvironmentVariableW(kEvidenceEnvironment, path, capacity);
  if (length > 0 && length < capacity) {
    return true;
  }
  wchar_t temp[MAX_PATH]{};
  if (GetTempPathW(MAX_PATH, temp) == 0) {
    return false;
  }
  wchar_t sidecar[MAX_PATH]{};
  swprintf_s(sidecar, L"%svrclient-meccha-observer-%lu.path", temp,
             GetCurrentProcessId());
  const HANDLE file = CreateFileW(sidecar, GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }
  DWORD bytes_read = 0;
  const BOOL ok = ReadFile(file, path, (capacity - 1) * sizeof(wchar_t),
                           &bytes_read, nullptr);
  CloseHandle(file);
  if (!ok || bytes_read < sizeof(wchar_t)) {
    return false;
  }
  path[bytes_read / sizeof(wchar_t)] = L'\0';
  return path[0] != L'\0';
}

std::string fileSha256(const wchar_t* path) {
  const HANDLE file = CreateFileW(path, GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return {};
  }
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD object_length = 0;
  DWORD hash_length = 0;
  DWORD copied = 0;
  std::vector<UCHAR> object;
  std::array<UCHAR, 32> digest{};
  bool ok = BCryptOpenAlgorithmProvider(
      &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0;
  if (ok) {
    ok = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&object_length), sizeof(object_length),
        &copied, 0) == 0 &&
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
        reinterpret_cast<PUCHAR>(&hash_length), sizeof(hash_length),
        &copied, 0) == 0 && hash_length == digest.size();
  }
  if (ok) {
    object.resize(object_length);
    ok = BCryptCreateHash(algorithm, &hash, object.data(), object_length,
                           nullptr, 0, 0) == 0;
  }
  std::array<UCHAR, 64 * 1024> buffer{};
  while (ok) {
    DWORD bytes_read = 0;
    if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                  &bytes_read, nullptr)) {
      ok = false;
      break;
    }
    if (bytes_read == 0) {
      break;
    }
    ok = BCryptHashData(hash, buffer.data(), bytes_read, 0) == 0;
  }
  if (ok) {
    ok = BCryptFinishHash(hash, digest.data(), hash_length, 0) == 0;
  }
  if (hash != nullptr) BCryptDestroyHash(hash);
  if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
  CloseHandle(file);
  if (!ok) {
    return {};
  }
  static constexpr char hex[] = "0123456789abcdef";
  std::string output;
  output.reserve(64);
  for (UCHAR byte : digest) {
    output.push_back(hex[byte >> 4]);
    output.push_back(hex[byte & 0xF]);
  }
  return output;
}

void captureSceneView(
    const ObservationRegisters& registers,
    std::uint32_t thread_id,
    void* context) {
  auto* provider = static_cast<ProviderState*>(context);
  if (provider == nullptr || registers.rdx == 0) {
    return;
  }
  std::array<std::uint8_t, kMeccha24508135SceneViewCaptureSize> local{};
  __try {
    std::memcpy(local.data(), reinterpret_cast<const void*>(registers.rdx),
                local.size());
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    provider->capture_faults.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  const std::uint64_t index =
      provider->write_count.fetch_add(1, std::memory_order_relaxed);
  RawSlot& slot = provider->slots[index % kRingCapacity];
  const std::uint64_t writing = index * 2 + 1;
  slot.guard.store(writing, std::memory_order_release);
  for (std::size_t byte = 0; byte < local.size(); ++byte) {
    slot.bytes[byte].store(local[byte], std::memory_order_relaxed);
  }
  slot.qpc_ticks.store(now.QuadPart, std::memory_order_relaxed);
  slot.thread_id.store(thread_id, std::memory_order_relaxed);
  slot.guard.store(writing + 1, std::memory_order_release);
}

struct EvidenceState {
  std::string observed_hash;
  UnrealViewSeamLocation seam{};
  std::uint64_t raw_samples = 0;
  std::uint64_t decoded_samples = 0;
  std::uint64_t rejected_samples = 0;
  std::uint64_t capture_faults = 0;
  std::uint64_t missing_observations = 0;
  std::uint64_t ambiguous_observations = 0;
  std::uint64_t consecutive_clean_captures = 0;
  std::uint64_t validation_rejected_samples = 0;
  std::uint64_t validation_capture_faults = 0;
  std::uint64_t validation_missing_observations = 0;
  std::uint64_t validation_ambiguous_observations = 0;
  std::size_t recent_candidate_count = 0;
  UnrealCameraObservationSnapshot camera{};
  UnrealSceneViewIdentity identity{};
  std::array<std::uint8_t, kMeccha24508135SceneViewCaptureSize> latest_raw{};
  bool has_raw = false;
};

void writeEvidence(const EvidenceState& evidence) {
  wchar_t path[32768]{};
  if (!resolveEvidencePath(path, static_cast<DWORD>(std::size(path)))) {
    return;
  }
  std::wstring temporary(path);
  temporary += L".tmp";
  const auto summary = state->observer.summary();
  const auto provider_status = state->status.load(std::memory_order_acquire);
  const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }
  char json[8192]{};
  const auto& camera = evidence.camera.sample;
  const int length = sprintf_s(json,
      "{\n"
      "  \"schema\":\"vrclient-meccha-camera-observer/1\",\n"
      "  \"pid\":%lu,\n"
      "  \"status\":\"%s\",\n"
      "  \"build_id\":\"%.*s\",\n"
      "  \"expected_sha256\":\"%.*s\",\n"
      "  \"observed_sha256\":\"%s\",\n"
      "  \"signature_verified\":%s,\n"
      "  \"constructor_rva\":%u,\n"
      "  \"direct_call_reference_count\":%llu,\n"
      "  \"breakpoint_installed\":%s,\n"
      "  \"threads_armed\":%llu,\n"
      "  \"hit_count\":%llu,\n"
      "  \"raw_sample_count\":%llu,\n"
      "  \"decoded_sample_count\":%llu,\n"
      "  \"rejected_sample_count\":%llu,\n"
      "  \"capture_fault_count\":%llu,\n"
      "  \"missing_observation_count\":%llu,\n"
      "  \"recent_candidate_count\":%llu,\n"
      "  \"ambiguous_observation_count\":%llu,\n"
      "  \"consecutive_clean_capture_count\":%llu,\n"
      "  \"validation_rejected_sample_count\":%llu,\n"
      "  \"validation_capture_fault_count\":%llu,\n"
      "  \"validation_missing_observation_count\":%llu,\n"
      "  \"validation_ambiguous_observation_count\":%llu,\n"
      "  \"camera_ready\":%s,\n"
      "  \"camera_id\":%llu,\n"
      "  \"sample_index\":%llu,\n"
      "  \"sample_time_ns\":%lld,\n"
      "  \"location_uu\":[%.9g,%.9g,%.9g],\n"
      "  \"rotation_degrees\":[%.9g,%.9g,%.9g],\n"
      "  \"vertical_fov_degrees\":%.9g,\n"
      "  \"aspect_ratio\":%.9g,\n"
      "  \"near_clip_uu\":%.9g,\n"
      "  \"far_clip_uu\":%.9g,\n"
      "  \"scene_view_state\":%llu,\n"
      "  \"view_family\":%llu,\n"
      "  \"actor\":%llu,\n"
      "  \"player_index\":%d,\n"
      "  \"stereo_pass\":%d\n"
      "}\n",
      GetCurrentProcessId(), statusName(provider_status),
      static_cast<int>(mecchaCameraBuildId().size()), mecchaCameraBuildId().data(),
      static_cast<int>(mecchaCameraExecutableSha256().size()),
      mecchaCameraExecutableSha256().data(), evidence.observed_hash.c_str(),
      evidence.seam.result == UnrealViewSeamLocateResult::Ready ? "true" : "false",
      evidence.seam.constructor_rva,
      static_cast<unsigned long long>(evidence.seam.direct_call_reference_count),
      summary.installed ? "true" : "false",
      static_cast<unsigned long long>(summary.threads_armed),
      static_cast<unsigned long long>(summary.hit_count),
      static_cast<unsigned long long>(evidence.raw_samples),
      static_cast<unsigned long long>(evidence.decoded_samples),
      static_cast<unsigned long long>(evidence.rejected_samples),
      static_cast<unsigned long long>(evidence.capture_faults),
      static_cast<unsigned long long>(evidence.missing_observations),
      static_cast<unsigned long long>(evidence.recent_candidate_count),
      static_cast<unsigned long long>(evidence.ambiguous_observations),
      static_cast<unsigned long long>(evidence.consecutive_clean_captures),
      static_cast<unsigned long long>(evidence.validation_rejected_samples),
      static_cast<unsigned long long>(evidence.validation_capture_faults),
      static_cast<unsigned long long>(evidence.validation_missing_observations),
      static_cast<unsigned long long>(evidence.validation_ambiguous_observations),
      evidence.camera.ready ? "true" : "false",
      static_cast<unsigned long long>(camera.camera_id),
      static_cast<unsigned long long>(camera.sample_index),
      static_cast<long long>(camera.sample_time_ns),
      camera.location_uu.x, camera.location_uu.y, camera.location_uu.z,
      camera.rotation_degrees.x, camera.rotation_degrees.y,
      camera.rotation_degrees.z, camera.vertical_fov_degrees,
      camera.aspect_ratio, camera.near_clip_uu, camera.far_clip_uu,
      static_cast<unsigned long long>(evidence.identity.scene_view_state),
      static_cast<unsigned long long>(evidence.identity.view_family),
      static_cast<unsigned long long>(evidence.identity.actor),
      evidence.identity.player_index, evidence.identity.stereo_pass);
  if (length > 0) {
    DWORD written = 0;
    WriteFile(file, json, static_cast<DWORD>(length), &written, nullptr);
    FlushFileBuffers(file);
  }
  CloseHandle(file);
  if (!MoveFileExW(temporary.c_str(), path,
      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(temporary.c_str());
  }
  if (evidence.has_raw) {
    std::wstring raw_path(path);
    raw_path += L".raw.bin";
    const HANDLE raw_file = CreateFileW(raw_path.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (raw_file != INVALID_HANDLE_VALUE) {
      DWORD written = 0;
      WriteFile(raw_file, evidence.latest_raw.data(),
                static_cast<DWORD>(evidence.latest_raw.size()), &written, nullptr);
      FlushFileBuffers(raw_file);
      CloseHandle(raw_file);
    }
  }
}

std::int64_t qpcToNanoseconds(std::int64_t ticks, std::int64_t frequency) {
  const long double nanoseconds =
      static_cast<long double>(ticks) * 1'000'000'000.0L /
      static_cast<long double>(frequency);
  return static_cast<std::int64_t>(nanoseconds);
}

DWORD WINAPI runProvider(void*) {
  EvidenceState evidence{};
  wchar_t executable[32768]{};
  const DWORD path_length = GetModuleFileNameW(
      nullptr, executable, static_cast<DWORD>(std::size(executable)));
  if (path_length == 0 || path_length >= std::size(executable)) {
    state->status.store(ProviderStatus::HashFailed, std::memory_order_release);
    writeEvidence(evidence);
    return 1;
  }
  evidence.observed_hash = fileSha256(executable);
  if (evidence.observed_hash != mecchaCameraExecutableSha256()) {
    state->status.store(ProviderStatus::HashFailed, std::memory_order_release);
    writeEvidence(evidence);
    return 2;
  }
  UnrealViewSeamImage image{};
  std::string pe_error;
  if (loadUnrealViewSeamImageFromFile(executable, &image, &pe_error) !=
      UnrealViewSeamPeResult::Ready) {
    state->status.store(ProviderStatus::PeFailed, std::memory_order_release);
    writeEvidence(evidence);
    return 3;
  }
  evidence.seam = validatePinnedUnrealViewConstructor(
      image, mecchaCameraExecutableSha256(), evidence.observed_hash,
      mecchaCameraViewSeam());
  if (evidence.seam.result != UnrealViewSeamLocateResult::Ready) {
    state->status.store(ProviderStatus::PinRejected, std::memory_order_release);
    writeEvidence(evidence);
    return 4;
  }
  const auto module_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
  const void* constructor = reinterpret_cast<const void*>(
      module_base + evidence.seam.constructor_rva);
  if (!state->observer.install(constructor, HwBreakSlot::Dr0, nullptr,
                               &captureSceneView, state)) {
    state->status.store(ProviderStatus::BreakpointFailed, std::memory_order_release);
    writeEvidence(evidence);
    return 5;
  }
  state->status.store(ProviderStatus::Observing, std::memory_order_release);

  LARGE_INTEGER frequency{};
  QueryPerformanceFrequency(&frequency);
  UnrealCameraObserver camera_observer(3, 250'000'000);
  std::map<std::uint64_t, UnrealCameraSample> recent;
  std::uint64_t validation_rejected_baseline = 0;
  std::uint64_t validation_capture_fault_baseline = 0;
  std::uint64_t validation_missing_baseline = 0;
  std::uint64_t validation_ambiguous_baseline = 0;
  const auto updateValidationCounts = [&]() {
    evidence.validation_rejected_samples =
        evidence.rejected_samples - validation_rejected_baseline;
    evidence.validation_capture_faults =
        evidence.capture_faults - validation_capture_fault_baseline;
    evidence.validation_missing_observations =
        evidence.missing_observations - validation_missing_baseline;
    evidence.validation_ambiguous_observations =
        evidence.ambiguous_observations - validation_ambiguous_baseline;
  };
  const auto resetCleanCapture = [&]() {
    evidence.consecutive_clean_captures = 0;
    recent.clear();
    evidence.recent_candidate_count = 0;
    camera_observer.reset();
    evidence.camera = camera_observer.snapshot();
    updateValidationCounts();
  };
  const auto beginOrContinueCleanCapture = [&]() {
    if (evidence.consecutive_clean_captures == 0) {
      validation_rejected_baseline = evidence.rejected_samples;
      validation_capture_fault_baseline = evidence.capture_faults;
      validation_missing_baseline = evidence.missing_observations;
      validation_ambiguous_baseline = evidence.ambiguous_observations;
    }
    ++evidence.consecutive_clean_captures;
    updateValidationCounts();
  };
  const auto synchronizeCaptureFaults = [&]() {
    const auto observed = state->capture_faults.load(std::memory_order_acquire);
    if (observed != evidence.capture_faults) {
      evidence.capture_faults = observed;
      resetCleanCapture();
    }
  };
  std::uint64_t read_index = 0;
  for (;;) {
    synchronizeCaptureFaults();
    const std::uint64_t published = state->write_count.load(std::memory_order_acquire);
    if (published > read_index + kRingCapacity) {
      evidence.rejected_samples += published - read_index - kRingCapacity;
      read_index = published - kRingCapacity;
      resetCleanCapture();
    }
    while (read_index < published) {
      RawSlot& slot = state->slots[read_index % kRingCapacity];
      const std::uint64_t expected_guard = read_index * 2 + 2;
      const std::uint64_t before = slot.guard.load(std::memory_order_acquire);
      std::array<std::uint8_t, kMeccha24508135SceneViewCaptureSize> raw{};
      const auto ticks = slot.qpc_ticks.load(std::memory_order_relaxed);
      for (std::size_t byte = 0; byte < raw.size(); ++byte) {
        raw[byte] = slot.bytes[byte].load(std::memory_order_relaxed);
      }
      const std::uint64_t after = slot.guard.load(std::memory_order_acquire);
      ++read_index;
      ++evidence.raw_samples;
      if (before != expected_guard || after != expected_guard) {
        ++evidence.rejected_samples;
        resetCleanCapture();
        continue;
      }
      evidence.latest_raw = raw;
      evidence.has_raw = true;
      UnrealCameraSample camera{};
      UnrealSceneViewIdentity identity{};
      const std::int64_t sample_time = qpcToNanoseconds(ticks, frequency.QuadPart);
      if (decodeMeccha24508135SceneViewInitOptions(
              raw.data(), raw.size(), read_index, sample_time,
              &camera, &identity) != UnrealSceneViewDecodeResult::Ready) {
        ++evidence.rejected_samples;
        resetCleanCapture();
        continue;
      }
      ++evidence.decoded_samples;
      evidence.identity = identity;
      recent[camera.camera_id] = camera;
      for (auto iterator = recent.begin(); iterator != recent.end();) {
        if (sample_time - iterator->second.sample_time_ns > 50'000'000) {
          iterator = recent.erase(iterator);
        } else {
          ++iterator;
        }
      }
      std::vector<UnrealCameraSample> candidates;
      candidates.reserve(recent.size());
      for (const auto& [id, sample] : recent) {
        (void)id;
        candidates.push_back(sample);
      }
      evidence.recent_candidate_count = candidates.size();
      const auto observation = camera_observer.observe(candidates, sample_time);
      if (observation == UnrealCameraObservationResult::Ambiguous) {
        ++evidence.ambiguous_observations;
        resetCleanCapture();
        continue;
      }
      if (observation == UnrealCameraObservationResult::Missing) {
        ++evidence.missing_observations;
        resetCleanCapture();
        continue;
      }
      if (observation != UnrealCameraObservationResult::Pending &&
          observation != UnrealCameraObservationResult::Ready) {
        ++evidence.rejected_samples;
        resetCleanCapture();
        continue;
      }
      beginOrContinueCleanCapture();
      evidence.camera = camera_observer.snapshot();
    }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const auto now_ns = qpcToNanoseconds(now.QuadPart, frequency.QuadPart);
    for (auto iterator = recent.begin(); iterator != recent.end();) {
      if (now_ns - iterator->second.sample_time_ns > 50'000'000) {
        iterator = recent.erase(iterator);
      } else {
        ++iterator;
      }
    }
    evidence.recent_candidate_count = recent.size();
    if (recent.empty() && evidence.consecutive_clean_captures > 0) {
      ++evidence.missing_observations;
      resetCleanCapture();
    }
    synchronizeCaptureFaults();
    updateValidationCounts();
    writeEvidence(evidence);
    Sleep(250);
  }
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(module);
    const HANDLE worker = CreateThread(nullptr, 0, &runProvider, nullptr, 0, nullptr);
    if (worker != nullptr) {
      CloseHandle(worker);
    }
  }
  return TRUE;
}

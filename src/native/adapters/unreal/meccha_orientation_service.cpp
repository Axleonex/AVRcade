#include "adapters/unreal/meccha_orientation_service.h"

#include <windows.h>
#include <bcrypt.h>

#include "adapters/unreal/meccha_camera_pin.h"
#include "adapters/unreal/meccha_detour_transaction.h"
#include "adapters/unreal/meccha_orientation_tracking_gate.h"
#include "adapters/unreal/meccha_camera_identity_gate.h"
#include "adapters/unreal/meccha_stereo_pose_pair.h"
#include "adapters/unreal/meccha_stereo_projection.h"
#include "adapters/unreal/unreal_orientation_only.h"
#include "adapters/unreal/unreal_scene_view_decoder.h"
#include "adapters/unreal/unreal_view_seam_locator.h"
#include "adapters/unreal/unreal_view_seam_pe.h"

#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace vrclient::adapters::unreal {

namespace {

constexpr std::uint64_t kStableCameraSamples = 30;
constexpr std::size_t kRequiredFreshPoseSamples = 60;
constexpr std::int64_t kMaximumPoseAgeNs = 100'000'000;
constexpr std::int64_t kMaximumCameraObservationGapNs = 100'000'000;
constexpr std::int64_t kRecentCameraIdentityWindowNs = 100'000'000;
constexpr std::size_t kViewMatrixOffset = 0x20;
constexpr int kPoseSnapshotReadAttempts = 64;
constexpr float kStereoSourceVerticalFovDegrees = 110.0F;
constexpr float kStereoSourceVerticalFovToleranceDegrees = 2.0F;
constexpr float kStereoSourceProjectionAspectRatio = 1.0F;
constexpr float kStereoSourceProjectionAspectTolerance = 0.01F;

using FSceneViewConstructorFn = void*(__fastcall*)(void*, const void*);
FSceneViewConstructorFn realFSceneViewConstructor = nullptr;

struct AtomicPoseSnapshot {
  std::atomic<std::uint64_t> sequence{0};
  std::array<std::atomic<std::uint64_t>, 9> delta{};
  std::atomic<std::uint64_t> frame_index{0};
  std::atomic<std::int64_t> publish_time_ns{0};
  std::atomic<bool> valid{false};
};

struct OrientationState {
  std::atomic<MecchaOrientationStatus> status{MecchaOrientationStatus::Starting};
  std::atomic<bool> signature_verified{false};
  std::atomic<bool> hook_installed{false};
  std::atomic<std::uint32_t> constructor_rva{0};
  std::atomic<std::uint64_t> constructor_hits{0};
  std::atomic<std::uint64_t> decoded_cameras{0};
  std::atomic<std::uint64_t> rejected_cameras{0};
  std::atomic<std::uint64_t> stable_camera_id{0};
  std::atomic<std::uint64_t> secondary_stable_camera_id{0};
  std::atomic<std::uint32_t> observed_vertical_fov_bits{0};
  std::atomic<std::uint32_t> observed_aspect_ratio_bits{0};
  std::atomic<std::uint64_t> pose_publish_count{0};
  std::atomic<std::uint64_t> orientation_apply_count{0};
  std::atomic<std::uint64_t> consecutive_applies{0};
  std::atomic<std::uint64_t> missing_pose_skips{0};
  std::atomic<std::uint64_t> stale_pose_skips{0};
  std::atomic<std::uint64_t> write_rejects{0};
  std::atomic<std::uint64_t> stereo_render_generation{0};
  AtomicPoseSnapshot pose{};
  MecchaOrientationTelemetryBuffer telemetry{};
  SRWLOCK camera_lock = SRWLOCK_INIT;
  MecchaCameraIdentityGate camera_identity_gate{
      kStableCameraSamples,
      kMaximumCameraObservationGapNs,
      kRecentCameraIdentityWindowNs};
  MecchaStereoCameraIdentityGate stereo_camera_identity_gate{
      kStableCameraSamples,
      kMaximumCameraObservationGapNs,
      kRecentCameraIdentityWindowNs};
  MecchaStereoPosePairCoordinator stereo_pose_pair{};
  MecchaStereoPosePairSelection last_completed_stereo_pair{};
  MecchaOrientationTrackingState tracking_state{
      kRequiredFreshPoseSamples, kMaximumPoseAgeNs};
  bool reference_valid = false;
  VrRuntimePose reference_pose{};
};

OrientationState* state = new OrientationState();

void observeTrackingPose(bool valid, std::int64_t publish_time_ns) {
  state->tracking_state.publishPose(valid, publish_time_ns);
}

void latchTrackingFault() {
  state->tracking_state.latchFault();
}

std::int64_t monotonicNowNs() {
  LARGE_INTEGER counter{};
  LARGE_INTEGER frequency{};
  if (!QueryPerformanceCounter(&counter) ||
      !QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
    return 0;
  }
  return static_cast<std::int64_t>(
      static_cast<long double>(counter.QuadPart) * 1'000'000'000.0L /
      static_cast<long double>(frequency.QuadPart));
}

std::string fileSha256(HANDLE file) {
  if (file == nullptr || file == INVALID_HANDLE_VALUE) {
    return {};
  }
  LARGE_INTEGER beginning{};
  if (!SetFilePointerEx(file, beginning, nullptr, FILE_BEGIN)) {
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
    ok = BCryptGetProperty(
             algorithm, BCRYPT_OBJECT_LENGTH,
             reinterpret_cast<PUCHAR>(&object_length), sizeof(object_length),
             &copied, 0) == 0 &&
        BCryptGetProperty(
             algorithm, BCRYPT_HASH_LENGTH,
             reinterpret_cast<PUCHAR>(&hash_length), sizeof(hash_length),
             &copied, 0) == 0 &&
        hash_length == digest.size();
  }
  if (ok) {
    object.resize(object_length);
    ok = BCryptCreateHash(
             algorithm, &hash, object.data(), object_length, nullptr, 0, 0) == 0;
  }
  std::array<UCHAR, 64 * 1024> buffer{};
  while (ok) {
    DWORD bytes_read = 0;
    if (!ReadFile(
            file, buffer.data(), static_cast<DWORD>(buffer.size()),
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

bool loadedModuleMatchesPinnedPrologue(
    HMODULE module,
    std::uint32_t constructor_rva,
    const std::uint8_t* expected,
    std::size_t expected_size) {
  if (module == nullptr || expected == nullptr || expected_size == 0) {
    return false;
  }
  const auto base = reinterpret_cast<std::uintptr_t>(module);
  const auto address = base + constructor_rva;
  if (address < base || expected_size > UINTPTR_MAX - address) {
    return false;
  }
  MEMORY_BASIC_INFORMATION info{};
  if (VirtualQuery(
          reinterpret_cast<const void*>(address), &info, sizeof(info)) !=
          sizeof(info) ||
      info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
    return false;
  }
  const auto region_end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) +
      info.RegionSize;
  if (region_end < address || expected_size > region_end - address) {
    return false;
  }
  __try {
    return std::memcmp(
               reinterpret_cast<const void*>(address), expected,
               expected_size) == 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

void publishInvalidPose(
    std::uint64_t frame_index,
    std::int64_t publish_time_ns,
    bool sampler_active,
    VrRuntimeResult sampler_result,
    std::uint64_t sampler_attempt_count,
    std::uint64_t sampler_success_count) {
  auto& pose = state->pose;
  pose.sequence.fetch_add(1, std::memory_order_acq_rel);
  pose.frame_index.store(frame_index, std::memory_order_relaxed);
  pose.publish_time_ns.store(publish_time_ns, std::memory_order_relaxed);
  pose.valid.store(false, std::memory_order_relaxed);
  pose.sequence.fetch_add(1, std::memory_order_release);
  MecchaOrientationTelemetrySample telemetry{};
  telemetry.pose.frame_index = frame_index;
  telemetry.pose.publish_time_ns = publish_time_ns;
  telemetry.pose.valid = false;
  telemetry.pose_publish_count =
      state->pose_publish_count.load(std::memory_order_relaxed);
  telemetry.sampler_active = sampler_active;
  telemetry.sampler_result = sampler_result;
  telemetry.sampler_attempt_count = sampler_attempt_count;
  telemetry.sampler_success_count = sampler_success_count;
  state->telemetry.publish(telemetry);
  observeTrackingPose(false, publish_time_ns);
  state->consecutive_applies.store(0, std::memory_order_release);
}

void publishPose(
    const UnrealOrientationPoseSample& sample,
    bool sampler_active,
    VrRuntimeResult sampler_result,
    std::uint64_t sampler_attempt_count,
    std::uint64_t sampler_success_count) {
  auto& pose = state->pose;
  pose.sequence.fetch_add(1, std::memory_order_acq_rel);
  for (std::size_t index = 0; index < sample.unreal_delta.values.size();
       ++index) {
    pose.delta[index].store(
        std::bit_cast<std::uint64_t>(sample.unreal_delta.values[index]),
        std::memory_order_relaxed);
  }
  pose.frame_index.store(sample.frame_index, std::memory_order_relaxed);
  pose.publish_time_ns.store(sample.publish_time_ns, std::memory_order_relaxed);
  pose.valid.store(sample.valid, std::memory_order_relaxed);
  pose.sequence.fetch_add(1, std::memory_order_release);
  MecchaOrientationTelemetrySample telemetry{};
  telemetry.pose = sample;
  telemetry.pose_publish_count =
      state->pose_publish_count.fetch_add(1, std::memory_order_relaxed) + 1;
  telemetry.sampler_active = sampler_active;
  telemetry.sampler_result = sampler_result;
  telemetry.sampler_attempt_count = sampler_attempt_count;
  telemetry.sampler_success_count = sampler_success_count;
  state->telemetry.publish(telemetry);
  observeTrackingPose(sample.valid, sample.publish_time_ns);
}

bool readPose(UnrealOrientationPoseSample* output) {
  if (output == nullptr) {
    return false;
  }
  auto& pose = state->pose;
  for (int attempt = 0; attempt < kPoseSnapshotReadAttempts; ++attempt) {
    const std::uint64_t before = pose.sequence.load(std::memory_order_acquire);
    if ((before & 1) != 0) {
      YieldProcessor();
      continue;
    }
    UnrealOrientationPoseSample sample{};
    for (std::size_t index = 0; index < sample.unreal_delta.values.size();
         ++index) {
      sample.unreal_delta.values[index] = std::bit_cast<double>(
          pose.delta[index].load(std::memory_order_relaxed));
    }
    sample.frame_index = pose.frame_index.load(std::memory_order_relaxed);
    sample.publish_time_ns =
        pose.publish_time_ns.load(std::memory_order_relaxed);
    sample.valid = pose.valid.load(std::memory_order_relaxed);
    const std::uint64_t after = pose.sequence.load(std::memory_order_acquire);
    if (before == after) {
      *output = sample;
      return true;
    }
    YieldProcessor();
  }
  return false;
}

bool writableRange(void* address, std::size_t size) {
  if (address == nullptr || size == 0) {
    return false;
  }
  auto cursor = reinterpret_cast<std::uintptr_t>(address);
  const auto end = cursor + size;
  if (end < cursor) {
    return false;
  }
  while (cursor < end) {
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(
            reinterpret_cast<const void*>(cursor), &info, sizeof(info)) !=
        sizeof(info)) {
      return false;
    }
    const DWORD protection = info.Protect & 0xFF;
    const bool writable =
        protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
        protection == PAGE_EXECUTE_READWRITE ||
        protection == PAGE_EXECUTE_WRITECOPY;
    if (info.State != MEM_COMMIT || !writable ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
      return false;
    }
    const auto region_end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) +
        info.RegionSize;
    if (region_end <= cursor) {
      return false;
    }
    cursor = region_end;
  }
  return true;
}

void applyOrientationOnly(void* options) {
  const auto hit =
      state->constructor_hits.fetch_add(1, std::memory_order_relaxed) + 1;
  if (options == nullptr) {
    state->rejected_cameras.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const std::int64_t now_ns = monotonicNowNs();
  std::array<std::uint8_t, kMeccha24508135SceneViewCaptureSize> raw{};
  __try {
    std::memcpy(raw.data(), options, raw.size());
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    state->rejected_cameras.fetch_add(1, std::memory_order_relaxed);
    state->consecutive_applies.store(0, std::memory_order_release);
    return;
  }
  UnrealCameraSample camera{};
  UnrealSceneViewIdentity identity{};
  if (decodeMeccha24508135SceneViewInitOptions(
          raw.data(), raw.size(), hit, now_ns, &camera, &identity) !=
      UnrealSceneViewDecodeResult::Ready) {
    state->rejected_cameras.fetch_add(1, std::memory_order_relaxed);
    state->consecutive_applies.store(0, std::memory_order_release);
    return;
  }
  state->decoded_cameras.fetch_add(1, std::memory_order_relaxed);
  state->observed_vertical_fov_bits.store(
      std::bit_cast<std::uint32_t>(camera.vertical_fov_degrees),
      std::memory_order_release);
  state->observed_aspect_ratio_bits.store(
      std::bit_cast<std::uint32_t>(camera.aspect_ratio),
      std::memory_order_release);

  AcquireSRWLockExclusive(&state->camera_lock);
#if defined(VRCLIENT_MECCHA_STEREO_RELAY)
  const auto identity_result = state->stereo_camera_identity_gate.observe(
      identity.scene_view_state, now_ns);
  const auto stable_pair =
      state->stereo_camera_identity_gate.stableCameraIds();
  const std::uint64_t stable_camera = stable_pair[0];
  const std::uint64_t secondary_stable_camera = stable_pair[1];
  const bool camera_identity_may_write =
      identity_result == MecchaCameraIdentityResult::Ready &&
      state->stereo_camera_identity_gate.allows(
          identity.scene_view_state, now_ns);
#else
  const auto identity_result = state->camera_identity_gate.observe(
      identity.scene_view_state, now_ns);
  const std::uint64_t stable_camera =
      state->camera_identity_gate.stableCameraId();
  constexpr std::uint64_t secondary_stable_camera = 0;
  const bool camera_identity_may_write =
      identity_result == MecchaCameraIdentityResult::Ready &&
      state->camera_identity_gate.allows(identity.scene_view_state, now_ns);
#endif
  state->stable_camera_id.store(stable_camera, std::memory_order_release);
  state->secondary_stable_camera_id.store(
      secondary_stable_camera, std::memory_order_release);
  ReleaseSRWLockExclusive(&state->camera_lock);
  if (!camera_identity_may_write
#if !defined(VRCLIENT_MECCHA_STEREO_RELAY)
      || stable_camera != identity.scene_view_state
#endif
  ) {
    state->consecutive_applies.store(0, std::memory_order_release);
    return;
  }

#if defined(VRCLIENT_MECCHA_STEREO_RELAY)
  // The packaged game ignores the startup config overrides for its fake-stereo
  // device. Normalize the angular terms at the exact, already-pinned per-view
  // seam instead. First transform and decode a private copy; only a known stock
  // or already-correct projection for one of the two stable eye identities is
  // eligible for the two live double writes below.
  auto normalized_raw = raw;
  const auto normalized_result = normalizeMeccha24517175StereoProjection(
      normalized_raw.data(), normalized_raw.size());
  if (normalized_result != MecchaStereoProjectionNormalizeResult::Ready &&
      normalized_result !=
          MecchaStereoProjectionNormalizeResult::AlreadyNormalized) {
    state->write_rejects.fetch_add(1, std::memory_order_relaxed);
    state->consecutive_applies.store(0, std::memory_order_release);
    return;
  }
  UnrealCameraSample normalized_camera{};
  UnrealSceneViewIdentity normalized_identity{};
  if (decodeMeccha24508135SceneViewInitOptions(
          normalized_raw.data(), normalized_raw.size(), hit, now_ns,
          &normalized_camera, &normalized_identity) !=
          UnrealSceneViewDecodeResult::Ready ||
      normalized_identity.view_family != identity.view_family ||
      normalized_identity.scene_view_state != identity.scene_view_state ||
      !mecchaStereoProjectionMatches(
          normalized_camera.vertical_fov_degrees,
          normalized_camera.aspect_ratio,
          kStereoSourceVerticalFovDegrees,
          kStereoSourceProjectionAspectRatio,
          kStereoSourceVerticalFovToleranceDegrees,
          kStereoSourceProjectionAspectTolerance)) {
    state->write_rejects.fetch_add(1, std::memory_order_relaxed);
    state->consecutive_applies.store(0, std::memory_order_release);
    return;
  }
  constexpr std::size_t kProjectionScaleXOffset = 0xA0;
  constexpr std::size_t kProjectionScaleYOffset =
      0xA0 + 5 * sizeof(double);
  if (!writableRange(
          static_cast<std::uint8_t*>(options) + kProjectionScaleXOffset,
          sizeof(double)) ||
      !writableRange(
          static_cast<std::uint8_t*>(options) + kProjectionScaleYOffset,
          sizeof(double))) {
    state->write_rejects.fetch_add(1, std::memory_order_relaxed);
    state->consecutive_applies.store(0, std::memory_order_release);
    return;
  }
  auto live_normalize_result =
      MecchaStereoProjectionNormalizeResult::InvalidInput;
  __try {
    live_normalize_result = normalizeMeccha24517175StereoProjection(
        options, kMeccha24508135SceneViewCaptureSize);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    live_normalize_result =
        MecchaStereoProjectionNormalizeResult::InvalidInput;
  }
  if (live_normalize_result != MecchaStereoProjectionNormalizeResult::Ready &&
      live_normalize_result !=
          MecchaStereoProjectionNormalizeResult::AlreadyNormalized) {
    state->write_rejects.fetch_add(1, std::memory_order_relaxed);
    state->consecutive_applies.store(0, std::memory_order_release);
    return;
  }
  camera = normalized_camera;
  state->observed_vertical_fov_bits.store(
      std::bit_cast<std::uint32_t>(camera.vertical_fov_degrees),
      std::memory_order_release);
  state->observed_aspect_ratio_bits.store(
      std::bit_cast<std::uint32_t>(camera.aspect_ratio),
      std::memory_order_release);
#endif

  UnrealOrientationPoseSample pose{};
  bool pose_snapshot_read = false;
  bool structural_pose_snapshot_failure = false;
  std::int64_t pose_validation_time_ns = now_ns;
#if defined(VRCLIENT_MECCHA_STEREO_RELAY)
  // Pose publication is asynchronous to UE's two FSceneView constructor
  // calls. Select under the same lock that owns the stable identity pair so
  // the first eye latches one generation and the other eye must reuse it.
  AcquireSRWLockExclusive(&state->camera_lock);
  const auto current_stable_pair =
      state->stereo_camera_identity_gate.stableCameraIds();
  const bool pair_identity_still_valid =
      current_stable_pair == stable_pair &&
      state->stereo_camera_identity_gate.allows(
          identity.scene_view_state, now_ns);
  UnrealOrientationPoseSample latest_pose{};
  pose_snapshot_read = pair_identity_still_valid && readPose(&latest_pose);
  MecchaStereoPosePairSelection pair_selection{};
  MecchaStereoPosePairResult pair_result =
      MecchaStereoPosePairResult::InvalidIdentity;
  if (pair_identity_still_valid) {
    pair_result = state->stereo_pose_pair.select(
        identity.scene_view_state, current_stable_pair,
        state->stereo_render_generation.load(std::memory_order_acquire),
        camera.location_uu, now_ns, latest_pose,
        state->tracking_state.ready() &&
            !state->tracking_state.faultLatched(),
        &pair_selection);
    if (pair_result == MecchaStereoPosePairResult::ReadyFirstEye ||
        pair_result == MecchaStereoPosePairResult::ReadyPairComplete ||
        pair_result == MecchaStereoPosePairResult::ReadyDuplicateEye) {
      pose = pair_selection.pose;
    }
    if (pair_result == MecchaStereoPosePairResult::ReadyPairComplete) {
      state->last_completed_stereo_pair = pair_selection;
    }
  }
  ReleaseSRWLockExclusive(&state->camera_lock);
  if (!pair_identity_still_valid) {
    state->consecutive_applies.store(0, std::memory_order_release);
    return;
  }
  const bool pair_result_ready =
      pair_result == MecchaStereoPosePairResult::ReadyFirstEye ||
      pair_result == MecchaStereoPosePairResult::ReadyPairComplete ||
      pair_result == MecchaStereoPosePairResult::ReadyDuplicateEye;
  if (!pair_result_ready) {
    structural_pose_snapshot_failure = !pose_snapshot_read;
    if (!structural_pose_snapshot_failure) {
      state->write_rejects.fetch_add(1, std::memory_order_relaxed);
    }
    state->consecutive_applies.store(0, std::memory_order_release);
  } else if (!pair_selection.write_enabled) {
    state->consecutive_applies.store(0, std::memory_order_release);
    return;
  } else {
    pose_validation_time_ns = pair_selection.pose_validation_time_ns;
  }
  if (!pair_result_ready && !structural_pose_snapshot_failure) {
    return;
  }
#else
  pose_snapshot_read = readPose(&pose);
  structural_pose_snapshot_failure = !pose_snapshot_read;
#endif
  if (structural_pose_snapshot_failure) {
    latchTrackingFault();
    state->missing_pose_skips.fetch_add(1, std::memory_order_relaxed);
    state->consecutive_applies.store(0, std::memory_order_release);
    return;
  }
#if !defined(VRCLIENT_MECCHA_STEREO_RELAY)
  if (state->tracking_state.faultLatched()) {
    state->consecutive_applies.store(0, std::memory_order_release);
    return;
  }
  if (!state->tracking_state.ready()) {
    state->consecutive_applies.store(0, std::memory_order_release);
    return;
  }
#endif
  if (!writableRange(
          static_cast<std::uint8_t*>(options) + kViewMatrixOffset,
          12 * sizeof(double))) {
    state->write_rejects.fetch_add(1, std::memory_order_relaxed);
    state->consecutive_applies.store(0, std::memory_order_release);
    return;
  }
  UnrealOrientationPoseResult result =
      UnrealOrientationPoseResult::InvalidSceneView;
  __try {
    result = applyMeccha24508135OrientationOnlyToSceneViewInitOptions(
        options, kMeccha24508135SceneViewCaptureSize, pose,
        pose_validation_time_ns,
        kMaximumPoseAgeNs);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    result = UnrealOrientationPoseResult::InvalidSceneView;
  }
  if (result == UnrealOrientationPoseResult::MissingOrientation) {
    state->missing_pose_skips.fetch_add(1, std::memory_order_relaxed);
    state->consecutive_applies.store(0, std::memory_order_release);
    return;
  }
  if (result == UnrealOrientationPoseResult::StalePose ||
      result == UnrealOrientationPoseResult::FuturePose) {
    state->stale_pose_skips.fetch_add(1, std::memory_order_relaxed);
    state->consecutive_applies.store(0, std::memory_order_release);
    return;
  }
  if (result != UnrealOrientationPoseResult::Ready) {
    state->write_rejects.fetch_add(1, std::memory_order_relaxed);
    state->consecutive_applies.store(0, std::memory_order_release);
    return;
  }
  state->orientation_apply_count.fetch_add(1, std::memory_order_relaxed);
  state->consecutive_applies.fetch_add(1, std::memory_order_release);
}

void* __fastcall hookedFSceneViewConstructor(void* self, const void* options) {
  applyOrientationOnly(const_cast<void*>(options));
  return realFSceneViewConstructor(self, options);
}

}  // namespace

const char* mecchaOrientationStatusName(MecchaOrientationStatus status) {
  switch (status) {
    case MecchaOrientationStatus::Starting: return "starting";
    case MecchaOrientationStatus::HashFailed: return "hash_failed";
    case MecchaOrientationStatus::PeFailed: return "pe_failed";
    case MecchaOrientationStatus::PinRejected: return "pin_rejected";
    case MecchaOrientationStatus::HookFailed: return "hook_failed";
    case MecchaOrientationStatus::Ready: return "ready";
  }
  return "unknown";
}

bool installMecchaOrientationOnlyHook() {
  wchar_t executable[32768]{};
  const DWORD path_length = GetModuleFileNameW(
      nullptr, executable, static_cast<DWORD>(std::size(executable)));
  if (path_length == 0 || path_length >= std::size(executable)) {
    state->status.store(MecchaOrientationStatus::HashFailed);
    return false;
  }
  // Deny writes and deletes until every disk check, the loaded-image prologue
  // check, and the detour transaction have completed. This pins the pathname to
  // one immutable file identity across the otherwise separate PE reader open.
  const HANDLE executable_file = CreateFileW(
      executable, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (executable_file == INVALID_HANDLE_VALUE ||
      fileSha256(executable_file) != mecchaCameraExecutableSha256()) {
    if (executable_file != INVALID_HANDLE_VALUE) {
      CloseHandle(executable_file);
    }
    state->status.store(MecchaOrientationStatus::HashFailed);
    return false;
  }
  UnrealViewSeamImage image{};
  std::string error;
  if (loadUnrealViewSeamImageFromFile(executable, &image, &error) !=
      UnrealViewSeamPeResult::Ready) {
    CloseHandle(executable_file);
    state->status.store(MecchaOrientationStatus::PeFailed);
    return false;
  }
  const auto& pin = mecchaCameraViewSeam();
  const auto seam = validatePinnedUnrealViewConstructor(
      image, mecchaCameraExecutableSha256(), mecchaCameraExecutableSha256(),
      pin);
  if (seam.result != UnrealViewSeamLocateResult::Ready) {
    CloseHandle(executable_file);
    state->status.store(MecchaOrientationStatus::PinRejected);
    return false;
  }
  const HMODULE module = GetModuleHandleW(nullptr);
  if (!loadedModuleMatchesPinnedPrologue(
          module, seam.constructor_rva, pin.prologue.data(),
          pin.prologue.size())) {
    CloseHandle(executable_file);
    state->status.store(MecchaOrientationStatus::PinRejected);
    return false;
  }
  state->signature_verified.store(true, std::memory_order_release);
  state->constructor_rva.store(seam.constructor_rva, std::memory_order_release);
  realFSceneViewConstructor = reinterpret_cast<FSceneViewConstructorFn>(
      reinterpret_cast<std::uintptr_t>(module) + seam.constructor_rva);
  const MecchaDetourAttachment attachment{
      reinterpret_cast<PVOID*>(&realFSceneViewConstructor),
      reinterpret_cast<PVOID>(hookedFSceneViewConstructor)};
  if (!commitMecchaDetourTransaction(&attachment, 1)) {
    CloseHandle(executable_file);
    state->status.store(MecchaOrientationStatus::HookFailed);
    return false;
  }
  CloseHandle(executable_file);
  state->hook_installed.store(true, std::memory_order_release);
  state->status.store(MecchaOrientationStatus::Ready, std::memory_order_release);
  return true;
}

void publishMecchaOrientationOnlyPoseSample(
    const VrRuntimePose& pose,
    std::uint64_t sample_index,
    bool sampler_active,
    VrRuntimeResult sampler_result,
    std::uint64_t sampler_attempt_count,
    std::uint64_t sampler_success_count) {
  const std::int64_t now_ns = monotonicNowNs();
  if (now_ns <= 0 || pose.orientation_valid == 0) {
    state->reference_valid = false;
    publishInvalidPose(
        sample_index, now_ns, sampler_active, sampler_result,
        sampler_attempt_count, sampler_success_count);
    return;
  }
  if (!state->reference_valid) {
    state->reference_pose = pose;
    state->reference_valid = true;
  }
  UnrealOrientationPoseSample sample{};
  if (makeUnrealOrientationPoseSample(
          state->reference_pose, pose, sample_index, now_ns, &sample) !=
      UnrealOrientationPoseResult::Ready) {
    state->reference_valid = false;
    publishInvalidPose(
        sample_index, now_ns, sampler_active, sampler_result,
        sampler_attempt_count, sampler_success_count);
    return;
  }
  publishPose(
      sample, sampler_active, sampler_result, sampler_attempt_count,
      sampler_success_count);
}

void publishMecchaOrientationOnlyPoseSample(
    const VrRuntimePose& pose, std::uint64_t sample_index) {
  MecchaOrientationTelemetrySample current{};
  state->telemetry.read(&current);
  publishMecchaOrientationOnlyPoseSample(
      pose, sample_index, current.sampler_active, current.sampler_result,
      current.sampler_attempt_count, current.sampler_success_count);
}

void publishMecchaOrientationSamplerState(
    bool sampler_active,
    VrRuntimeResult sampler_result,
    std::uint64_t sampler_attempt_count,
    std::uint64_t sampler_success_count) {
  MecchaOrientationTelemetrySample current{};
  if (!state->telemetry.read(&current)) {
    return;
  }
  current.sampler_active = sampler_active;
  current.sampler_result = sampler_result;
  current.sampler_attempt_count = sampler_attempt_count;
  current.sampler_success_count = sampler_success_count;
  state->telemetry.publish(current);
}

void publishMecchaOrientationOnlyPose(const VrRuntimeFrameData& frame) {
  publishMecchaOrientationOnlyPoseSample(
      frame.head_pose, frame.timing.frame_index);
}

void publishMecchaStereoRenderGeneration(std::uint64_t render_generation) {
  state->stereo_render_generation.store(
      render_generation, std::memory_order_release);
}

MecchaOrientationSnapshot mecchaOrientationSnapshot() {
  MecchaOrientationSnapshot snapshot{};
  snapshot.status = state->status.load(std::memory_order_acquire);
  snapshot.signature_verified =
      state->signature_verified.load(std::memory_order_acquire);
  snapshot.hook_installed = state->hook_installed.load(std::memory_order_acquire);
  snapshot.constructor_rva = state->constructor_rva.load(std::memory_order_acquire);
  snapshot.observed_vertical_fov_degrees = std::bit_cast<float>(
      state->observed_vertical_fov_bits.load(std::memory_order_acquire));
  snapshot.observed_aspect_ratio = std::bit_cast<float>(
      state->observed_aspect_ratio_bits.load(std::memory_order_acquire));
  MecchaOrientationOperationalSources operational_sources{};
  operational_sources.constructor_hits = &state->constructor_hits;
  operational_sources.decoded_cameras = &state->decoded_cameras;
  operational_sources.rejected_cameras = &state->rejected_cameras;
  operational_sources.stable_camera_id = &state->stable_camera_id;
  operational_sources.secondary_stable_camera_id =
      &state->secondary_stable_camera_id;
  operational_sources.orientation_applies = &state->orientation_apply_count;
  operational_sources.consecutive_applies = &state->consecutive_applies;
  operational_sources.missing_pose_skips = &state->missing_pose_skips;
  operational_sources.stale_pose_skips = &state->stale_pose_skips;
  operational_sources.write_rejects = &state->write_rejects;
  operational_sources.load_tracking_ready = [](void* user_data) {
    return static_cast<OrientationState*>(user_data)->tracking_state.ready();
  };
  operational_sources.load_tracking_fault = [](void* user_data) {
    return static_cast<OrientationState*>(user_data)
        ->tracking_state.faultLatched();
  };
  operational_sources.tracking_user_data = state;
  MecchaOrientationOperationalSnapshot operational{};
  snapshot.operational_snapshot_available =
      collectMecchaOrientationOperationalSnapshot(
          operational_sources, &operational);
  if (snapshot.operational_snapshot_available) {
    snapshot.tracking_ready = operational.tracking_ready;
    snapshot.tracking_fault_latched = operational.tracking_fault_latched;
    snapshot.constructor_hit_count = operational.constructor_hit_count;
    snapshot.decoded_camera_count = operational.decoded_camera_count;
    snapshot.rejected_camera_count = operational.rejected_camera_count;
    snapshot.stable_camera_id = operational.stable_camera_id;
    snapshot.secondary_stable_camera_id =
        operational.secondary_stable_camera_id;
    snapshot.orientation_apply_count = operational.orientation_apply_count;
    snapshot.consecutive_orientation_apply_count =
        operational.consecutive_orientation_apply_count;
    snapshot.missing_pose_skip_count = operational.missing_pose_skip_count;
    snapshot.stale_pose_skip_count = operational.stale_pose_skip_count;
    snapshot.write_reject_count = operational.write_reject_count;
  }
  MecchaOrientationTelemetrySample telemetry{};
  snapshot.pose_snapshot_available = state->telemetry.read(&telemetry);
  if (snapshot.pose_snapshot_available) {
    snapshot.pose_sample_valid = telemetry.pose.valid;
    snapshot.pose_publish_count = telemetry.pose_publish_count;
    snapshot.pose_frame_index = telemetry.pose.frame_index;
    snapshot.pose_publish_time_ns = telemetry.pose.publish_time_ns;
    snapshot.pose_unreal_delta = telemetry.pose.unreal_delta.values;
    snapshot.sampler_active = telemetry.sampler_active;
    snapshot.sampler_result = telemetry.sampler_result;
    snapshot.sampler_attempt_count = telemetry.sampler_attempt_count;
    snapshot.sampler_success_count = telemetry.sampler_success_count;
  }
#if defined(VRCLIENT_MECCHA_STEREO_RELAY)
  if (TryAcquireSRWLockShared(&state->camera_lock)) {
    snapshot.stereo_completed_pair_count =
        state->stereo_pose_pair.completedPairCount();
    snapshot.stereo_incomplete_pair_count =
        state->stereo_pose_pair.incompletePairCount();
    snapshot.stereo_duplicate_eye_count =
        state->stereo_pose_pair.duplicateEyeCount();
    if (state->last_completed_stereo_pair.completed_pair_count != 0) {
      const auto& pair = state->last_completed_stereo_pair;
      snapshot.stereo_pair_snapshot_available = true;
      snapshot.stereo_pose_frame_mismatch_count =
          pair.pose_frame_mismatch_count;
      snapshot.stereo_pair_pose_frame_index = pair.pose.frame_index;
      snapshot.stereo_pair_render_generation = pair.render_generation;
      snapshot.stereo_eye_camera_ids = pair.eye_camera_ids;
      snapshot.stereo_eye_locations_uu = pair.eye_locations_uu;
      snapshot.stereo_eye_observation_delta_ns =
          pair.eye_observation_delta_ns;
      snapshot.stereo_eye_separation_uu = pair.eye_separation_uu;
    }
    ReleaseSRWLockShared(&state->camera_lock);
  }
#endif
  return snapshot;
}

}  // namespace vrclient::adapters::unreal

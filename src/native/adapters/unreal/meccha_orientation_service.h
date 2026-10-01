#pragma once

#include "adapters/unreal/unreal_camera_contract.h"
#include "adapters/unreal/unreal_orientation_only.h"
#include "public/vr_runtime_api.h"

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace vrclient::adapters::unreal {

enum class MecchaOrientationStatus : unsigned {
  Starting,
  HashFailed,
  PeFailed,
  PinRejected,
  HookFailed,
  Ready,
};

struct MecchaOrientationTelemetrySample {
  UnrealOrientationPoseSample pose{};
  std::uint64_t pose_publish_count = 0;
  bool sampler_active = false;
  VrRuntimeResult sampler_result = VR_RUNTIME_SKIPPED;
  std::uint64_t sampler_attempt_count = 0;
  std::uint64_t sampler_success_count = 0;
};

using MecchaOrientationTelemetryPublishHook = void (*)(void* user_data);

// Single-writer, bounded-read telemetry buffer. Every field consumed by the
// evidence writer belongs to one sequence; a contended read reports unavailable
// instead of returning a mixed generation. The production instance has no hook.
class MecchaOrientationTelemetryBuffer {
 public:
  explicit MecchaOrientationTelemetryBuffer(
      MecchaOrientationTelemetryPublishHook mid_publish_hook = nullptr,
      void* hook_user_data = nullptr)
      : mid_publish_hook_(mid_publish_hook),
        hook_user_data_(hook_user_data) {}

  void publish(const MecchaOrientationTelemetrySample& sample) {
    sequence_.fetch_add(1, std::memory_order_acq_rel);
    for (std::size_t index = 0;
         index < sample.pose.unreal_delta.values.size(); ++index) {
      delta_[index].store(
          std::bit_cast<std::uint64_t>(
              sample.pose.unreal_delta.values[index]),
          std::memory_order_relaxed);
    }
    frame_index_.store(sample.pose.frame_index, std::memory_order_relaxed);
    publish_time_ns_.store(
        sample.pose.publish_time_ns, std::memory_order_relaxed);
    pose_valid_.store(sample.pose.valid, std::memory_order_relaxed);
    pose_publish_count_.store(sample.pose_publish_count,
                              std::memory_order_relaxed);
    sampler_active_.store(sample.sampler_active, std::memory_order_relaxed);
    sampler_result_.store(
        static_cast<int>(sample.sampler_result), std::memory_order_relaxed);
    sampler_attempt_count_.store(
        sample.sampler_attempt_count, std::memory_order_relaxed);
    if (mid_publish_hook_ != nullptr) {
      mid_publish_hook_(hook_user_data_);
    }
    const std::uint64_t bounded_success =
        sample.sampler_success_count <= sample.sampler_attempt_count
        ? sample.sampler_success_count
        : sample.sampler_attempt_count;
    sampler_success_count_.store(bounded_success, std::memory_order_relaxed);
    sequence_.fetch_add(1, std::memory_order_release);
  }

  bool read(MecchaOrientationTelemetrySample* output) const {
    if (output == nullptr) {
      return false;
    }
    for (int attempt = 0; attempt < kReadAttempts; ++attempt) {
      const std::uint64_t before = sequence_.load(std::memory_order_acquire);
      if ((before & 1) != 0) {
        contentionPause();
        continue;
      }
      MecchaOrientationTelemetrySample sample{};
      for (std::size_t index = 0;
           index < sample.pose.unreal_delta.values.size(); ++index) {
        sample.pose.unreal_delta.values[index] = std::bit_cast<double>(
            delta_[index].load(std::memory_order_relaxed));
      }
      sample.pose.frame_index = frame_index_.load(std::memory_order_relaxed);
      sample.pose.publish_time_ns =
          publish_time_ns_.load(std::memory_order_relaxed);
      sample.pose.valid = pose_valid_.load(std::memory_order_relaxed);
      sample.pose_publish_count =
          pose_publish_count_.load(std::memory_order_relaxed);
      sample.sampler_active = sampler_active_.load(std::memory_order_relaxed);
      sample.sampler_result = static_cast<VrRuntimeResult>(
          sampler_result_.load(std::memory_order_relaxed));
      sample.sampler_attempt_count =
          sampler_attempt_count_.load(std::memory_order_relaxed);
      sample.sampler_success_count =
          sampler_success_count_.load(std::memory_order_relaxed);
      const std::uint64_t after = sequence_.load(std::memory_order_acquire);
      if (before == after) {
        *output = sample;
        return true;
      }
      contentionPause();
    }
    return false;
  }

 private:
  static constexpr int kReadAttempts = 64;

  static void contentionPause() {
#if defined(_MSC_VER)
    _mm_pause();
#else
    std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
  }

  std::atomic<std::uint64_t> sequence_{0};
  std::array<std::atomic<std::uint64_t>, 9> delta_{};
  std::atomic<std::uint64_t> frame_index_{0};
  std::atomic<std::int64_t> publish_time_ns_{0};
  std::atomic<bool> pose_valid_{false};
  std::atomic<std::uint64_t> pose_publish_count_{0};
  std::atomic<bool> sampler_active_{false};
  std::atomic<int> sampler_result_{VR_RUNTIME_SKIPPED};
  std::atomic<std::uint64_t> sampler_attempt_count_{0};
  std::atomic<std::uint64_t> sampler_success_count_{0};
  MecchaOrientationTelemetryPublishHook mid_publish_hook_ = nullptr;
  void* hook_user_data_ = nullptr;
};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<std::int64_t>::is_always_lock_free);
static_assert(std::atomic<int>::is_always_lock_free);

enum class MecchaOrientationOperationalReadStage : unsigned {
  WholeTupleCollected,
};

using MecchaOrientationOperationalReadHook = void (*)(
    MecchaOrientationOperationalReadStage stage, void* user_data);

struct MecchaOrientationOperationalSources {
  const std::atomic<std::uint64_t>* constructor_hits = nullptr;
  const std::atomic<std::uint64_t>* decoded_cameras = nullptr;
  const std::atomic<std::uint64_t>* rejected_cameras = nullptr;
  const std::atomic<std::uint64_t>* stable_camera_id = nullptr;
  const std::atomic<std::uint64_t>* secondary_stable_camera_id = nullptr;
  const std::atomic<std::uint64_t>* orientation_applies = nullptr;
  const std::atomic<std::uint64_t>* consecutive_applies = nullptr;
  const std::atomic<std::uint64_t>* missing_pose_skips = nullptr;
  const std::atomic<std::uint64_t>* stale_pose_skips = nullptr;
  const std::atomic<std::uint64_t>* write_rejects = nullptr;
  bool (*load_tracking_ready)(void* user_data) = nullptr;
  bool (*load_tracking_fault)(void* user_data) = nullptr;
  void* tracking_user_data = nullptr;
};

struct MecchaOrientationOperationalSnapshot {
  bool tracking_ready = false;
  bool tracking_fault_latched = false;
  std::uint64_t constructor_hit_count = 0;
  std::uint64_t decoded_camera_count = 0;
  std::uint64_t rejected_camera_count = 0;
  std::uint64_t stable_camera_id = 0;
  std::uint64_t secondary_stable_camera_id = 0;
  std::uint64_t orientation_apply_count = 0;
  std::uint64_t consecutive_orientation_apply_count = 0;
  std::uint64_t missing_pose_skip_count = 0;
  std::uint64_t stale_pose_skip_count = 0;
  std::uint64_t write_reject_count = 0;

  bool operator==(const MecchaOrientationOperationalSnapshot&) const = default;
};

inline bool collectMecchaOrientationOperationalSnapshot(
    const MecchaOrientationOperationalSources& sources,
    MecchaOrientationOperationalSnapshot* output,
    MecchaOrientationOperationalReadHook read_hook = nullptr,
    void* hook_user_data = nullptr) {
  constexpr int kReadAttempts = 64;
  const auto contentionPause = []() {
#if defined(_MSC_VER)
    _mm_pause();
#else
    std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
  };
  if (output == nullptr || sources.constructor_hits == nullptr ||
      sources.decoded_cameras == nullptr ||
      sources.rejected_cameras == nullptr ||
      sources.stable_camera_id == nullptr ||
      sources.secondary_stable_camera_id == nullptr ||
      sources.orientation_applies == nullptr ||
      sources.consecutive_applies == nullptr ||
      sources.missing_pose_skips == nullptr ||
      sources.stale_pose_skips == nullptr || sources.write_rejects == nullptr ||
      sources.load_tracking_ready == nullptr ||
      sources.load_tracking_fault == nullptr) {
    return false;
  }

  const auto readTuple = [&sources]() {
    MecchaOrientationOperationalSnapshot tuple{};
    tuple.constructor_hit_count =
        sources.constructor_hits->load(std::memory_order_acquire);
    tuple.decoded_camera_count =
        sources.decoded_cameras->load(std::memory_order_acquire);
    tuple.rejected_camera_count =
        sources.rejected_cameras->load(std::memory_order_acquire);
    tuple.stable_camera_id =
        sources.stable_camera_id->load(std::memory_order_acquire);
    tuple.secondary_stable_camera_id =
        sources.secondary_stable_camera_id->load(std::memory_order_acquire);
    tuple.orientation_apply_count =
        sources.orientation_applies->load(std::memory_order_acquire);
    tuple.consecutive_orientation_apply_count =
        sources.consecutive_applies->load(std::memory_order_acquire);
    tuple.missing_pose_skip_count =
        sources.missing_pose_skips->load(std::memory_order_acquire);
    tuple.stale_pose_skip_count =
        sources.stale_pose_skips->load(std::memory_order_acquire);
    tuple.write_reject_count =
        sources.write_rejects->load(std::memory_order_acquire);
    tuple.tracking_ready =
        sources.load_tracking_ready(sources.tracking_user_data);
    tuple.tracking_fault_latched =
        sources.load_tracking_fault(sources.tracking_user_data);
    return tuple;
  };
  const auto invariantsHold = [](
      const MecchaOrientationOperationalSnapshot& tuple) {
    if (tuple.decoded_camera_count > tuple.constructor_hit_count ||
        tuple.rejected_camera_count >
            tuple.constructor_hit_count - tuple.decoded_camera_count ||
        (tuple.stable_camera_id != 0 && tuple.decoded_camera_count == 0) ||
        (tuple.secondary_stable_camera_id != 0 &&
         (tuple.stable_camera_id == 0 ||
          tuple.secondary_stable_camera_id == tuple.stable_camera_id)) ||
        tuple.consecutive_orientation_apply_count >
            tuple.orientation_apply_count ||
        (tuple.tracking_ready && tuple.tracking_fault_latched)) {
      return false;
    }
    std::uint64_t remaining = tuple.decoded_camera_count;
    const std::array<std::uint64_t, 4> terminal_counts{{
        tuple.orientation_apply_count,
        tuple.missing_pose_skip_count,
        tuple.stale_pose_skip_count,
        tuple.write_reject_count,
    }};
    for (const std::uint64_t count : terminal_counts) {
      if (count > remaining) {
        return false;
      }
      remaining -= count;
    }
    return true;
  };

  for (int attempt = 0; attempt < kReadAttempts; ++attempt) {
    const MecchaOrientationOperationalSnapshot before = readTuple();
    if (read_hook != nullptr) {
      read_hook(
          MecchaOrientationOperationalReadStage::WholeTupleCollected,
          hook_user_data);
    }
    const MecchaOrientationOperationalSnapshot after = readTuple();
    if (before == after && invariantsHold(after)) {
      *output = after;
      return true;
    }
    contentionPause();
  }
  return false;
}

struct MecchaOrientationSnapshot {
  MecchaOrientationStatus status = MecchaOrientationStatus::Starting;
  bool signature_verified = false;
  bool hook_installed = false;
  bool pose_snapshot_available = false;
  bool operational_snapshot_available = false;
  bool pose_sample_valid = false;
  bool sampler_active = false;
  bool tracking_ready = false;
  bool tracking_fault_latched = false;
  bool stereo_pair_snapshot_available = false;
  VrRuntimeResult sampler_result = VR_RUNTIME_SKIPPED;
  std::uint32_t constructor_rva = 0;
  std::uint64_t constructor_hit_count = 0;
  std::uint64_t decoded_camera_count = 0;
  std::uint64_t rejected_camera_count = 0;
  std::uint64_t stable_camera_id = 0;
  std::uint64_t secondary_stable_camera_id = 0;
  float observed_vertical_fov_degrees = 0.0F;
  float observed_aspect_ratio = 0.0F;
  std::uint64_t pose_publish_count = 0;
  std::uint64_t pose_frame_index = 0;
  std::int64_t pose_publish_time_ns = 0;
  std::array<double, 9> pose_unreal_delta{};
  std::uint64_t sampler_attempt_count = 0;
  std::uint64_t sampler_success_count = 0;
  std::uint64_t orientation_apply_count = 0;
  std::uint64_t consecutive_orientation_apply_count = 0;
  std::uint64_t missing_pose_skip_count = 0;
  std::uint64_t stale_pose_skip_count = 0;
  std::uint64_t write_reject_count = 0;
  std::uint64_t stereo_completed_pair_count = 0;
  std::uint64_t stereo_incomplete_pair_count = 0;
  std::uint64_t stereo_duplicate_eye_count = 0;
  std::uint64_t stereo_pose_frame_mismatch_count = 0;
  std::uint64_t stereo_pair_pose_frame_index = 0;
  std::uint64_t stereo_pair_render_generation = 0;
  std::array<std::uint64_t, 2> stereo_eye_camera_ids{};
  std::array<UnrealVector3d, 2> stereo_eye_locations_uu{};
  std::int64_t stereo_eye_observation_delta_ns = 0;
  double stereo_eye_separation_uu = 0.0;
};

bool installMecchaOrientationOnlyHook();
void publishMecchaOrientationOnlyPoseSample(
    const VrRuntimePose& pose, std::uint64_t sample_index);
void publishMecchaOrientationOnlyPoseSample(
    const VrRuntimePose& pose,
    std::uint64_t sample_index,
    bool sampler_active,
    VrRuntimeResult sampler_result,
    std::uint64_t sampler_attempt_count,
    std::uint64_t sampler_success_count);
void publishMecchaOrientationSamplerState(
    bool sampler_active,
    VrRuntimeResult sampler_result,
    std::uint64_t sampler_attempt_count,
    std::uint64_t sampler_success_count);
void publishMecchaOrientationOnlyPose(const VrRuntimeFrameData& frame);
void publishMecchaStereoRenderGeneration(std::uint64_t render_generation);
MecchaOrientationSnapshot mecchaOrientationSnapshot();
const char* mecchaOrientationStatusName(MecchaOrientationStatus status);

}  // namespace vrclient::adapters::unreal

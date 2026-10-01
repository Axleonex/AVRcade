#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace vrclient::adapters::unreal {

enum class MecchaCameraIdentityResult {
  Ready,
  Pending,
  Ambiguous,
  InvalidObservation,
};

bool mecchaStereoProjectionMatches(
    float observed_vertical_fov_degrees,
    float observed_aspect_ratio,
    float expected_vertical_fov_degrees,
    float expected_aspect_ratio,
    float fov_tolerance_degrees,
    float aspect_tolerance);

class MecchaCameraIdentityGate {
 public:
  MecchaCameraIdentityGate(
      std::size_t required_consecutive_samples,
      std::int64_t maximum_observation_gap_ns,
      std::int64_t recent_identity_window_ns);

  MecchaCameraIdentityResult observe(
      std::uint64_t camera_id, std::int64_t observation_time_ns);
  bool allows(std::uint64_t camera_id, std::int64_t now_ns) const;
  std::uint64_t stableCameraId() const;
  void reset();

 private:
  struct RecentIdentity {
    std::uint64_t camera_id = 0;
    std::int64_t last_seen_ns = 0;
  };

  static constexpr std::size_t kRecentIdentityCapacity = 4;

  std::size_t required_consecutive_samples_ = 0;
  std::int64_t maximum_observation_gap_ns_ = 0;
  std::int64_t recent_identity_window_ns_ = 0;
  std::array<RecentIdentity, kRecentIdentityCapacity> recent_{};
  std::int64_t overflow_last_seen_ns_ = 0;
  std::uint64_t candidate_camera_id_ = 0;
  std::size_t candidate_samples_ = 0;
  std::int64_t candidate_last_seen_ns_ = 0;
  std::uint64_t stable_camera_id_ = 0;
};

// Native stereo deliberately has two recent FSceneViewState identities. This
// gate accepts exactly one alternating pair and fails closed on a third view,
// a stale eye, non-monotonic time, or a changed pair.
class MecchaStereoCameraIdentityGate {
 public:
  MecchaStereoCameraIdentityGate(
      std::size_t required_complete_pairs,
      std::int64_t maximum_observation_gap_ns,
      std::int64_t recent_identity_window_ns);

  MecchaCameraIdentityResult observe(
      std::uint64_t camera_id, std::int64_t observation_time_ns);
  bool allows(std::uint64_t camera_id, std::int64_t now_ns) const;
  std::array<std::uint64_t, 2> stableCameraIds() const;
  void reset();

 private:
  struct RecentIdentity {
    std::uint64_t camera_id = 0;
    std::int64_t last_seen_ns = 0;
  };

  std::size_t required_complete_pairs_ = 0;
  std::int64_t maximum_observation_gap_ns_ = 0;
  std::int64_t recent_identity_window_ns_ = 0;
  std::array<RecentIdentity, 3> recent_{};
  std::array<std::uint64_t, 2> candidate_pair_{};
  std::array<std::uint64_t, 2> stable_pair_{};
  std::uint8_t candidate_seen_mask_ = 0;
  std::size_t complete_pair_count_ = 0;
  std::int64_t last_observation_ns_ = 0;
};

}  // namespace vrclient::adapters::unreal

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace vrclient::adapters::unreal {

using MecchaOrientationTrackingPublishHook = void (*)(void* user_data);

enum class MecchaOrientationTrackingResult {
  Warming,
  Ready,
  FaultLatched,
};

class MecchaOrientationTrackingGate {
 public:
  MecchaOrientationTrackingGate(
      std::size_t required_fresh_samples,
      std::int64_t maximum_pose_gap_ns);

  MecchaOrientationTrackingResult observePose(
      bool valid, std::int64_t publish_time_ns);
  bool allowsWrite(std::int64_t now_ns);
  bool ready() const;
  bool faultLatched() const;

 private:
  void latchFault();

  std::size_t required_fresh_samples_ = 0;
  std::int64_t maximum_pose_gap_ns_ = 0;
  std::size_t fresh_samples_ = 0;
  std::int64_t last_publish_time_ns_ = 0;
  bool ready_ = false;
  bool fault_latched_ = false;
};

// One pose-publisher thread owns the warm-up gate. Camera threads may latch a
// fault concurrently without sharing a lock with the publisher or re-enabling
// writes later in the launch.
class MecchaOrientationTrackingState {
 public:
  MecchaOrientationTrackingState(
      std::size_t required_fresh_samples,
      std::int64_t maximum_pose_gap_ns,
      MecchaOrientationTrackingPublishHook before_ready_publish = nullptr,
      void* publish_hook_user_data = nullptr);

  MecchaOrientationTrackingResult publishPose(
      bool valid, std::int64_t publish_time_ns);
  void latchFault();
  bool ready() const;
  bool faultLatched() const;

 private:
  MecchaOrientationTrackingGate publisher_gate_;
  MecchaOrientationTrackingPublishHook before_ready_publish_ = nullptr;
  void* publish_hook_user_data_ = nullptr;
  std::atomic<bool> ready_{false};
  std::atomic<bool> fault_latched_{false};
};

}  // namespace vrclient::adapters::unreal

#include "adapters/unreal/meccha_orientation_tracking_gate.h"

namespace vrclient::adapters::unreal {

MecchaOrientationTrackingGate::MecchaOrientationTrackingGate(
    std::size_t required_fresh_samples,
    std::int64_t maximum_pose_gap_ns)
    : required_fresh_samples_(required_fresh_samples),
      maximum_pose_gap_ns_(maximum_pose_gap_ns) {}

MecchaOrientationTrackingResult MecchaOrientationTrackingGate::observePose(
    bool valid, std::int64_t publish_time_ns) {
  if (fault_latched_) {
    return MecchaOrientationTrackingResult::FaultLatched;
  }
  if (!valid || publish_time_ns <= 0 || required_fresh_samples_ == 0 ||
      maximum_pose_gap_ns_ < 0 ||
      (last_publish_time_ns_ != 0 &&
       (publish_time_ns < last_publish_time_ns_ ||
        publish_time_ns - last_publish_time_ns_ > maximum_pose_gap_ns_))) {
    fresh_samples_ = 0;
    last_publish_time_ns_ = 0;
    ready_ = false;
    return MecchaOrientationTrackingResult::Warming;
  }
  last_publish_time_ns_ = publish_time_ns;
  if (fresh_samples_ < required_fresh_samples_) {
    ++fresh_samples_;
  }
  ready_ = fresh_samples_ >= required_fresh_samples_;
  return ready_ ? MecchaOrientationTrackingResult::Ready
                : MecchaOrientationTrackingResult::Warming;
}

bool MecchaOrientationTrackingGate::allowsWrite(std::int64_t now_ns) {
  if (fault_latched_ || !ready_) {
    return false;
  }
  if (now_ns < last_publish_time_ns_ ||
      now_ns - last_publish_time_ns_ > maximum_pose_gap_ns_) {
    fresh_samples_ = 0;
    last_publish_time_ns_ = 0;
    ready_ = false;
    return false;
  }
  return true;
}

bool MecchaOrientationTrackingGate::ready() const {
  return ready_;
}

bool MecchaOrientationTrackingGate::faultLatched() const {
  return fault_latched_;
}

void MecchaOrientationTrackingGate::latchFault() {
  ready_ = false;
  fault_latched_ = true;
}

MecchaOrientationTrackingState::MecchaOrientationTrackingState(
    std::size_t required_fresh_samples,
    std::int64_t maximum_pose_gap_ns,
    MecchaOrientationTrackingPublishHook before_ready_publish,
    void* publish_hook_user_data)
    : publisher_gate_(required_fresh_samples, maximum_pose_gap_ns),
      before_ready_publish_(before_ready_publish),
      publish_hook_user_data_(publish_hook_user_data) {}

MecchaOrientationTrackingResult MecchaOrientationTrackingState::publishPose(
    bool valid, std::int64_t publish_time_ns) {
  if (fault_latched_.load(std::memory_order_acquire)) {
    ready_.store(false, std::memory_order_release);
    return MecchaOrientationTrackingResult::FaultLatched;
  }

  const auto result = publisher_gate_.observePose(valid, publish_time_ns);
  if (result == MecchaOrientationTrackingResult::FaultLatched) {
    fault_latched_.store(true, std::memory_order_release);
    ready_.store(false, std::memory_order_release);
    return result;
  }

  // Optional deterministic test seam. Production callers leave this null, so
  // the single-publisher/atomic-fault path remains lock-free.
  if (before_ready_publish_ != nullptr) {
    before_ready_publish_(publish_hook_user_data_);
  }
  ready_.store(
      result == MecchaOrientationTrackingResult::Ready,
      std::memory_order_release);
  if (fault_latched_.load(std::memory_order_acquire)) {
    ready_.store(false, std::memory_order_release);
    return MecchaOrientationTrackingResult::FaultLatched;
  }
  return result;
}

void MecchaOrientationTrackingState::latchFault() {
  fault_latched_.store(true, std::memory_order_release);
  ready_.store(false, std::memory_order_release);
}

bool MecchaOrientationTrackingState::ready() const {
  return ready_.load(std::memory_order_acquire) &&
      !fault_latched_.load(std::memory_order_acquire);
}

bool MecchaOrientationTrackingState::faultLatched() const {
  return fault_latched_.load(std::memory_order_acquire);
}

}  // namespace vrclient::adapters::unreal

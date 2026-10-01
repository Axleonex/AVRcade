#pragma once

#include <atomic>
#include <bit>
#include <cstdint>
#include <mutex>

namespace vrclient::adapters::unreal {

struct MecchaEvidenceResultSnapshot {
  int result = 0;
  bool attempted = false;
};

// One atomic publication prevents evidence consumers from observing an
// attempted bit from one update and a result from another.
class MecchaEvidenceResultState {
 public:
  explicit MecchaEvidenceResultState(int initial_result)
      : value_(pack(initial_result, false)) {}

  void publish(int result) {
    value_.store(pack(result, true), std::memory_order_release);
  }

  MecchaEvidenceResultSnapshot snapshot() const {
    return unpack(value_.load(std::memory_order_acquire));
  }

  // Deterministic test seam: the hook runs after the one coherent load but
  // before decoding. A concurrent publish can therefore produce either the
  // complete old pair or the complete new pair, never a mixed pair.
  MecchaEvidenceResultSnapshot snapshotAfterLoad(
      void (*after_load)(void*), void* user_data) const {
    const std::uint64_t value = value_.load(std::memory_order_acquire);
    if (after_load != nullptr) {
      after_load(user_data);
    }
    return unpack(value);
  }

 private:
  static constexpr std::uint64_t pack(int result, bool attempted) {
    return static_cast<std::uint64_t>(
               std::bit_cast<std::uint32_t>(
                   static_cast<std::int32_t>(result))) |
        (static_cast<std::uint64_t>(attempted) << 32);
  }

  static constexpr MecchaEvidenceResultSnapshot unpack(std::uint64_t value) {
    return {
        static_cast<int>(std::bit_cast<std::int32_t>(
            static_cast<std::uint32_t>(value))),
        ((value >> 32) & 1u) != 0,
    };
  }

  std::atomic<std::uint64_t> value_;
};

// Requests remain outstanding until the writer acknowledges a successfully
// persisted generation. Newer concurrent requests cannot be cleared by an
// older write completing.
class MecchaEvidenceGenerationState {
 public:
  std::uint64_t request() {
    return requested_.fetch_add(1, std::memory_order_acq_rel) + 1;
  }

  std::uint64_t requested() const {
    return requested_.load(std::memory_order_acquire);
  }

  std::uint64_t acknowledged() const {
    return acknowledged_.load(std::memory_order_acquire);
  }

  bool acknowledged(std::uint64_t generation) const {
    return acknowledged() >= generation;
  }

  bool pending() const {
    return acknowledged() < requested();
  }

  void acknowledge(std::uint64_t generation) {
    std::uint64_t observed = acknowledged_.load(std::memory_order_acquire);
    while (observed < generation &&
           !acknowledged_.compare_exchange_weak(
               observed, generation, std::memory_order_release,
               std::memory_order_acquire)) {
    }
  }

  void completeAttempt(std::uint64_t generation, bool persisted) {
    if (persisted) {
      acknowledge(generation);
    }
  }

 private:
  std::atomic<std::uint64_t> requested_{0};
  std::atomic<std::uint64_t> acknowledged_{0};
};

enum class MecchaEvidencePersistResult {
  Success,
  WriteFailed,
  FlushFailed,
  RenameFailed,
};

using MecchaEvidenceWorkerEntry = void (*)(void*);

// Platform operations are injectable so the production initialization,
// worker, retry, flush, and shutdown state machine can be exercised without
// loading the injected DLL or touching the filesystem.
class MecchaEvidenceDispatchOperations {
 public:
  virtual ~MecchaEvidenceDispatchOperations() = default;

  virtual bool createRequestSignal() = 0;
  virtual bool createAcknowledgedSignal(bool initially_signaled) = 0;
  virtual bool startWorker(MecchaEvidenceWorkerEntry entry, void* context) = 0;
  virtual bool signalRequest() = 0;
  virtual bool resetAcknowledged() = 0;
  virtual bool signalAcknowledged() = 0;
  virtual void waitForRequest(std::uint32_t timeout_ms) = 0;
  virtual void waitForAcknowledged(std::uint32_t timeout_ms) = 0;
  virtual void sleepFor(std::uint32_t timeout_ms) = 0;
  virtual std::uint64_t monotonicMilliseconds() = 0;
  virtual MecchaEvidencePersistResult persist() = 0;
  virtual bool joinWorker(std::uint32_t timeout_ms) = 0;
};

class MecchaEvidenceDispatcher {
 public:
  MecchaEvidenceDispatcher(
      MecchaEvidenceDispatchOperations& operations,
      std::uint32_t poll_ms,
      std::uint32_t retry_initial_ms,
      std::uint32_t retry_maximum_ms)
      : operations_(operations),
        poll_ms_(poll_ms),
        retry_initial_ms_(retry_initial_ms),
        retry_maximum_ms_(retry_maximum_ms) {}

  bool initializeOnce() {
    std::lock_guard<std::mutex> lock(initialization_lock_);
    if (initialization_terminal_failure_.load(std::memory_order_acquire)) {
      return false;
    }
    if (!request_signal_ready_.load(std::memory_order_acquire) &&
        operations_.createRequestSignal()) {
      request_signal_ready_.store(true, std::memory_order_release);
    }
    if (!acknowledged_signal_ready_.load(std::memory_order_acquire) &&
        operations_.createAcknowledgedSignal(!generations_.pending())) {
      acknowledged_signal_ready_.store(true, std::memory_order_release);
    }
    if (!worker_started_.load(std::memory_order_acquire) &&
        request_signal_ready_.load(std::memory_order_acquire) &&
        acknowledged_signal_ready_.load(std::memory_order_acquire) &&
        operations_.startWorker(&MecchaEvidenceDispatcher::workerEntry, this)) {
      worker_started_.store(true, std::memory_order_release);
    }
    return worker_started_.load(std::memory_order_acquire);
  }

  bool initializeUntilReady(
      std::uint32_t timeout_ms,
      std::uint32_t retry_initial_ms,
      std::uint32_t retry_maximum_ms) {
    const std::uint64_t started = operations_.monotonicMilliseconds();
    std::uint32_t retry_ms = retry_initial_ms;
    for (;;) {
      if (initializeOnce()) {
        return true;
      }
      const std::uint64_t now = operations_.monotonicMilliseconds();
      const std::uint64_t elapsed = now >= started ? now - started : 0;
      if (elapsed >= timeout_ms) {
        std::lock_guard<std::mutex> lock(initialization_lock_);
        if (worker_started_.load(std::memory_order_acquire)) {
          return true;
        }
        initialization_terminal_failure_.store(
            true, std::memory_order_release);
        return false;
      }
      const std::uint64_t remaining = timeout_ms - elapsed;
      const std::uint32_t delay = static_cast<std::uint32_t>(
          remaining < retry_ms ? remaining : retry_ms);
      operations_.sleepFor(delay);
      retry_ms = retry_ms < retry_maximum_ms / 2
          ? retry_ms * 2
          : retry_maximum_ms;
    }
  }

  std::uint64_t request() {
    const std::uint64_t generation = generations_.request();
    if (acknowledged_signal_ready_.load(std::memory_order_acquire)) {
      operations_.resetAcknowledged();
    }
    if (request_signal_ready_.load(std::memory_order_acquire)) {
      operations_.signalRequest();
    }
    return generation;
  }

  bool flush(std::uint64_t generation, std::uint32_t timeout_ms) {
    const std::uint64_t started = operations_.monotonicMilliseconds();
    while (!generations_.acknowledged(generation)) {
      if (initialization_terminal_failure_.load(std::memory_order_acquire)) {
        return false;
      }
      initializeOnce();
      if (request_signal_ready_.load(std::memory_order_acquire)) {
        operations_.signalRequest();
      }
      const std::uint64_t now = operations_.monotonicMilliseconds();
      const std::uint64_t elapsed = now >= started ? now - started : 0;
      if (elapsed >= timeout_ms) {
        return false;
      }
      const std::uint64_t remaining = timeout_ms - elapsed;
      const std::uint32_t wait_ms = static_cast<std::uint32_t>(
          remaining < poll_ms_ ? remaining : poll_ms_);
      if (acknowledged_signal_ready_.load(std::memory_order_acquire)) {
        operations_.waitForAcknowledged(wait_ms);
      } else {
        operations_.sleepFor(wait_ms);
      }
    }
    return true;
  }

  bool shutdownForTest(std::uint32_t timeout_ms) {
    stopping_.store(true, std::memory_order_release);
    if (!worker_started_.load(std::memory_order_acquire)) {
      return true;
    }
    operations_.signalRequest();
    return operations_.joinWorker(timeout_ms);
  }

  bool ready() const {
    return worker_started_.load(std::memory_order_acquire);
  }

  bool initializationTerminalFailure() const {
    return initialization_terminal_failure_.load(std::memory_order_acquire);
  }

  std::uint64_t requested() const { return generations_.requested(); }
  std::uint64_t acknowledged() const { return generations_.acknowledged(); }
  bool pending() const { return generations_.pending(); }

 private:
  static void workerEntry(void* context) {
    static_cast<MecchaEvidenceDispatcher*>(context)->runWorker();
  }

  void runWorker() {
    std::uint32_t retry_ms = retry_initial_ms_;
    while (!stopping_.load(std::memory_order_acquire)) {
      if (!generations_.pending()) {
        operations_.signalAcknowledged();
        operations_.waitForRequest(poll_ms_);
        continue;
      }

      const std::uint64_t generation = generations_.requested();
      const bool persisted =
          operations_.persist() == MecchaEvidencePersistResult::Success;
      generations_.completeAttempt(generation, persisted);
      if (persisted) {
        retry_ms = retry_initial_ms_;
        if (!generations_.pending()) {
          operations_.signalAcknowledged();
        }
        continue;
      }

      operations_.waitForRequest(retry_ms);
      retry_ms = retry_ms < retry_maximum_ms_ / 2
          ? retry_ms * 2
          : retry_maximum_ms_;
    }
  }

  MecchaEvidenceDispatchOperations& operations_;
  const std::uint32_t poll_ms_;
  const std::uint32_t retry_initial_ms_;
  const std::uint32_t retry_maximum_ms_;
  MecchaEvidenceGenerationState generations_;
  std::mutex initialization_lock_;
  std::atomic<bool> request_signal_ready_{false};
  std::atomic<bool> acknowledged_signal_ready_{false};
  std::atomic<bool> worker_started_{false};
  std::atomic<bool> initialization_terminal_failure_{false};
  std::atomic<bool> stopping_{false};
};

}  // namespace vrclient::adapters::unreal

#include "adapters/unreal/meccha_evidence_dispatch_state.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

using vrclient::adapters::unreal::MecchaEvidenceDispatchOperations;
using vrclient::adapters::unreal::MecchaEvidenceDispatcher;
using vrclient::adapters::unreal::MecchaEvidencePersistResult;
using vrclient::adapters::unreal::MecchaEvidenceResultSnapshot;
using vrclient::adapters::unreal::MecchaEvidenceResultState;
using vrclient::adapters::unreal::MecchaEvidenceWorkerEntry;

class FaultInjectedOperations final : public MecchaEvidenceDispatchOperations {
 public:
  int request_create_failures = 0;
  int acknowledged_create_failures = 0;
  int worker_create_failures = 0;
  int request_signal_failures = 0;
  int acknowledged_signal_failures = 0;
  int write_failures = 0;
  int flush_failures = 0;
  int rename_failures = 0;

  bool createRequestSignal() override {
    ++request_create_attempts;
    if (consumeFailure(request_create_failures)) {
      return false;
    }
    request_created = true;
    return true;
  }

  bool createAcknowledgedSignal(bool initially_signaled) override {
    ++acknowledged_create_attempts;
    if (consumeFailure(acknowledged_create_failures)) {
      return false;
    }
    std::lock_guard<std::mutex> lock(event_lock_);
    acknowledged_created = true;
    acknowledged_signaled_ = initially_signaled;
    return true;
  }

  bool startWorker(
      MecchaEvidenceWorkerEntry entry, void* context) override {
    ++worker_create_attempts;
    if (consumeFailure(worker_create_failures)) {
      return false;
    }
    worker_ = std::thread([this, entry, context]() {
      entry(context);
      {
        std::lock_guard<std::mutex> lock(worker_lock_);
        worker_exited_ = true;
      }
      worker_condition_.notify_all();
    });
    return true;
  }

  bool signalRequest() override {
    ++request_signal_attempts;
    if (consumeFailure(request_signal_failures)) {
      return false;
    }
    {
      std::lock_guard<std::mutex> lock(event_lock_);
      request_signaled_ = true;
    }
    request_condition_.notify_all();
    return true;
  }

  bool resetAcknowledged() override {
    std::lock_guard<std::mutex> lock(event_lock_);
    acknowledged_signaled_ = false;
    return true;
  }

  bool signalAcknowledged() override {
    ++acknowledged_signal_attempts;
    acknowledged_attempt_condition_.notify_all();
    if (consumeFailure(acknowledged_signal_failures)) {
      return false;
    }
    {
      std::lock_guard<std::mutex> lock(event_lock_);
      acknowledged_signaled_ = true;
    }
    acknowledged_condition_.notify_all();
    return true;
  }

  void waitForRequest(std::uint32_t timeout_ms) override {
    std::unique_lock<std::mutex> lock(event_lock_);
    request_condition_.wait_for(
        lock,
        std::chrono::milliseconds(timeout_ms),
        [this]() { return request_signaled_; });
    request_signaled_ = false;
  }

  void waitForAcknowledged(std::uint32_t timeout_ms) override {
    std::unique_lock<std::mutex> lock(event_lock_);
    acknowledged_condition_.wait_for(
        lock,
        std::chrono::milliseconds(timeout_ms),
        [this]() { return acknowledged_signaled_; });
  }

  void sleepFor(std::uint32_t timeout_ms) override {
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
  }

  std::uint64_t monotonicMilliseconds() override {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
  }

  MecchaEvidencePersistResult persist() override {
    const int attempt = ++persist_attempts;
    {
      std::unique_lock<std::mutex> lock(persist_lock_);
      persist_attempt_seen_ = attempt;
      persist_condition_.notify_all();
      if (attempt <= paused_persist_attempts_) {
        persist_condition_.wait(
            lock,
            [this, attempt]() { return released_persist_attempt_ >= attempt; });
      }
    }
    ++write_attempts;
    if (consumeFailure(write_failures)) {
      return MecchaEvidencePersistResult::WriteFailed;
    }
    ++flush_attempts;
    if (consumeFailure(flush_failures)) {
      return MecchaEvidencePersistResult::FlushFailed;
    }
    ++rename_attempts;
    if (consumeFailure(rename_failures)) {
      return MecchaEvidencePersistResult::RenameFailed;
    }
    return MecchaEvidencePersistResult::Success;
  }

  bool joinWorker(std::uint32_t timeout_ms) override {
    {
      std::unique_lock<std::mutex> lock(worker_lock_);
      if (!worker_condition_.wait_for(
              lock,
              std::chrono::milliseconds(timeout_ms),
              [this]() { return worker_exited_; })) {
        return false;
      }
    }
    worker_.join();
    return true;
  }

  void pausePersistAttempts(int count) {
    std::lock_guard<std::mutex> lock(persist_lock_);
    paused_persist_attempts_ = count;
  }

  bool waitForPersistAttempt(int attempt, std::uint32_t timeout_ms) {
    std::unique_lock<std::mutex> lock(persist_lock_);
    return persist_condition_.wait_for(
        lock,
        std::chrono::milliseconds(timeout_ms),
        [this, attempt]() { return persist_attempt_seen_ >= attempt; });
  }

  void releasePersistAttempt(int attempt) {
    {
      std::lock_guard<std::mutex> lock(persist_lock_);
      released_persist_attempt_ = attempt;
    }
    persist_condition_.notify_all();
  }

  bool waitForAcknowledgedSignalAttempt(
      int attempt, std::uint32_t timeout_ms) {
    std::unique_lock<std::mutex> lock(acknowledged_attempt_lock_);
    return acknowledged_attempt_condition_.wait_for(
        lock,
        std::chrono::milliseconds(timeout_ms),
        [this, attempt]() {
          return acknowledged_signal_attempts.load(std::memory_order_acquire) >=
              attempt;
        });
  }

  std::atomic<int> request_create_attempts{0};
  std::atomic<int> acknowledged_create_attempts{0};
  std::atomic<int> worker_create_attempts{0};
  std::atomic<int> request_signal_attempts{0};
  std::atomic<int> acknowledged_signal_attempts{0};
  std::atomic<int> persist_attempts{0};
  std::atomic<int> write_attempts{0};
  std::atomic<int> flush_attempts{0};
  std::atomic<int> rename_attempts{0};
  bool request_created = false;
  bool acknowledged_created = false;

 private:
  static bool consumeFailure(int& failures) {
    if (failures <= 0) {
      return false;
    }
    --failures;
    return true;
  }

  std::mutex event_lock_;
  std::condition_variable request_condition_;
  std::condition_variable acknowledged_condition_;
  std::mutex acknowledged_attempt_lock_;
  std::condition_variable acknowledged_attempt_condition_;
  bool request_signaled_ = false;
  bool acknowledged_signaled_ = false;
  std::thread worker_;
  std::mutex worker_lock_;
  std::condition_variable worker_condition_;
  bool worker_exited_ = false;
  std::mutex persist_lock_;
  std::condition_variable persist_condition_;
  int paused_persist_attempts_ = 0;
  int persist_attempt_seen_ = 0;
  int released_persist_attempt_ = 0;
};

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void testResultAttemptedPairIsCoherent() {
  constexpr int kInitialFailure = -5;
  constexpr int kPublishedSuccess = 0;
  MecchaEvidenceResultState state(kInitialFailure);
  const auto pauseAfterLoad = [](void* user_data) {
    auto* flags = static_cast<std::atomic<bool>*>(user_data);
    flags[0].store(true, std::memory_order_release);
    while (!flags[1].load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  };
  std::atomic<bool> flags[2];
  flags[0].store(false);
  flags[1].store(false);
  MecchaEvidenceResultSnapshot concurrent_snapshot{};
  std::thread reader([&]() {
    concurrent_snapshot = state.snapshotAfterLoad(pauseAfterLoad, flags);
  });
  while (!flags[0].load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  state.publish(kPublishedSuccess);
  flags[1].store(true, std::memory_order_release);
  reader.join();

  expect(
      !concurrent_snapshot.attempted &&
          concurrent_snapshot.result == kInitialFailure,
      "paused snapshot must retain the complete pre-publication pair");
  const auto published = state.snapshot();
  expect(
      published.attempted && published.result == kPublishedSuccess,
      "later snapshot must observe the complete published pair");
}

void testProductionStateMachineRetriesFirstNFailures() {
  constexpr int kAcknowledgedSignalFailures = 2;
  FaultInjectedOperations operations;
  operations.request_create_failures = 2;
  operations.acknowledged_create_failures = 2;
  operations.worker_create_failures = 2;
  operations.request_signal_failures = 3;
  operations.acknowledged_signal_failures = kAcknowledgedSignalFailures;
  operations.write_failures = 2;
  operations.flush_failures = 2;
  operations.rename_failures = 2;
  MecchaEvidenceDispatcher dispatcher(operations, 2, 1, 8);

  const bool initialized = dispatcher.initializeUntilReady(500, 1, 8);
  const std::uint64_t generation = dispatcher.request();
  const bool flushed = dispatcher.flush(generation, 1'000);
  const bool acknowledged_signal_retried =
      operations.waitForAcknowledgedSignalAttempt(
          kAcknowledgedSignalFailures + 1, 500);
  const bool stopped = dispatcher.shutdownForTest(500);

  expect(initialized && dispatcher.ready(),
         "bootstrap must survive first-N event and worker creation failures");
  expect(flushed && dispatcher.acknowledged() >= generation,
         "write, flush, rename, and signal failures must eventually acknowledge");
  expect(operations.request_create_attempts == 3 &&
             operations.acknowledged_create_attempts == 3 &&
             operations.worker_create_attempts == 3,
         "each creation operation must be retried through its injected failures");
  expect(operations.request_signal_attempts > 3,
         "failed request signals must be retried by polling and terminal flush");
  expect(acknowledged_signal_retried &&
             operations.acknowledged_signal_attempts >
                 kAcknowledgedSignalFailures,
         "acknowledgement signal failures must be followed by a successful retry");
  expect(operations.write_attempts >= 7 && operations.flush_attempts >= 5 &&
             operations.rename_attempts >= 3,
         "the real worker must retry each failed persistence stage");
  expect(stopped, "test harness must join the real worker cleanly");
}

void testRealWorkerPreservesNewerGeneration() {
  FaultInjectedOperations operations;
  operations.pausePersistAttempts(2);
  MecchaEvidenceDispatcher dispatcher(operations, 2, 1, 8);
  const bool initialized = dispatcher.initializeUntilReady(100, 1, 8);
  const std::uint64_t first = dispatcher.request();
  const bool first_started = operations.waitForPersistAttempt(1, 200);
  const std::uint64_t second = dispatcher.request();
  operations.releasePersistAttempt(1);
  const bool second_started = operations.waitForPersistAttempt(2, 200);
  const std::uint64_t acknowledged_while_second_pending =
      dispatcher.acknowledged();
  const bool pending_while_second_blocked = dispatcher.pending();
  operations.releasePersistAttempt(2);
  const bool flushed = dispatcher.flush(second, 500);
  const bool stopped = dispatcher.shutdownForTest(500);

  expect(initialized && first_started && second_started,
         "real worker must reach both controlled persistence attempts");
  expect(acknowledged_while_second_pending == first &&
             pending_while_second_blocked,
         "an older successful write must not acknowledge a newer generation");
  expect(flushed && dispatcher.acknowledged() >= second,
         "newer generation must receive its own successful acknowledgement");
  expect(stopped, "generation test worker must shut down cleanly");
}

void testTerminalFlushIsBounded() {
  FaultInjectedOperations operations;
  operations.write_failures = 1'000'000;
  MecchaEvidenceDispatcher dispatcher(operations, 2, 1, 8);
  const bool initialized = dispatcher.initializeUntilReady(100, 1, 8);
  const std::uint64_t generation = dispatcher.request();
  const auto started = std::chrono::steady_clock::now();
  const bool flushed = dispatcher.flush(generation, 30);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);
  const bool remained_pending = dispatcher.pending();
  const bool stopped = dispatcher.shutdownForTest(500);

  expect(initialized, "timeout test dispatcher must initialize");
  expect(!flushed && remained_pending,
         "terminal timeout must not falsely acknowledge failed persistence");
  expect(elapsed.count() >= 20 && elapsed.count() < 250,
         "terminal flush must honor its bounded timeout");
  expect(stopped, "timed-out worker must still shut down cleanly");
}

void testInitializationFailureBecomesExplicitTerminalState() {
  FaultInjectedOperations operations;
  operations.request_create_failures = 1'000'000;
  MecchaEvidenceDispatcher dispatcher(operations, 2, 1, 8);
  const bool initialized = dispatcher.initializeUntilReady(25, 1, 8);
  const std::uint64_t generation = dispatcher.request();
  const bool flushed = dispatcher.flush(generation, 100);

  expect(!initialized && dispatcher.initializationTerminalFailure(),
         "exhausted bootstrap must publish explicit terminal init failure");
  expect(!dispatcher.ready() && !flushed && dispatcher.pending(),
         "terminal init failure must never masquerade as a possible writer");
}

}  // namespace

int main() {
  try {
    testResultAttemptedPairIsCoherent();
    testProductionStateMachineRetriesFirstNFailures();
    testRealWorkerPreservesNewerGeneration();
    testTerminalFlushIsBounded();
    testInitializationFailureBecomesExplicitTerminalState();
    std::cout << "Meccha evidence dispatch state tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}

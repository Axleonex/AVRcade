#pragma once

// Bolt-on Phase B2 (RE-02): read-only hook validation sandbox.
//
// A hook candidate is only "validated" when it is proven to be hit at the
// expected cadence and on the expected thread BEFORE any transform exists. This
// module proves that the read-only way: it arms an x64 hardware execution
// breakpoint (debug registers DR0-DR3) on a code address in the CURRENT process
// and observes hits through a Vectored Exception Handler. It NEVER patches code
// bytes, never writes target memory, and never installs a transform.
//
// The VEH handler records and consumes a hit only when an armed breakpoint
// matches (debug status bit B0..B3 + faulting RIP). Unrelated single-step and
// non-single-step exceptions are passed through (EXCEPTION_CONTINUE_SEARCH), so
// the process-global handler can coexist with debuggers and other instrumentation.
//
// Platform: x64 Windows only (hardware debug registers). That is the only
// supported platform for this tooling.

#include "diagnostics/logging/diagnostic_logger.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace vrclient::tooling::hookdisc {

enum class HwBreakSlot {
  Dr0 = 0,
  Dr1 = 1,
  Dr2 = 2,
  Dr3 = 3
};

struct ObservationHit {
  std::uint32_t thread_id = 0;
  std::chrono::steady_clock::time_point timestamp;
};

struct ObservationSummary {
  bool installed = false;
  std::uint64_t hit_count = 0;
  std::vector<std::uint32_t> distinct_thread_ids;
  std::size_t threads_armed = 0;
  double window_seconds = 0.0;       // first-hit to last-hit span
  double observed_cadence_hz = 0.0;  // average hits/second over the window
};

struct ObservationRegisters {
  std::uint64_t rip = 0;
  std::uint64_t rcx = 0;
  std::uint64_t rdx = 0;
  std::uint64_t r8 = 0;
  std::uint64_t r9 = 0;
};

using ObservationHitCallback = void (*)(
    const ObservationRegisters& registers,
    std::uint32_t thread_id,
    void* context);

namespace testing {
// Deterministic unit-test seam for the process-global exception classifier.
long dispatchObservationException(void* exception_pointers);
}  // namespace testing

// Observes a single code address via one hardware execution breakpoint. The
// observer arms every thread of the current process EXCEPT the calling thread
// (the validation harness drives target calls from worker threads). Removal is
// clean: debug registers are cleared on every armed thread AND on the calling
// thread, so the target stays callable with zero behavioral residue (no bytes
// were ever changed and no breakpoint remains armed).
//
// Teardown safety: remove() (and the destructor) first disarm every thread,
// then retire and clear the registry slot, then DRAIN any exception handler
// already in flight before returning, so a handler can never run recordHit()
// against a half-destroyed object. A late CPU/OS dispatch is consumed only when
// its DR status slot and RIP match the retired watch exactly.
// The process-global Vectored Exception Handler is installed once and then LEFT
// REGISTERED:
// unregistering it per-observer would race a hardware single-step already in
// flight toward it and turn that into an unhandled exception. recordHit() is
// lock-free and allocation-free (it runs inside the VEH and must never block on
// a heap or a mutex the faulting thread might hold).
//
// Honest same-thread / dynamic-thread limitations (v1):
//   * remove() MUST be able to disarm the calling thread, because install() and
//     remove() may run on different threads. It does so via a short-lived helper
//     thread (a helper can GetThreadContext/SetThreadContext the parked caller).
//   * Threads created AFTER install() are not armed retroactively, so a hot
//     function first reached from a thread spawned later is under-counted. This
//     is a known v1 limitation; arm before spinning up worker threads.
class HardwareBreakpointObserver {
 public:
  HardwareBreakpointObserver() = default;
  ~HardwareBreakpointObserver();

  HardwareBreakpointObserver(const HardwareBreakpointObserver&) = delete;
  HardwareBreakpointObserver& operator=(const HardwareBreakpointObserver&) = delete;

  // Arm the breakpoint. Returns false if already installed, address is null, the
  // slot is in use, or no thread could be armed.
  bool install(
      const void* address,
      HwBreakSlot slot = HwBreakSlot::Dr0,
      diagnostics::AsyncLogger* logger = nullptr,
      ObservationHitCallback callback = nullptr,
      void* callback_context = nullptr);

  // Disarm cleanly. Safe to call when not installed (returns true). Idempotent.
  bool remove(diagnostics::AsyncLogger* logger = nullptr);

  [[nodiscard]] bool installed() const;
  [[nodiscard]] ObservationSummary summary() const;

  // Called by the global VEH handler. Public so the C-style handler can reach
  // it; not part of the intended user API. LOCK-FREE and ALLOCATION-FREE by
  // contract: it only touches the atomics below so it is safe to call from
  // exception-dispatch context.
  void recordHit(
      std::uint32_t thread_id,
      const ObservationRegisters& registers = {});

 private:
  // Fixed capacity for the distinct-thread set. ObservationSummary only needs
  // aggregates, so there is no per-hit log and no unbounded growth.
  static constexpr std::size_t kMaxDistinctThreads = 32;

  mutable std::mutex mutex_;  // guards installed_/slot_/address_/threads_armed_
  const void* address_ = nullptr;
  HwBreakSlot slot_ = HwBreakSlot::Dr0;
  bool installed_ = false;
  std::size_t threads_armed_ = 0;

  // Lock-free, fixed-capacity hit aggregates written by recordHit() (inside the
  // VEH) and read by summary(). steady_clock nanoseconds since its epoch.
  std::atomic<std::uint64_t> hit_count_{0};
  std::atomic<std::int64_t> first_hit_ns_{0};
  std::atomic<std::int64_t> last_hit_ns_{0};
  std::atomic<std::uint32_t> distinct_thread_ids_[kMaxDistinctThreads]{};
  std::atomic<std::size_t> distinct_thread_count_{0};
  std::atomic<ObservationHitCallback> callback_{nullptr};
  std::atomic<void*> callback_context_{nullptr};
};

}  // namespace vrclient::tooling::hookdisc

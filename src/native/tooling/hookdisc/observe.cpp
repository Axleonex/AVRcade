#include "tooling/hookdisc/observe.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>

namespace vrclient::tooling::hookdisc {
namespace {

// One global watch slot per hardware debug register. The Vectored Exception
// Handler is process-global, so it locates the owning observer through this
// table. Reads in the handler use atomics; mutation is serialized by the
// registry mutex.
struct ActiveWatch {
  std::atomic<const void*> address{nullptr};
  std::atomic<HardwareBreakpointObserver*> owner{nullptr};
  // Last successfully disarmed address. A CPU exception already in dispatch
  // can arrive after remove() clears owner; exact DR slot + address matching
  // lets the VEH finish only that retired hit without swallowing foreign steps.
  std::atomic<const void*> retiring_address{nullptr};
};

std::array<ActiveWatch, 4> g_watches{};
std::mutex g_registry_mutex;
PVOID g_veh_handle = nullptr;
int g_veh_refcount = 0;

// Count of vectored-handler invocations that have committed to dereferencing an
// observer's owner pointer. remove()/the destructor clear the registry slot and
// then spin until this reaches zero, guaranteeing no in-flight handler is still
// touching the observer before the VEH is unregistered or the object destroyed.
std::atomic<int> g_handler_in_flight{0};

constexpr DWORD64 kResumeFlag = 0x10000;  // EFLAGS.RF
constexpr DWORD kTrapFlag = 0x100;        // EFLAGS.TF
constexpr DWORD64 kDr6SingleStep = DWORD64{1} << 14;  // DR6.BS

LONG CALLBACK vectoredHandler(EXCEPTION_POINTERS* info) {
  // We claim EXCEPTION_SINGLE_STEP only; every other exception (access
  // violations, software breakpoints, C++ exceptions) is passed straight through.
  if (info == nullptr || info->ExceptionRecord == nullptr ||
      info->ContextRecord == nullptr) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  CONTEXT* ctx = info->ContextRecord;
  const DWORD64 dr6 = ctx->Dr6;
  bool claimed = false;

  // Record a hit for any currently-armed slot whose hardware breakpoint (DR6
  // status bit B0..B3) fired at the faulting RIP.
  for (int slot = 0; slot < 4; ++slot) {
    if ((dr6 & (DWORD64{1} << slot)) == 0) {
      continue;
    }
    // Publish that a handler is in flight for this slot BEFORE reading the owner.
    // A concurrent remove() that clears the slot then drains on us rather than
    // freeing the observer out from under this call (use-after-free guard).
    g_handler_in_flight.fetch_add(1, std::memory_order_acquire);
    const void* address = g_watches[static_cast<std::size_t>(slot)].address.load(
        std::memory_order_acquire);
    HardwareBreakpointObserver* owner =
        g_watches[static_cast<std::size_t>(slot)].owner.load(std::memory_order_acquire);
    bool slot_claimed = false;
    if (address != nullptr && owner != nullptr &&
        ctx->Rip == reinterpret_cast<DWORD64>(address)) {
      slot_claimed = true;
      owner->recordHit(
          static_cast<std::uint32_t>(GetCurrentThreadId()),
          {ctx->Rip, ctx->Rcx, ctx->Rdx, ctx->R8, ctx->R9});
    } else {
      const void* retiring_address =
          g_watches[static_cast<std::size_t>(slot)].retiring_address.load(
              std::memory_order_acquire);
      slot_claimed = retiring_address != nullptr &&
                     ctx->Rip == reinterpret_cast<DWORD64>(retiring_address);
    }
    if (slot_claimed) {
      claimed = true;
      ctx->Dr6 &= ~(DWORD64{1} << slot);  // clear our breakpoint status bit
    }
    g_handler_in_flight.fetch_sub(1, std::memory_order_release);
  }

  // SetThreadContext can clear DR6/DR7 while an already-raised hardware
  // exception is still pending delivery. In that teardown-only case the OS
  // retains the exact exception address/RIP but no B0..B3 ownership bit. Claim
  // only an exact retired address with no competing hardware-slot status.
  if (!claimed && (dr6 & DWORD64{0xF}) == 0 &&
      info->ExceptionRecord->ExceptionAddress ==
          reinterpret_cast<void*>(ctx->Rip)) {
    for (const auto& watch : g_watches) {
      const void* retiring_address =
          watch.retiring_address.load(std::memory_order_acquire);
      if (retiring_address != nullptr &&
          ctx->Rip == reinterpret_cast<DWORD64>(retiring_address)) {
        claimed = true;
        break;
      }
    }
  }

  if (!claimed) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  // Resume only the exact hardware-breakpoint exception claimed above.
  ctx->Dr6 &= ~kDr6SingleStep;
  ctx->EFlags &= ~kTrapFlag;
  ctx->EFlags |= static_cast<DWORD>(kResumeFlag);
  return EXCEPTION_CONTINUE_EXECUTION;
}

void setDebugRegister(CONTEXT& ctx, int slot, DWORD64 value) {
  switch (slot) {
    case 0: ctx.Dr0 = value; break;
    case 1: ctx.Dr1 = value; break;
    case 2: ctx.Dr2 = value; break;
    case 3: ctx.Dr3 = value; break;
    default: break;
  }
}

// Arm (or disarm) a single execute hardware breakpoint on one thread by
// suspend -> get-context -> edit debug registers -> set-context -> resume. This
// is read-only with respect to the target's code: only the thread's own debug
// registers change; no instruction bytes are touched.
bool applyToThread(DWORD thread_id, const void* address, int slot, bool arm) {
  HANDLE thread = OpenThread(
      THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME,
      FALSE,
      thread_id);
  if (thread == nullptr) {
    return false;
  }

  bool ok = false;
  if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
    alignas(16) CONTEXT ctx;
    std::memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(thread, &ctx)) {
      const DWORD64 enable_bit = DWORD64{1} << (slot * 2);          // Ln local enable
      const DWORD64 condlen_mask = DWORD64{0xF} << (16 + slot * 4);  // R/Wn + LENn
      if (arm) {
        setDebugRegister(ctx, slot, reinterpret_cast<DWORD64>(address));
        ctx.Dr7 |= enable_bit;
        ctx.Dr7 &= ~condlen_mask;  // R/W=00 (execute), LEN=00 (1 byte)
      } else {
        setDebugRegister(ctx, slot, 0);
        ctx.Dr7 &= ~enable_bit;
        ctx.Dr7 &= ~condlen_mask;
      }
      ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
      ok = SetThreadContext(thread, &ctx) != FALSE;
    }
    ResumeThread(thread);
  }

  CloseHandle(thread);
  return ok;
}

std::vector<DWORD> enumerateOtherThreads(bool* complete = nullptr) {
  if (complete != nullptr) {
    *complete = false;
  }
  std::vector<DWORD> thread_ids;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return thread_ids;
  }
  const DWORD my_pid = GetCurrentProcessId();
  const DWORD my_tid = GetCurrentThreadId();

  THREADENTRY32 entry;
  std::memset(&entry, 0, sizeof(entry));
  entry.dwSize = sizeof(entry);
  bool enumerated = false;
  if (Thread32First(snapshot, &entry)) {
    do {
      if (entry.th32OwnerProcessID == my_pid && entry.th32ThreadID != my_tid) {
        thread_ids.push_back(entry.th32ThreadID);
      }
    } while (Thread32Next(snapshot, &entry));
    enumerated = GetLastError() == ERROR_NO_MORE_FILES;
  }
  CloseHandle(snapshot);
  if (complete != nullptr) {
    *complete = enumerated;
  }
  return thread_ids;
}

// A thread may legitimately exit between the process snapshot and OpenThread /
// SetThreadContext. Treat only a definitively exited thread as already clean;
// access errors and other unknown states remain teardown failures.
bool threadHasExited(DWORD thread_id) {
  HANDLE thread = OpenThread(SYNCHRONIZE, FALSE, thread_id);
  if (thread == nullptr) {
    return GetLastError() == ERROR_INVALID_PARAMETER;
  }
  const DWORD wait_result = WaitForSingleObject(thread, 0);
  CloseHandle(thread);
  return wait_result == WAIT_OBJECT_0;
}

// Worker entry point used to disarm the thread that called remove(). A thread
// cannot reliably GetThreadContext/SetThreadContext itself while running, so a
// short-lived helper suspends the (parked) caller and clears its DRs.
struct DisarmSelfContext {
  DWORD thread_id = 0;
  int slot = 0;
};

DWORD WINAPI disarmSelfWorker(LPVOID param) {
  auto* ctx = static_cast<DisarmSelfContext*>(param);
  return applyToThread(ctx->thread_id, nullptr, ctx->slot, false) ? 1 : 0;
}

// Clear the calling thread's own debug registers for one slot. install() never
// arms the caller, but install() and remove() may run on different threads, so
// the remove() caller may itself be an armed thread; leaving its DR enabled
// after the VEH is gone would crash on the next hit.
bool disarmCurrentThread(int slot_index) {
  DisarmSelfContext ctx;
  ctx.thread_id = GetCurrentThreadId();
  ctx.slot = slot_index;
  HANDLE worker = CreateThread(nullptr, 0, disarmSelfWorker, &ctx, 0, nullptr);
  if (worker == nullptr) {
    return false;
  }
  const DWORD wait_result = WaitForSingleObject(worker, INFINITE);
  DWORD worker_result = 0;
  const bool ok = wait_result == WAIT_OBJECT_0 &&
                  GetExitCodeThread(worker, &worker_result) != FALSE &&
                  worker_result == 1;
  CloseHandle(worker);
  return ok;
}

void logEvent(
    diagnostics::AsyncLogger* logger,
    diagnostics::Severity severity,
    std::string_view event,
    std::initializer_list<diagnostics::LogField> fields) {
  if (logger != nullptr) {
    logger->log(severity, event, fields);
  }
}

}  // namespace

long testing::dispatchObservationException(void* exception_pointers) {
  return vectoredHandler(static_cast<EXCEPTION_POINTERS*>(exception_pointers));
}

HardwareBreakpointObserver::~HardwareBreakpointObserver() {
  // The registry holds a raw owner pointer. If teardown transiently fails, the
  // object cannot safely finish destruction while any thread may remain armed.
  while (!remove(nullptr)) {
    SwitchToThread();
  }
}

bool HardwareBreakpointObserver::install(
    const void* address,
    HwBreakSlot slot,
    diagnostics::AsyncLogger* logger,
    ObservationHitCallback callback,
    void* callback_context) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (installed_) {
    return false;
  }
  if (address == nullptr) {
    return false;
  }
  const int slot_index = static_cast<int>(slot);

  {
    std::lock_guard<std::mutex> registry(g_registry_mutex);
    if (g_watches[static_cast<std::size_t>(slot_index)].owner.load() != nullptr) {
      return false;  // slot already in use by another observer
    }
    callback_context_.store(callback_context, std::memory_order_relaxed);
    callback_.store(callback, std::memory_order_release);
    g_watches[static_cast<std::size_t>(slot_index)].address.store(address);
    g_watches[static_cast<std::size_t>(slot_index)].owner.store(this);
    // Install the process-global handler exactly once and then LEAVE IT
    // REGISTERED for the life of the process. Unregistering it per-observer is
    // unsafe: a hardware single-step can be raised by the CPU and be in flight
    // toward the handler before remove() runs, and RemoveVectoredExceptionHandler
    // during that window turns it into an unhandled exception (process crash).
    // The handler is inert when no slot is armed (it claims an exception only on
    // a matching DR6 bit + RIP + non-null owner), so keeping it costs nothing.
    if (g_veh_handle == nullptr) {
      g_veh_handle = AddVectoredExceptionHandler(1, vectoredHandler);
      if (g_veh_handle == nullptr) {
        g_watches[static_cast<std::size_t>(slot_index)].address.store(nullptr);
        g_watches[static_cast<std::size_t>(slot_index)].owner.store(nullptr);
        callback_.store(nullptr, std::memory_order_release);
        callback_context_.store(nullptr, std::memory_order_release);
        return false;
      }
    }
    ++g_veh_refcount;
  }

  hit_count_.store(0, std::memory_order_relaxed);
  first_hit_ns_.store(0, std::memory_order_relaxed);
  last_hit_ns_.store(0, std::memory_order_relaxed);
  distinct_thread_count_.store(0, std::memory_order_relaxed);
  for (auto& id : distinct_thread_ids_) {
    id.store(0, std::memory_order_relaxed);
  }

  std::size_t armed = 0;
  for (const DWORD thread_id : enumerateOtherThreads()) {
    if (applyToThread(thread_id, address, slot_index, true)) {
      ++armed;
    }
  }

  if (armed == 0) {
    // Nothing to observe — roll back the slot. No thread was armed, so no
    // single-step can be in flight; the inert process-global handler stays
    // registered (see install note above).
    std::lock_guard<std::mutex> registry(g_registry_mutex);
    g_watches[static_cast<std::size_t>(slot_index)].address.store(nullptr);
    g_watches[static_cast<std::size_t>(slot_index)].owner.store(nullptr);
    if (g_veh_refcount > 0) {
      --g_veh_refcount;
    }
    callback_.store(nullptr, std::memory_order_release);
    callback_context_.store(nullptr, std::memory_order_release);
    logEvent(logger, diagnostics::Severity::Warning, "hookdisc_observe_install_failed",
             {{"reason", "no_threads_armed"}});
    return false;
  }

  address_ = address;
  slot_ = slot;
  installed_ = true;
  threads_armed_ = armed;

  logEvent(logger, diagnostics::Severity::Info, "hookdisc_observe_installed",
           {{"slot", std::to_string(slot_index)},
            {"threads_armed", std::to_string(armed)}});
  return true;
}

bool HardwareBreakpointObserver::remove(diagnostics::AsyncLogger* logger) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (!installed_) {
    return true;
  }
  const int slot_index = static_cast<int>(slot_);

  // 1. Disarm every other live thread so no NEW hit can be generated. Threads
  //    that exited after the snapshot are already clean; failures against a
  //    live or unknown thread leave the observer installed so removal can be
  //    retried without dropping the VEH owner while a breakpoint remains armed.
  bool enumeration_complete = false;
  std::size_t disarm_failures = 0;
  for (const DWORD thread_id : enumerateOtherThreads(&enumeration_complete)) {
    if (!applyToThread(thread_id, nullptr, slot_index, false) &&
        !threadHasExited(thread_id)) {
      ++disarm_failures;
    }
  }
  // 2. Disarm the calling thread too (it may itself be armed if install() ran on
  //    a different thread). Without this its DR survives the VEH removal below.
  if (!disarmCurrentThread(slot_index)) {
    ++disarm_failures;
  }

  if (!enumeration_complete || disarm_failures != 0) {
    logEvent(logger, diagnostics::Severity::Warning, "hookdisc_observe_remove_failed",
             {{"slot", std::to_string(slot_index)},
              {"thread_enumeration_complete", enumeration_complete ? "true" : "false"},
              {"disarm_failures", std::to_string(disarm_failures)}});
    return false;
  }

  // 3. Retire the exact address, then clear its owner. A hardware exception
  //    already in CPU/OS dispatch can still be recognized without dereferencing
  //    the observer; unrelated single-steps continue exception search.
  {
    std::lock_guard<std::mutex> registry(g_registry_mutex);
    g_watches[static_cast<std::size_t>(slot_index)].retiring_address.store(
        address_, std::memory_order_release);
    g_watches[static_cast<std::size_t>(slot_index)].address.store(nullptr);
    g_watches[static_cast<std::size_t>(slot_index)].owner.store(nullptr);
  }

  // 4. Drain handlers already in flight that may hold this owner pointer, BEFORE
  //    returning (the destructor calls remove(), so a handler must never run
  //    recordHit() against a half-destroyed object). A handler that enters after
  //    step 3 can claim only the exact retired slot/address and never dereferences
  //    the cleared owner. recordHit is lock-free, so this spin cannot deadlock.
  while (g_handler_in_flight.load(std::memory_order_acquire) > 0) {
    YieldProcessor();
  }
  callback_.store(nullptr, std::memory_order_release);
  callback_context_.store(nullptr, std::memory_order_release);

  // 5. The process-global VEH is intentionally LEFT REGISTERED (see install
  //    note) — unregistering it here would race a single-step already in flight.
  //    It retains only the exact retired slot/address identity needed for a late
  //    dispatch. Track the active-observer count for diagnostics only.
  {
    std::lock_guard<std::mutex> registry(g_registry_mutex);
    if (g_veh_refcount > 0) {
      --g_veh_refcount;
    }
  }

  installed_ = false;
  address_ = nullptr;

  logEvent(logger, diagnostics::Severity::Info, "hookdisc_observe_removed",
           {{"slot", std::to_string(slot_index)},
            {"hit_count", std::to_string(hit_count_.load(std::memory_order_acquire))}});
  return true;
}

bool HardwareBreakpointObserver::installed() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return installed_;
}

void HardwareBreakpointObserver::recordHit(
    std::uint32_t thread_id,
    const ObservationRegisters& registers) {
  // LOCK-FREE / ALLOCATION-FREE: runs inside the vectored exception handler.
  const std::int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count();
  const std::uint64_t index = hit_count_.fetch_add(1, std::memory_order_relaxed);
  if (index == 0) {
    first_hit_ns_.store(now, std::memory_order_relaxed);
  }
  last_hit_ns_.store(now, std::memory_order_relaxed);

  const auto callback = callback_.load(std::memory_order_acquire);
  if (callback != nullptr) {
    callback(
        registers, thread_id,
        callback_context_.load(std::memory_order_acquire));
  }

  // Best-effort distinct-thread capture into a fixed, lock-free table.
  if (thread_id == 0) {
    return;
  }
  const std::size_t known = distinct_thread_count_.load(std::memory_order_acquire);
  for (std::size_t i = 0; i < known && i < kMaxDistinctThreads; ++i) {
    if (distinct_thread_ids_[i].load(std::memory_order_relaxed) == thread_id) {
      return;
    }
  }
  for (std::size_t slot = known; slot < kMaxDistinctThreads; ++slot) {
    std::uint32_t expected = 0;
    if (distinct_thread_ids_[slot].compare_exchange_strong(
            expected, thread_id, std::memory_order_acq_rel)) {
      distinct_thread_count_.fetch_add(1, std::memory_order_release);
      return;
    }
    if (distinct_thread_ids_[slot].load(std::memory_order_relaxed) == thread_id) {
      return;  // a concurrent hit registered the same thread id
    }
  }
}

ObservationSummary HardwareBreakpointObserver::summary() const {
  std::lock_guard<std::mutex> guard(mutex_);
  ObservationSummary out;
  out.installed = installed_;
  out.threads_armed = threads_armed_;

  const std::uint64_t count = hit_count_.load(std::memory_order_acquire);
  out.hit_count = count;

  const std::size_t distinct =
      distinct_thread_count_.load(std::memory_order_acquire);
  for (std::size_t i = 0; i < distinct && i < kMaxDistinctThreads; ++i) {
    const std::uint32_t tid = distinct_thread_ids_[i].load(std::memory_order_relaxed);
    if (tid != 0) {
      out.distinct_thread_ids.push_back(tid);
    }
  }

  if (count >= 2) {
    const std::int64_t first = first_hit_ns_.load(std::memory_order_relaxed);
    const std::int64_t last = last_hit_ns_.load(std::memory_order_relaxed);
    const double seconds = static_cast<double>(last - first) / 1e9;
    out.window_seconds = seconds;
    if (seconds > 0.0) {
      out.observed_cadence_hz = static_cast<double>(count - 1) / seconds;
    }
  }
  return out;
}

}  // namespace vrclient::tooling::hookdisc

// Bolt-on Phase B2 unit tests for the hook-discovery toolkit.
//   * RE-01 inspection: module enumeration + PE export search + RTTI scan.
//   * hook_surface_doc: round-trip + refusal cases + evidence-gated promotion.
//   * RE-02 observation sandbox: hardware-breakpoint hit counting across threads
//     and clean removal with no residue.

#include "tooling/hookdisc/hook_surface_doc.h"
#include "tooling/hookdisc/inspection.h"
#include "tooling/hookdisc/observe.h"
#include "tooling/hookdisc/safety_gate.h"

#include "injector/process/process_discovery.h"
#include "versioning/game_fingerprint.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <typeinfo>
#include <vector>

namespace {

namespace hookdisc = vrclient::tooling::hookdisc;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::filesystem::path repoRoot() {
#if defined(VRCLIENT_SOURCE_DIR)
  return std::filesystem::path(VRCLIENT_SOURCE_DIR);
#else
  return std::filesystem::current_path();
#endif
}

// ---------------------------------------------------------------------------
// RE-01: inspection harness
// ---------------------------------------------------------------------------
class PolymorphicProbe {
 public:
  virtual ~PolymorphicProbe() = default;
  virtual int value() const { return 7; }
};

void testModuleAndExportEnumeration() {
  const auto modules = hookdisc::enumerateCurrentProcessModules();
  expect(modules.ok, "module enumeration must succeed");
  expect(modules.modules.size() >= 2, "expected at least a couple of modules");

  // Own module: its base equals GetModuleHandle(nullptr).
  const std::uintptr_t self_base =
      reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
  bool found_self = false;
  for (const auto& module : modules.modules) {
    if (module.base == self_base) {
      found_self = true;
      break;
    }
  }
  expect(found_self, "enumeration must include this test executable's own module");

  // Known system module.
  const hookdisc::ModuleInfo* kernel32 = hookdisc::findModule(modules, "kernel32.dll");
  expect(kernel32 != nullptr, "enumeration must include kernel32.dll");

  const auto exports = hookdisc::enumerateCurrentProcessExports(*kernel32);
  expect(exports.ok, "kernel32 export enumeration must succeed");
  expect(!exports.exports.empty(), "kernel32 must expose exports");

  const auto hits = hookdisc::searchExportSymbols(exports, {"GetProcAddress"});
  bool exact = false;
  for (const auto& hit : hits) {
    if (hit.symbol == "GetProcAddress" && hit.exact) {
      exact = true;
      expect(hit.address != 0, "export address must be non-zero");
    }
  }
  expect(exact, "export search must find kernel32!GetProcAddress");
}

void testRttiScan() {
  // Force a type descriptor for the probe class to exist in this module.
  const PolymorphicProbe probe;
  volatile const char* probe_type = typeid(probe).name();
  (void)probe_type;

  const auto modules = hookdisc::enumerateCurrentProcessModules();
  expect(modules.ok, "module enumeration must succeed for RTTI scan");
  const std::uintptr_t self_base =
      reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
  const hookdisc::ModuleInfo* self = nullptr;
  for (const auto& module : modules.modules) {
    if (module.base == self_base) {
      self = &module;
      break;
    }
  }
  expect(self != nullptr, "must locate own module for RTTI scan");

  const auto scan = hookdisc::scanCurrentProcessRtti(*self, {});
  expect(scan.ok, "RTTI scan must complete");
  expect(scan.bytes_scanned > 0, "RTTI scan must read some readable image bytes");
  expect(!scan.type_descriptors.empty(),
         "RTTI scan must find at least one .?AV/.?AU type descriptor");
}

// ---------------------------------------------------------------------------
// hook_surface_doc: round-trip + refusals
// ---------------------------------------------------------------------------
hookdisc::HookSurfaceDoc makeValidDoc() {
  hookdisc::HookSurfaceDoc doc;
  doc.version = 1;
  doc.game_id = "repo";
  doc.build_id = "steam-3241660-build-23363152";
  doc.generated_by = "hookdisc unit test";
  doc.generated_at = "2026-06-12T12:00:00Z";

  hookdisc::HookEntry candidate;
  candidate.name = "MainCamera";
  candidate.area = hookdisc::HookArea::Camera;
  candidate.module = "Assembly-CSharp.dll";
  candidate.symbol = "MainCamera";
  candidate.status = hookdisc::HookStatus::Candidate;
  candidate.confidence = hookdisc::HookConfidence::Low;
  doc.hooks.push_back(candidate);

  hookdisc::HookEntry validated;
  validated.name = "CameraUpdate";
  validated.area = hookdisc::HookArea::Camera;
  validated.module = "Assembly-CSharp.dll";
  validated.offset = "0x1234abcd";
  validated.status = hookdisc::HookStatus::Validated;
  validated.confidence = hookdisc::HookConfidence::High;
  validated.has_validation = true;
  validated.validation.method = hookdisc::ValidationMethod::HardwareBreakpoint;
  validated.validation.has_cadence_hz = true;
  validated.validation.cadence_hz = 72.0;
  validated.validation.thread_context = hookdisc::ThreadContext::Render;
  validated.validation.hit_count = 144;
  validated.validation.captured_at = "2026-06-12T12:05:00Z";
  doc.hooks.push_back(validated);

  return doc;
}

void testDocRoundTrip() {
  const hookdisc::HookSurfaceDoc doc = makeValidDoc();
  const auto validation = hookdisc::validateHookSurfaceDoc(doc);
  expect(validation.ok, "constructed doc must validate: " + validation.message);

  const std::filesystem::path out =
      std::filesystem::temp_directory_path() / "vr_hookdisc_roundtrip.json";
  std::string error;
  expect(hookdisc::saveHookSurfaceDoc(doc, out, error), "save must succeed: " + error);

  const auto loaded = hookdisc::loadHookSurfaceDoc(out);
  expect(loaded.loaded, "reload must succeed: " + loaded.message);
  expect(loaded.doc.hooks.size() == 2, "round-trip hook count mismatch");
  expect(loaded.doc.game_id == "repo", "round-trip game_id mismatch");
  expect(loaded.doc.hooks[1].status == hookdisc::HookStatus::Validated,
         "round-trip status mismatch");
  expect(loaded.doc.hooks[1].has_validation, "round-trip lost validation evidence");
  expect(loaded.doc.hooks[1].validation.hit_count == 144,
         "round-trip hit_count mismatch");
  expect(loaded.doc.hooks[1].validation.has_cadence_hz &&
             loaded.doc.hooks[1].validation.cadence_hz > 0.0,
         "round-trip cadence mismatch");

  std::error_code ec;
  std::filesystem::remove(out, ec);
}

void testDocRefusals() {
  // Validated without evidence is refused.
  hookdisc::HookSurfaceDoc no_evidence = makeValidDoc();
  no_evidence.hooks[1].has_validation = false;
  expect(!hookdisc::validateHookSurfaceDoc(no_evidence).ok,
         "validated hook without evidence must be refused");

  // Validated with incomplete evidence (no cadence and no per_frame) is refused.
  hookdisc::HookSurfaceDoc incomplete = makeValidDoc();
  incomplete.hooks[1].validation.has_cadence_hz = false;
  incomplete.hooks[1].validation.has_per_frame = false;
  expect(!hookdisc::validateHookSurfaceDoc(incomplete).ok,
         "validated hook without cadence/per_frame must be refused");

  // Wrong version is refused.
  hookdisc::HookSurfaceDoc wrong_version = makeValidDoc();
  wrong_version.version = 2;
  expect(!hookdisc::validateHookSurfaceDoc(wrong_version).ok,
         "wrong version must be refused");

  // Blocked without reason is refused.
  hookdisc::HookSurfaceDoc blocked = makeValidDoc();
  blocked.hooks[0].status = hookdisc::HookStatus::Blocked;
  blocked.hooks[0].blocked_reason.clear();
  expect(!hookdisc::validateHookSurfaceDoc(blocked).ok,
         "blocked hook without reason must be refused");

  // Missing locator is refused.
  hookdisc::HookSurfaceDoc no_locator = makeValidDoc();
  no_locator.hooks[0].symbol.clear();
  no_locator.hooks[0].signature.clear();
  no_locator.hooks[0].offset.clear();
  expect(!hookdisc::validateHookSurfaceDoc(no_locator).ok,
         "hook without symbol/signature/offset must be refused");
}

void testPromotionGate() {
  hookdisc::HookEntry hook;
  hook.name = "GrabBeamLogic";
  hook.area = hookdisc::HookArea::Interaction;
  hook.module = "Assembly-CSharp.dll";
  hook.symbol = "GrabBeamLogic";
  hook.status = hookdisc::HookStatus::Candidate;
  hook.confidence = hookdisc::HookConfidence::Low;

  hookdisc::HookValidationEvidence incomplete;
  incomplete.method = hookdisc::ValidationMethod::HardwareBreakpoint;
  incomplete.thread_context = hookdisc::ThreadContext::Main;
  incomplete.hit_count = 0;  // invalid
  std::string error;
  expect(!hookdisc::promoteHookToValidated(hook, incomplete, error),
         "promotion with hit_count 0 must be refused");
  expect(hook.status == hookdisc::HookStatus::Candidate,
         "refused promotion must not change status");

  hookdisc::HookValidationEvidence complete;
  complete.method = hookdisc::ValidationMethod::HardwareBreakpoint;
  complete.has_per_frame = true;
  complete.per_frame = true;
  complete.thread_context = hookdisc::ThreadContext::Worker;
  complete.hit_count = 240;
  complete.captured_at = "2026-06-12T12:10:00Z";
  expect(hookdisc::promoteHookToValidated(hook, complete, error),
         "complete evidence must promote: " + error);
  expect(hook.status == hookdisc::HookStatus::Validated,
         "promotion must mark hook validated");
  expect(hookdisc::validateHookSurfaceDoc(
             hookdisc::HookSurfaceDoc{1, "repo", "steam-3241660-build-23363152",
                                      "test", "2026-06-12T12:00:00Z", {hook}})
             .ok,
         "promoted hook must satisfy document validation");
}

void testLoadRealRepoSurface() {
  const std::filesystem::path repo_doc =
      repoRoot() / "config" / "hooks" / "repo.json";
  if (!std::filesystem::exists(repo_doc)) {
    return;  // tolerated when run outside the source tree
  }
  const auto loaded = hookdisc::loadHookSurfaceDoc(repo_doc);
  expect(loaded.loaded, "config/hooks/repo.json must load: " + loaded.message);
  expect(loaded.doc.game_id == "repo", "repo surface game_id mismatch");
  expect(loaded.doc.hooks.size() >= 1, "repo surface must have hooks");
}

// ---------------------------------------------------------------------------
// RE-05: safety gate behavioral coverage (B2 success criterion 3 — "the harness
// refuses any target the safety preflight does not approve"). These assert the
// gate's verdicts directly, not just that the source contains the right strings.
// ---------------------------------------------------------------------------
namespace process = vrclient::injector::process;
namespace versioning = vrclient::versioning;

// Mirror of the CLI's modeled-target builder: derive a TargetDescriptor's
// observed-evidence fields from a fingerprint config so the gate's identity leg
// resolves. This is dry-run modeling only.
process::TargetDescriptor modelTargetFromConfig(
    const versioning::GameFingerprintConfig& config) {
  process::TargetDescriptor target;
  bool has_steam = false;
  bool has_direct = false;
  bool has_manual = false;
  for (const std::string& source : config.support_policy.allowed_sources) {
    if (source == "steam") has_steam = true;
    if (source == "direct") has_direct = true;
    if (source == "manual") has_manual = true;
  }
  if (has_steam) {
    target.flow = process::DiscoveryFlow::SteamLaunch;
  } else if (has_direct) {
    target.flow = process::DiscoveryFlow::DirectLaunch;
  } else if (has_manual) {
    target.flow = process::DiscoveryFlow::ManualPath;
  }
  if (!config.executable_names.empty()) {
    target.executable_path = config.executable_names.front();
  }
  target.game_id_hint = config.game_id;
  target.architecture = config.support_policy.target_architecture;
  for (const versioning::BuildFingerprint& build : config.builds) {
    if (build.supported && !build.file_hashes.empty()) {
      target.executable_sha256 = build.file_hashes.front().value;
      target.build_id_hint = build.build_id;
      if (!build.product_versions.empty()) {
        target.product_version = build.product_versions.front();
      }
      break;
    }
  }
  target.identity_evidence_trusted = true;
  target.identity_evidence_source = "hookdisc_test_modeled";
  return target;
}

versioning::GameFingerprintConfig loadConfigOrThrow(const std::string& slug) {
  const std::filesystem::path path =
      repoRoot() / "config" / "games" / (slug + ".json");
  const auto loaded = versioning::loadGameFingerprintConfig(path);
  expect(loaded.loaded, "fingerprint config must load: " + slug + " (" + loaded.message + ")");
  return loaded.config;
}

void testSafetyGate() {
  const std::filesystem::path smoke_path =
      repoRoot() / "config" / "games" / "sample-game.json";
  const std::filesystem::path repo_path =
      repoRoot() / "config" / "games" / "repo.json";
  if (!std::filesystem::exists(smoke_path) || !std::filesystem::exists(repo_path)) {
    return;  // tolerated when run outside the source tree
  }

  const auto smoke_config = loadConfigOrThrow("sample-game");
  const auto repo_config = loadConfigOrThrow("repo");

  // 1. Controlled smoke host, dry-run model => APPROVED autonomously.
  {
    const process::TargetDescriptor target = modelTargetFromConfig(smoke_config);
    hookdisc::HookSurfaceGateOptions options;
    options.dry_run_model = true;
    const auto outcome =
        hookdisc::runHookSurfaceSafetyGate(smoke_path, target, options);
    expect(outcome.approved,
           "controlled smoke host must approve autonomously (reason=" +
               outcome.reason_code + ")");
    expect(outcome.dry_run, "dry-run modeled outcome must report dry_run");
    expect(outcome.controlled_smoke_target, "smoke target must be flagged");
  }

  // 2. Finding-1 guard: a LIVE (non-dry-run) smoke run with no observed hash must
  //    NOT self-satisfy the build-hash check — it refuses runtime_hash_missing.
  {
    const process::TargetDescriptor target = modelTargetFromConfig(smoke_config);
    hookdisc::HookSurfaceGateOptions options;  // dry_run_model = false, no observed hash
    const auto outcome =
        hookdisc::runHookSurfaceSafetyGate(smoke_path, target, options);
    expect(!outcome.approved,
           "live smoke run without observed hash must refuse (got approved)");
    expect(outcome.reason_code == "runtime_hash_missing",
           "missing observed hash must refuse runtime_hash_missing, got " +
               outcome.reason_code);
  }

  // 3. Finding-2 guard: a commercial target (R.E.P.O.) under default autonomous
  //    options must refuse — the autonomy invariant is code-enforced, not config.
  {
    const process::TargetDescriptor target = modelTargetFromConfig(repo_config);
    hookdisc::HookSurfaceGateOptions options;
    options.dry_run_model = true;
    const auto outcome =
        hookdisc::runHookSurfaceSafetyGate(repo_path, target, options);
    expect(!outcome.approved, "R.E.P.O. must refuse under autonomous options");
    expect(outcome.reason_code == "not_controlled_smoke_target",
           "autonomous commercial target must refuse not_controlled_smoke_target, got " +
               outcome.reason_code);
  }

  // 4. Human-authorized live R.E.P.O.: explicit private/modded confirmation +
  //    operator authorization + a REAL matching observed hash => APPROVED. Proves
  //    the gate's positive live path still works behind the human gate.
  {
    process::TargetDescriptor target = modelTargetFromConfig(repo_config);
    hookdisc::HookSurfaceGateOptions options;
    options.confirm_private_modded_session = true;
    options.confirm_mod_compatibility = true;
    options.operator_authorized_live_target = true;
    options.observed_runtime_sha256 = repo_config.runtime.sha256;
    const auto outcome =
        hookdisc::runHookSurfaceSafetyGate(repo_path, target, options);
    expect(outcome.approved,
           "human-authorized R.E.P.O. with matching hash must approve (reason=" +
               outcome.reason_code + ")");
    expect(!outcome.dry_run, "live-style run must not be flagged dry_run");
  }

  // 5. Human-authorized R.E.P.O. but a MISMATCHED observed hash => refuse.
  {
    process::TargetDescriptor target = modelTargetFromConfig(repo_config);
    hookdisc::HookSurfaceGateOptions options;
    options.confirm_private_modded_session = true;
    options.confirm_mod_compatibility = true;
    options.operator_authorized_live_target = true;
    options.observed_runtime_sha256 =
        "0000000000000000000000000000000000000000000000000000000000000000";
    const auto outcome =
        hookdisc::runHookSurfaceSafetyGate(repo_path, target, options);
    expect(!outcome.approved, "mismatched observed hash must refuse");
    expect(outcome.reason_code == "runtime_hash_mismatch",
           "mismatched hash must refuse runtime_hash_mismatch, got " +
               outcome.reason_code);
  }
}

// ---------------------------------------------------------------------------
// RE-02: observation sandbox
// ---------------------------------------------------------------------------
volatile long long g_observe_sink = 0;

__declspec(noinline) int observeTargetFunction() {
  g_observe_sink += 1;
  return static_cast<int>(g_observe_sink & 0x7);
}

using ObserveFn = int (*)();
volatile ObserveFn g_observe_fp = &observeTargetFunction;

void testObservationSandbox() {
  const int kCallsPerPhase = 64;

  HANDLE start1 = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HANDLE start2 = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  expect(start1 != nullptr && start2 != nullptr, "events must be created");

  std::atomic<int> done1{0};
  std::atomic<int> done2{0};

  auto worker = [&]() {
    WaitForSingleObject(start1, INFINITE);
    for (int i = 0; i < kCallsPerPhase; ++i) {
      g_observe_sink += g_observe_fp();
    }
    done1.fetch_add(1);
    WaitForSingleObject(start2, INFINITE);
    for (int i = 0; i < kCallsPerPhase; ++i) {
      g_observe_sink += g_observe_fp();
    }
    done2.fetch_add(1);
  };

  std::thread t1(worker);
  std::thread t2(worker);

  // Arm address == the exact address the workers will execute (avoids any
  // incremental-link thunk mismatch).
  ObserveFn fp = g_observe_fp;
  std::uintptr_t addr_value = 0;
  std::memcpy(&addr_value, &fp, sizeof(fp));
  const void* arm_address = reinterpret_cast<const void*>(addr_value);

  hookdisc::HardwareBreakpointObserver observer;
  const bool installed =
      observer.install(arm_address, hookdisc::HwBreakSlot::Dr0);
  expect(installed, "hardware breakpoint must install");
  expect(observer.installed(), "observer must report installed");

  SetEvent(start1);
  while (done1.load() < 2) {
    Sleep(1);
  }

  const auto after_phase1 = observer.summary();
  expect(after_phase1.hit_count == static_cast<std::uint64_t>(2 * kCallsPerPhase),
         "hit count must equal calls across both threads (got " +
             std::to_string(after_phase1.hit_count) + ")");
  expect(after_phase1.distinct_thread_ids.size() == 2,
         "exactly two worker threads must be captured");
  expect(after_phase1.threads_armed >= 2, "at least the two workers must be armed");

  // Clean removal: no byte was ever patched, so the function stays callable and
  // the previously armed threads stop producing hits.
  expect(observer.remove(), "observer must remove cleanly");
  expect(!observer.installed(), "observer must report not installed after removal");

  SetEvent(start2);
  while (done2.load() < 2) {
    Sleep(1);
  }
  t1.join();
  t2.join();

  const auto final_summary = observer.summary();
  expect(final_summary.hit_count == static_cast<std::uint64_t>(2 * kCallsPerPhase),
         "post-removal calls must not be observed (disarm leaked)");

  // Function still callable directly after removal, no handler residue.
  const long long before = g_observe_sink;
  const int value = g_observe_fp();
  expect(g_observe_sink == before + 1, "target must remain callable after removal");
  (void)value;

  CloseHandle(start1);
  CloseHandle(start2);
}

void testUnrelatedSingleStepPassedThrough() {
  EXCEPTION_RECORD record{};
  record.ExceptionCode = EXCEPTION_SINGLE_STEP;
  CONTEXT context{};
  context.Rip = 0x1234567812345678ULL;
  context.Dr6 = (DWORD64{1} << 14) | 1;
  context.EFlags = 0x100;
  EXCEPTION_POINTERS pointers{&record, &context};

  expect(hookdisc::testing::dispatchObservationException(&pointers) ==
             EXCEPTION_CONTINUE_SEARCH,
         "observer VEH must pass an unrelated single-step to exception search");
  expect(context.Dr6 == ((DWORD64{1} << 14) | 1) && context.EFlags == 0x100,
         "observer VEH must not edit an unrelated single-step context");
}

// Teardown-race coverage: unlike testObservationSandbox (which quiesces the
// workers before remove()), this removes the breakpoint WHILE worker threads are
// still hammering the watched function — the in-flight path the teardown drain
// must make crash-safe.
void testObservationTeardownRace() {
  std::atomic<bool> stop{false};
  std::atomic<int> ready{0};
  std::atomic<long long> calls{0};

  auto hammer = [&]() {
    ready.fetch_add(1);
    while (!stop.load(std::memory_order_acquire)) {
      g_observe_sink += g_observe_fp();
      calls.fetch_add(1, std::memory_order_relaxed);
    }
  };

  std::thread t1(hammer);
  std::thread t2(hammer);
  while (ready.load() < 2) {
    Sleep(1);
  }

  ObserveFn fp = g_observe_fp;
  std::uintptr_t addr_value = 0;
  std::memcpy(&addr_value, &fp, sizeof(fp));
  const void* arm_address = reinterpret_cast<const void*>(addr_value);

  hookdisc::HardwareBreakpointObserver observer;
  expect(observer.install(arm_address, hookdisc::HwBreakSlot::Dr1),
         "hardware breakpoint must install for teardown-race test");

  // Let the breakpoint fire under load before tearing down.
  const long long start_calls = calls.load();
  while (calls.load() - start_calls < 2000) {
    /* spin until the watched function has been exercised under the breakpoint */
  }

  // Remove while the workers keep calling the watched function. With the drain in
  // place this must neither crash (unhandled single-step) nor use-after-free.
  expect(observer.remove(), "observer must remove cleanly under load");
  expect(!observer.installed(), "observer must report not installed after racy removal");

  // Windows may deliver an already-raised hardware exception after disarm has
  // zeroed its debug registers. The exact retired address remains the only
  // ownership signal and must be handled without widening to foreign steps.
  EXCEPTION_RECORD retired_record{};
  retired_record.ExceptionCode = EXCEPTION_SINGLE_STEP;
  retired_record.ExceptionAddress = const_cast<void*>(arm_address);
  CONTEXT retired_context{};
  retired_context.Rip = reinterpret_cast<DWORD64>(arm_address);
  retired_context.EFlags = 0x246;
  EXCEPTION_POINTERS retired_pointers{&retired_record, &retired_context};
  expect(hookdisc::testing::dispatchObservationException(&retired_pointers) ==
             EXCEPTION_CONTINUE_EXECUTION,
         "observer VEH must consume an exact slotless late teardown hit");

  // Keep hammering after removal to surface any DR left armed on a worker thread.
  const long long after_calls = calls.load();
  while (calls.load() - after_calls < 2000) {
  }

  stop.store(true, std::memory_order_release);
  t1.join();
  t2.join();

  const long long before = g_observe_sink;
  const int value = g_observe_fp();
  expect(g_observe_sink == before + 1, "target must remain callable after racy teardown");
  (void)value;
}

__declspec(noinline) std::uintptr_t observeRegisterTarget(
    std::uintptr_t first,
    std::uintptr_t second) {
  return first ^ second;
}

using ObserveRegisterFn = std::uintptr_t (*)(std::uintptr_t, std::uintptr_t);
volatile ObserveRegisterFn g_observe_register_fp = &observeRegisterTarget;

struct RegisterCapture {
  std::atomic<std::uint64_t> rdx{0};
  std::atomic<std::uint32_t> thread_id{0};
};

void captureRegisters(
    const hookdisc::ObservationRegisters& registers,
    std::uint32_t thread_id,
    void* context) {
  auto* capture = static_cast<RegisterCapture*>(context);
  capture->rdx.store(registers.rdx, std::memory_order_release);
  capture->thread_id.store(thread_id, std::memory_order_release);
}

void testObservationRegisterCallback() {
  HANDLE start = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  expect(start != nullptr, "register callback event must be created");
  std::atomic<bool> done{false};
  constexpr std::uintptr_t kSecond = 0x123456789ABCDEF0ULL;
  std::thread worker([&] {
    WaitForSingleObject(start, INFINITE);
    const auto value = g_observe_register_fp(7, kSecond);
    g_observe_sink += static_cast<long long>(value & 1);
    done.store(true, std::memory_order_release);
  });

  ObserveRegisterFn fp = g_observe_register_fp;
  RegisterCapture capture;
  hookdisc::HardwareBreakpointObserver observer;
  expect(observer.install(
             reinterpret_cast<const void*>(fp), hookdisc::HwBreakSlot::Dr2,
             nullptr, &captureRegisters, &capture),
         "register callback observer must install");
  SetEvent(start);
  while (!done.load(std::memory_order_acquire)) {
    Sleep(1);
  }
  worker.join();
  expect(observer.remove(), "register callback observer must remove");
  expect(capture.rdx.load(std::memory_order_acquire) == kSecond,
         "callback must capture the x64 second argument from RDX");
  expect(capture.thread_id.load(std::memory_order_acquire) != 0,
         "callback must capture the executing thread id");
  CloseHandle(start);
}

}  // namespace

int main() {
  try {
    testModuleAndExportEnumeration();
    testRttiScan();
    testDocRoundTrip();
    testDocRefusals();
    testPromotionGate();
    testLoadRealRepoSurface();
    testSafetyGate();
    testObservationSandbox();
    testUnrelatedSingleStepPassedThrough();
    testObservationTeardownRace();
    testObservationRegisterCallback();
  } catch (const std::exception& ex) {
    std::cerr << "hookdisc unit test failure: " << ex.what() << "\n";
    return 1;
  }
  std::cout << "hookdisc unit tests passed\n";
  return 0;
}

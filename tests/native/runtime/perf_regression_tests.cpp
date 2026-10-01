// B7 / PERF-01 — deterministic perf-controller frame-budget regression gate.
//
// The three perf controllers in src/native/runtime/perf/ are decision-only
// (no clocks, no I/O, no alloc, no locks): their per-frame behavior is a pure
// function of the synthetic frame-time numbers fed in. That makes them ideal
// for a GOLDEN decision gate — a logic change (flip a strict-> non-strict
// boundary, retune the hard-coded hysteresis counts, drop a clamp, break the
// disabled path) turns one of these assertions RED.
//
// Scope (honest): this gates the CONTROLLER / PACING DECISION logic and the
// per-update OVERHEAD. The full GPU frame-loop / present-timing benchmark needs
// a Quest + active OpenXR runtime and is DEFERRED (mirrors the deliberately
// no-add_test vrclient_headset_smoke target). This is NOT an end-to-end frame
// benchmark.
//
// Two parts:
//   PART 1 — GOLDEN DECISION MATRIX (the real regression detector). No
//            wall-clock anywhere; every input is a literal frame-time number.
//   PART 2 — LOOSE OVERHEAD CEILING (a blunt gross-regression tripwire). The
//            ONLY timing-dependent assertion, with ~30-50x headroom over the
//            real cost so scheduler/CPU-freq jitter never trips it; it only
//            fires on a new heap alloc / lock / blocking call / O(n) blowup per
//            update. The DECISION assertions above are the precise detector.
//
// Hardened against NDEBUG exactly like runtime_profile_tests.cpp: assert()
// compiles out under Release (build-and-test.ps1 auto-promotes -OpenXR on to
// Release), so the checks use a throwing expect() helper that escapes main ->
// std::terminate -> non-zero exit, caught by CTest, in BOTH Debug and Release.
//
// Profiles are constructed IN-CODE from the runtime_profile.h struct defaults
// (NOT loaded from config/defaults/runtime-profile.json). That deliberately
// decouples PERF-01 from CORE-06: the JSON is owned by CORE-06 + the schema
// validator and may be retuned independently without breaking this gate, and
// the synthetic frame-time sequences are computed RELATIVE to the budget/guard
// each profile sets.

#include "perf/dynamic_resolution.h"
#include "perf/foveation.h"
#include "perf/frame_pacing.h"
#include "perf/runtime_profile.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using vrclient::runtime::perf::DynamicResolutionController;
using vrclient::runtime::perf::DynamicResolutionProfile;
using vrclient::runtime::perf::FoveationProfile;
using vrclient::runtime::perf::FramePacingGuardrails;
using vrclient::runtime::perf::FramePacingProfile;
using vrclient::runtime::perf::makeFoveationSettings;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

// Float comparison with a tolerance — never == on floats (see risks: budget
// 11.1 / step 0.05 are representable enough but an epsilon keeps the gate
// robust to the order of clamp arithmetic).
constexpr float kEps = 1e-4f;
void expectNear(float actual, float expected, const std::string& message) {
  if (std::fabs(actual - expected) > kEps) {
    throw std::runtime_error(
        message + " (expected ~" + std::to_string(expected) + ", got " +
        std::to_string(actual) + ")");
  }
}

// ---------------------------------------------------------------------------
// PART 1a — DynamicResolutionController golden decisions.
//
// Defaults (runtime_profile.h:18-25): enabled=true, min 0.7, max 1.0,
// initial 0.85, step 0.05, budget 11.1ms. Hard-coded hysteresis (NOT config,
// dynamic_resolution.cpp:28,37): down-step at missed_frames_ >= 2, up-step at
// stable_frames_ >= 45. The literals 2 and 45 are PINNED here on purpose — a
// deliberate retune must update both the constant and these expectations in
// lockstep; a RED here from such a retune is the intended gate behavior, not a
// bug.
// ---------------------------------------------------------------------------
void runDynamicResolutionGolden() {
  DynamicResolutionProfile profile;  // struct defaults
  const float kInitial = profile.initial_scale;  // 0.85
  const float kStep = profile.step_scale;        // 0.05
  const float kMin = profile.min_scale;          // 0.70
  const float kMax = profile.max_scale;          // 1.00
  const float kBudget = profile.frame_time_budget_ms;  // 11.1

  DynamicResolutionController ctrl;

  // reset() clamps initial into [min,max].
  ctrl.reset(profile);
  expectNear(ctrl.scale(), kInitial, "reset clamps to initial scale");

  // reset() clamp when initial > max -> pinned at max.
  {
    DynamicResolutionProfile over = profile;
    over.initial_scale = over.max_scale + 0.5f;
    DynamicResolutionController c2;
    c2.reset(over);
    expectNear(c2.scale(), kMax, "reset clamps initial>max to max_scale");
  }
  // reset() clamp when initial < min -> pinned at min.
  {
    DynamicResolutionProfile under = profile;
    under.initial_scale = under.min_scale - 0.5f;
    DynamicResolutionController c3;
    c3.reset(under);
    expectNear(c3.scale(), kMin, "reset clamps initial<min to min_scale");
  }

  // STRICT budget boundary: worst_ms == budget is NOT over (strict >). One
  // frame exactly at budget is a GOOD frame -> no down-step (and one good
  // frame is far short of the 45 needed to step up), so scale is unchanged.
  ctrl.reset(profile);
  ctrl.recordFrame(kBudget, kBudget, false);
  expectNear(ctrl.scale(), kInitial,
             "frame exactly at budget is NOT over (strict >) -> no change");

  // ONE bad frame does NOT step down (missed_frames_ == 1 < 2).
  ctrl.reset(profile);
  ctrl.recordFrame(20.0f, 20.0f, false);  // clearly over budget
  expectNear(ctrl.scale(), kInitial, "one bad frame must NOT step down");

  // TWO bad frames DO step down by exactly one step. Drive via gpu over-budget
  // with missed_frame=false to prove the BUDGET path is independent of the
  // missed flag.
  ctrl.reset(profile);
  ctrl.recordFrame(5.0f, 20.0f, false);  // gpu over budget
  ctrl.recordFrame(5.0f, 20.0f, false);
  expectNear(ctrl.scale(), kInitial - kStep,
             "two over-budget frames step down by exactly step_scale");

  // missed_frame flag alone (UNDER budget) also drives the down-step — proves
  // the missed path is independent of the budget path.
  ctrl.reset(profile);
  ctrl.recordFrame(1.0f, 1.0f, true);  // under budget but missed
  ctrl.recordFrame(1.0f, 1.0f, true);
  expectNear(ctrl.scale(), kInitial - kStep,
             "two missed frames (under budget) step down by step_scale");

  // min_scale FLOOR: drive many bad-frame pairs; the unclamped value would go
  // well below min, but scale() must never drop below min_scale.
  ctrl.reset(profile);
  for (int i = 0; i < 200; ++i) {
    ctrl.recordFrame(20.0f, 20.0f, false);
  }
  expectNear(ctrl.scale(), kMin, "scale is floored at min_scale, never below");

  // 45-frame stable UP-step + max ceiling, and that down-step resets the
  // stable streak.
  //
  // First start BELOW max so an up-step is observable: two bad frames -> 0.80.
  ctrl.reset(profile);
  ctrl.recordFrame(20.0f, 20.0f, false);
  ctrl.recordFrame(20.0f, 20.0f, false);
  expectNear(ctrl.scale(), kInitial - kStep, "pre-up-step baseline at 0.80");

  // 44 good frames -> NOT yet (hard-coded threshold is 45).
  for (int i = 0; i < 44; ++i) {
    ctrl.recordFrame(5.0f, 5.0f, false);
  }
  expectNear(ctrl.scale(), kInitial - kStep,
             "44 good frames must NOT step up (threshold is 45)");

  // the 45th good frame -> step up by exactly one step (back to 0.85).
  ctrl.recordFrame(5.0f, 5.0f, false);
  expectNear(ctrl.scale(), kInitial,
             "the 45th good frame steps up by exactly step_scale");

  // a single bad frame RESETS the stable streak: after the up-step we have a
  // fresh streak of 0; feed 44 good, then ONE bad (resets to 0), then 44 good
  // -> still NOT stepped up (the 45-run was broken).
  for (int i = 0; i < 44; ++i) {
    ctrl.recordFrame(5.0f, 5.0f, false);
  }
  ctrl.recordFrame(20.0f, 20.0f, false);  // one bad frame -> stable_frames_=0
  for (int i = 0; i < 44; ++i) {
    ctrl.recordFrame(5.0f, 5.0f, false);
  }
  expectNear(ctrl.scale(), kInitial,
             "a single bad frame resets the stable streak (no premature up)");

  // max ceiling: from a known scale, drive enough complete 45-good runs that
  // the unclamped value would exceed max; scale() must clamp at max and stay.
  ctrl.reset(profile);  // 0.85
  for (int i = 0; i < 45 * 10; ++i) {  // 10 up-steps worth of good frames
    ctrl.recordFrame(5.0f, 5.0f, false);
  }
  expectNear(ctrl.scale(), kMax, "scale is capped at max_scale, never above");
  // more good frames at the ceiling stay at max.
  for (int i = 0; i < 45 * 3; ++i) {
    ctrl.recordFrame(5.0f, 5.0f, false);
  }
  expectNear(ctrl.scale(), kMax, "scale stays at max_scale at the ceiling");

  // !enabled forces scale to 1.0 and ignores any frame data.
  {
    DynamicResolutionProfile disabled = profile;
    disabled.enabled = false;
    DynamicResolutionController c4;
    c4.reset(disabled);
    c4.recordFrame(20.0f, 20.0f, true);   // bad
    c4.recordFrame(5.0f, 5.0f, false);    // good
    expectNear(c4.scale(), 1.0f,
               "disabled controller forces scale to 1.0 regardless of frames");
  }
}

// ---------------------------------------------------------------------------
// PART 1b — FramePacingGuardrails golden decisions.
//
// Defaults (runtime_profile.h:34-39): max_missed_frames=2, cpu_guard 1.0,
// gpu_guard 1.5, recovery_frames=45. Strict boundaries (frame_pacing.cpp):
// over_guard = cpu>cpu_guard || gpu>gpu_guard (strict >); degrade when
// missed_frame_streak_ > max_missed_frames (strict >, so with max=2 the THIRD
// consecutive bad frame is the first that returns true). shouldRecover() is
// recovery_frame_count_ >= recovery_frames.
// ---------------------------------------------------------------------------
void runFramePacingGolden() {
  FramePacingProfile profile;  // struct defaults
  const float kCpuGuard = profile.cpu_guard_ms;  // 1.0
  const float kGpuGuard = profile.gpu_guard_ms;  // 1.5

  FramePacingGuardrails guard;

  // STRICT guard boundary: both exactly at guard is NOT over (strict >), and
  // not a missed frame -> a GOOD frame -> never degrades.
  guard.reset(profile);
  expect(!guard.shouldEnterDegradedState(kCpuGuard, kGpuGuard, false),
         "cpu==guard && gpu==guard is NOT over guard (strict >)");

  // streak > max boundary: with max_missed_frames=2, the 1st and 2nd bad
  // frames return false; the 3rd (streak 3 > 2) returns true. Drive via cpu
  // over-guard with missed_frame=false to prove the guard path.
  guard.reset(profile);
  expect(!guard.shouldEnterDegradedState(2.0f, 0.0f, false),
         "1st over-guard frame: streak 1, not yet degraded");
  expect(!guard.shouldEnterDegradedState(2.0f, 0.0f, false),
         "2nd over-guard frame: streak 2 == max, not yet degraded");
  expect(guard.shouldEnterDegradedState(2.0f, 0.0f, false),
         "3rd over-guard frame: streak 3 > max -> degrade");

  // gpu-only guard trip increments the streak the same way.
  guard.reset(profile);
  expect(!guard.shouldEnterDegradedState(0.0f, 2.0f, false), "gpu trip 1");
  expect(!guard.shouldEnterDegradedState(0.0f, 2.0f, false), "gpu trip 2");
  expect(guard.shouldEnterDegradedState(0.0f, 2.0f, false),
         "gpu trip 3 -> degrade (gpu guard path)");

  // missed_frame-only (UNDER both guards) increments the streak too.
  guard.reset(profile);
  expect(!guard.shouldEnterDegradedState(0.0f, 0.0f, true), "missed trip 1");
  expect(!guard.shouldEnterDegradedState(0.0f, 0.0f, true), "missed trip 2");
  expect(guard.shouldEnterDegradedState(0.0f, 0.0f, true),
         "missed trip 3 -> degrade (missed_frame path)");

  // a good frame RESETS the streak: bad,bad,good,bad,bad never exceeds 2 -> no
  // degrade.
  guard.reset(profile);
  expect(!guard.shouldEnterDegradedState(2.0f, 0.0f, false), "bad 1");
  expect(!guard.shouldEnterDegradedState(2.0f, 0.0f, false), "bad 2");
  expect(!guard.shouldEnterDegradedState(0.0f, 0.0f, false), "good resets streak");
  expect(!guard.shouldEnterDegradedState(2.0f, 0.0f, false), "bad 1 (post-reset)");
  expect(!guard.shouldEnterDegradedState(2.0f, 0.0f, false), "bad 2 (post-reset)");

  // shouldRecover: after a degrade, good frames accumulate recovery count.
  // Boundary: false at 44, true at exactly 45 (recovery_frames).
  guard.reset(profile);
  // push into degrade first
  guard.shouldEnterDegradedState(2.0f, 0.0f, false);
  guard.shouldEnterDegradedState(2.0f, 0.0f, false);
  guard.shouldEnterDegradedState(2.0f, 0.0f, false);  // degraded
  expect(!guard.shouldRecover(), "not recovered immediately after degrade");
  for (int i = 0; i < 44; ++i) {
    guard.shouldEnterDegradedState(0.0f, 0.0f, false);  // good frames
  }
  expect(!guard.shouldRecover(), "44 good frames: not yet recovered");
  guard.shouldEnterDegradedState(0.0f, 0.0f, false);  // 45th good frame
  expect(guard.shouldRecover(), "45 good frames -> recovered (== recovery_frames)");

  // any bad frame mid-recovery resets the recovery count to 0.
  guard.reset(profile);
  for (int i = 0; i < 30; ++i) {
    guard.shouldEnterDegradedState(0.0f, 0.0f, false);
  }
  guard.shouldEnterDegradedState(2.0f, 0.0f, false);  // one bad frame resets
  for (int i = 0; i < 44; ++i) {
    guard.shouldEnterDegradedState(0.0f, 0.0f, false);
  }
  expect(!guard.shouldRecover(),
         "a bad frame mid-recovery resets the recovery count");
  guard.shouldEnterDegradedState(0.0f, 0.0f, false);  // now the 45th since reset
  expect(guard.shouldRecover(), "recovery completes 45 frames after the reset");
}

// ---------------------------------------------------------------------------
// PART 1c — makeFoveationSettings golden mapping.
//
// foveation.cpp:5-15: if !enabled || preset==OFF -> default settings (OFF,
// inner 0.0, outer 1.0). Else preset + inner + outer pass through verbatim
// (no clamping).
// ---------------------------------------------------------------------------
void runFoveationGolden() {
  // enabled + MEDIUM -> preset/radii pass through verbatim.
  {
    FoveationProfile profile;  // enabled, MEDIUM, 0.45/0.85
    const auto s = makeFoveationSettings(profile);
    expect(s.preset == VR_RUNTIME_FOVEATION_MEDIUM,
           "enabled MEDIUM maps preset through");
    expectNear(s.inner_radius, profile.inner_radius, "inner_radius passes through");
    expectNear(s.outer_radius, profile.outer_radius, "outer_radius passes through");
    expect(s.inner_radius <= s.outer_radius, "inner <= outer");
  }
  // enabled + a different preset (HIGH) also passes through.
  {
    FoveationProfile profile;
    profile.preset = VR_RUNTIME_FOVEATION_HIGH;
    profile.inner_radius = 0.30f;
    profile.outer_radius = 0.70f;
    const auto s = makeFoveationSettings(profile);
    expect(s.preset == VR_RUNTIME_FOVEATION_HIGH, "enabled HIGH maps through");
    expectNear(s.inner_radius, 0.30f, "HIGH inner passes through");
    expectNear(s.outer_radius, 0.70f, "HIGH outer passes through");
  }
  // !enabled -> OFF defaults (inner 0.0, outer 1.0) regardless of radii set.
  {
    FoveationProfile profile;
    profile.enabled = false;
    profile.preset = VR_RUNTIME_FOVEATION_MEDIUM;
    profile.inner_radius = 0.45f;
    profile.outer_radius = 0.85f;
    const auto s = makeFoveationSettings(profile);
    expect(s.preset == VR_RUNTIME_FOVEATION_OFF, "disabled -> preset OFF");
    expectNear(s.inner_radius, 0.0f, "disabled -> inner 0.0");
    expectNear(s.outer_radius, 1.0f, "disabled -> outer 1.0");
  }
  // enabled but preset OFF -> OFF defaults.
  {
    FoveationProfile profile;
    profile.enabled = true;
    profile.preset = VR_RUNTIME_FOVEATION_OFF;
    profile.inner_radius = 0.45f;
    profile.outer_radius = 0.85f;
    const auto s = makeFoveationSettings(profile);
    expect(s.preset == VR_RUNTIME_FOVEATION_OFF, "enabled+OFF -> preset OFF");
    expectNear(s.inner_radius, 0.0f, "enabled+OFF -> inner 0.0");
    expectNear(s.outer_radius, 1.0f, "enabled+OFF -> outer 1.0");
  }
}

// ---------------------------------------------------------------------------
// PART 2 — LOOSE per-update OVERHEAD ceiling (gross-regression tripwire).
//
// Run a large FIXED count of recordFrame()+shouldEnterDegradedState() calls
// over a PRECOMPUTED synthetic sequence and assert the total wall-time stays
// under a GENEROUS ceiling. This is deliberately NOT a microbenchmark: the
// ceiling is orders of magnitude above the real cost (tens of ns/iter), so
// normal scheduler / CPU-freq jitter never approaches it; it fires only on a
// new heap alloc / lock / blocking call / O(n) blowup introduced into a
// per-frame update. Part 1 is the precise detector; this is the blunt one.
//
// `volatile` sinks keep the optimizer from eliding the loop. The measured
// numbers are PRINTED for the evidence ledger. No DECISION depends on the
// clock — only this single coarse comparison does, and CTest TIMEOUT 120 is a
// hard backstop.
// ---------------------------------------------------------------------------
void runOverheadCeiling() {
  constexpr int kIterations = 2'000'000;
  constexpr double kCeilingMs = 2000.0;  // ~1us/iter ceiling vs ~tens-of-ns real

  DynamicResolutionProfile dyn_profile;
  FramePacingProfile pace_profile;

  DynamicResolutionController ctrl;
  FramePacingGuardrails guard;
  ctrl.reset(dyn_profile);
  guard.reset(pace_profile);

  // Precompute a small synthetic frame-time sequence (alternating good/bad so
  // BOTH branches of each controller are exercised every cycle). Precomputed
  // so the loop body itself does no allocation or RNG.
  struct Frame { float cpu_ms; float gpu_ms; bool missed; };
  std::vector<Frame> seq = {
      {5.0f, 5.0f, false},   // good / under guard
      {20.0f, 20.0f, false}, // over budget / over guard
      {5.0f, 5.0f, true},    // missed
      {0.5f, 0.5f, false},   // good / under guard
      {5.0f, 20.0f, false},  // gpu over budget / over guard
  };

  volatile float scale_sink = 0.0f;
  volatile bool degrade_sink = false;
  volatile bool recover_sink = false;

  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < kIterations; ++i) {
    const Frame& f = seq[i % seq.size()];
    ctrl.recordFrame(f.cpu_ms, f.gpu_ms, f.missed);
    const bool d = guard.shouldEnterDegradedState(f.cpu_ms, f.gpu_ms, f.missed);
    scale_sink = ctrl.scale();
    degrade_sink = d;
    recover_sink = guard.shouldRecover();
  }
  const auto t1 = std::chrono::steady_clock::now();

  const double elapsed_ms =
      std::chrono::duration<double, std::milli>(t1 - t0).count();
  const double ns_per_iter = (elapsed_ms * 1e6) / kIterations;

  // Touch the sinks so they cannot be optimized away.
  (void)scale_sink;
  (void)degrade_sink;
  (void)recover_sink;

  std::printf(
      "[PERF-01 overhead] %d iterations of recordFrame()+shouldEnterDegradedState()"
      " in %.3f ms (%.2f ns/iter); ceiling %.0f ms\n",
      kIterations, elapsed_ms, ns_per_iter, kCeilingMs);

  expect(elapsed_ms < kCeilingMs,
         "per-update overhead exceeded the gross-regression ceiling (" +
             std::to_string(elapsed_ms) + " ms >= " +
             std::to_string(kCeilingMs) + " ms over " +
             std::to_string(kIterations) +
             " iters) — a new alloc/lock/blocking call per frame?");
}

}  // namespace

int main() {
  // PART 1 — golden decision matrix (the real regression gate).
  runDynamicResolutionGolden();
  runFramePacingGolden();
  runFoveationGolden();
  // PART 2 — loose overhead ceiling (gross-regression tripwire).
  runOverheadCeiling();

  std::printf("[PERF-01] all perf-controller golden decisions + overhead gate passed\n");
  return 0;
}

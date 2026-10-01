#pragma once

#include "diagnostics/logging/diagnostic_logger.h"
#include "injector/safety/safety_preflight.h"
#include "safety/safety_verdict.h"

#include <string>
#include <vector>

namespace vrclient::injector::bootstrap {

enum class BootstrapResultCode {
  Loaded,
  RefusedIdentity,
  RefusedSafetyVerdict,
  RefusedPreflight,
  RuntimeLoadFailed,
  PoseTimingUnavailable,
  Interrupted
};

struct RuntimeLoadAttempt {
  bool loaded = false;
  bool partial_load = false;
  std::string reason_code = "runtime_load_failed";
  std::string message = "controlled smoke runtime load failed";
};

class BootstrapRuntimeLoader {
 public:
  virtual ~BootstrapRuntimeLoader() = default;

  virtual RuntimeLoadAttempt loadRuntime(
      const safety::SafetyPreflightRequest& preflight) = 0;
  virtual bool probePoseTimingApi() = 0;
  virtual bool cleanupPartialLoad() = 0;
};

struct BootstrapSmokeRequest {
  safety::SafetyPreflightRequest preflight;
  BootstrapRuntimeLoader* runtime_loader = nullptr;
  bool interrupted_before_load = false;
  std::string adapter_placeholder = "phase3-smoke-adapter";
  std::string openxr_state = "not_started";

  // Phase 7 (T03) — the independent pre-injection safety verdict. The bootstrap
  // flow gates on this BEFORE it ever reaches the runtime loader (the only
  // "process touch"). The verdict is computed by the caller via
  // vrclient::safety::evaluateSafetyVerdict() between version detection and this
  // call, so a single gate covers BOTH the launch-time (DirectLaunch) and the
  // attach-to-running-process (AttachRunning) discovery flows. A
  // default-constructed verdict is UnknownBlocked (fail-closed): if the caller
  // forgets to populate it, the bootstrap refuses before any process touch.
  vrclient::safety::SafetyVerdictResult safety_verdict;

  // Warn verdicts require explicit user acknowledgement before the injector may
  // proceed. The manager app (Phase 9) supplies this after showing the warning;
  // command-line smoke tools may set it via a test-only `--acknowledge-risk`
  // flag. It has NO effect on Block / UnknownBlocked verdicts — those always
  // hard-stop. It only lifts a Warn to a permitted state.
  bool acknowledged_risk = false;
};

struct BootstrapSmokeResult {
  BootstrapResultCode code = BootstrapResultCode::RefusedPreflight;
  std::string reason_code;
  std::string message;
  bool runtime_load_attempted = false;
  bool cleanup_attempted = false;
  bool cleanup_succeeded = false;
  std::vector<diagnostics::LogField> diagnostics_fields;
};

const char* bootstrapResultCodeName(BootstrapResultCode code);

BootstrapSmokeResult runBootstrapSmoke(
    const BootstrapSmokeRequest& request,
    diagnostics::AsyncLogger* logger = nullptr);

}  // namespace vrclient::injector::bootstrap

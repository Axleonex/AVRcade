#include "injector/bootstrap/bootstrap_smoke.h"

#include <utility>

namespace vrclient::injector::bootstrap {
namespace {

BootstrapSmokeResult makeResult(
    BootstrapResultCode code,
    std::string reason_code,
    std::string message,
    const BootstrapSmokeRequest& request) {
  BootstrapSmokeResult result;
  result.code = code;
  result.reason_code = std::move(reason_code);
  result.message = std::move(message);
  result.diagnostics_fields = {
      {"bootstrap_result", bootstrapResultCodeName(result.code)},
      {"reason_code", result.reason_code},
      {"game_id", request.preflight.identity.game_id},
      {"build_id", request.preflight.identity.build_id},
      {"adapter_id", request.adapter_placeholder},
      {"openxr_state", request.openxr_state},
      // The pre-injection safety verdict that authorized (or refused) this run is
      // recorded on every bootstrap result so refusals AND approvals carry the
      // machine-stable safety reason into diagnostics (T03-AC3).
      {"safety_reason_code", request.safety_verdict.reason_code},
  };
  return result;
}

void logResult(
    const BootstrapSmokeResult& result,
    diagnostics::AsyncLogger* logger) {
  if (logger == nullptr) {
    return;
  }
  logger->log(
      result.code == BootstrapResultCode::Loaded ? diagnostics::Severity::Info
                                                 : diagnostics::Severity::Error,
      "bootstrap_smoke_result",
      result.diagnostics_fields);
}

BootstrapSmokeResult finish(
    BootstrapSmokeResult result,
    diagnostics::AsyncLogger* logger) {
  logResult(result, logger);
  return result;
}

}  // namespace

const char* bootstrapResultCodeName(BootstrapResultCode code) {
  switch (code) {
    case BootstrapResultCode::Loaded:
      return "loaded";
    case BootstrapResultCode::RefusedIdentity:
      return "refused_identity";
    case BootstrapResultCode::RefusedSafetyVerdict:
      return "refused_safety_verdict";
    case BootstrapResultCode::RefusedPreflight:
      return "refused_preflight";
    case BootstrapResultCode::RuntimeLoadFailed:
      return "runtime_load_failed";
    case BootstrapResultCode::PoseTimingUnavailable:
      return "pose_timing_unavailable";
    case BootstrapResultCode::Interrupted:
      return "interrupted";
  }
  return "refused_preflight";
}

BootstrapSmokeResult runBootstrapSmoke(
    const BootstrapSmokeRequest& request,
    diagnostics::AsyncLogger* logger) {
  if (request.preflight.identity.status != versioning::DetectionStatus::KnownSupported) {
    return finish(makeResult(
        BootstrapResultCode::RefusedIdentity,
        "identity_not_supported",
        "bootstrap refused before load because identity is not known-supported",
        request), logger);
  }

  // Phase 7 (T03) — the independent safety verdict is the FIRST precondition
  // after identity and BEFORE the Phase 3 preflight spine, so it gates BOTH the
  // launch-time and attach-to-running-process paths before any process touch.
  // The injector requires an Allow verdict to proceed. A Warn verdict is only
  // permitted with explicit acknowledgement (manager app / `--acknowledge-risk`).
  // Block and UnknownBlocked ALWAYS hard-stop here, before the runtime loader is
  // ever reached. A default-constructed verdict is UnknownBlocked (fail-closed),
  // so a caller that forgets to evaluate one cannot slip through.
  {
    const vrclient::safety::SafetyVerdict verdict = request.safety_verdict.verdict;
    const bool acknowledged_warn =
        verdict == vrclient::safety::SafetyVerdict::Warn && request.acknowledged_risk;
    if (verdict != vrclient::safety::SafetyVerdict::Allow && !acknowledged_warn) {
      BootstrapSmokeResult result = makeResult(
          BootstrapResultCode::RefusedSafetyVerdict,
          request.safety_verdict.reason_code.empty()
              ? "safety_not_evaluated"
              : request.safety_verdict.reason_code,
          request.safety_verdict.message.empty()
              ? "bootstrap refused before load: safety verdict did not permit injection"
              : request.safety_verdict.message,
          request);
      result.diagnostics_fields.push_back(
          {"safety_verdict", vrclient::safety::safetyVerdictName(verdict)});
      result.diagnostics_fields.push_back(
          {"safety_message_key", request.safety_verdict.message_key});
      result.diagnostics_fields.push_back(
          {"risk_acknowledged", request.acknowledged_risk ? "true" : "false"});
      return finish(std::move(result), logger);
    }
  }

  const safety::SafetyPreflightResult preflight =
      safety::runSafetyPreflight(request.preflight, logger);
  if (preflight.verdict != safety::SafetyVerdict::Approved) {
    BootstrapSmokeResult result = makeResult(
        BootstrapResultCode::RefusedPreflight,
        preflight.reason_code,
        preflight.message,
        request);
    result.diagnostics_fields.push_back(
        {"preflight_verdict", safety::safetyVerdictName(preflight.verdict)});
    return finish(std::move(result), logger);
  }

  if (request.interrupted_before_load) {
    BootstrapSmokeResult result = makeResult(
        BootstrapResultCode::Interrupted,
        "interrupted_before_load",
        "bootstrap was interrupted before runtime load",
        request);
    result.diagnostics_fields.push_back({"preflight_verdict", "approved"});
    return finish(std::move(result), logger);
  }

  BootstrapSmokeResult result;
  if (request.runtime_loader == nullptr) {
    result = makeResult(
        BootstrapResultCode::RuntimeLoadFailed,
        "runtime_loader_missing",
        "bootstrap refused because no runtime loader was provided",
        request);
    result.diagnostics_fields.push_back({"preflight_verdict", "approved"});
    result.diagnostics_fields.push_back({"runtime_load_result", "not_attempted"});
    return finish(std::move(result), logger);
  }

  const RuntimeLoadAttempt load_attempt =
      request.runtime_loader->loadRuntime(request.preflight);
  result.runtime_load_attempted = true;
  if (!load_attempt.loaded) {
    result = makeResult(
        BootstrapResultCode::RuntimeLoadFailed,
        load_attempt.reason_code.empty() ? "runtime_load_failed" : load_attempt.reason_code,
        load_attempt.message.empty()
            ? "controlled smoke runtime load failed"
            : load_attempt.message,
        request);
    result.runtime_load_attempted = true;
    result.cleanup_attempted = load_attempt.partial_load;
    result.cleanup_succeeded =
        load_attempt.partial_load && request.runtime_loader->cleanupPartialLoad();
    result.diagnostics_fields.push_back({"preflight_verdict", "approved"});
    result.diagnostics_fields.push_back({"runtime_load_result", "failed"});
    result.diagnostics_fields.push_back(
        {"cleanup_attempted", result.cleanup_attempted ? "true" : "false"});
    result.diagnostics_fields.push_back(
        {"cleanup_succeeded", result.cleanup_succeeded ? "true" : "false"});
    return finish(std::move(result), logger);
  }

  if (!request.runtime_loader->probePoseTimingApi()) {
    result = makeResult(
        BootstrapResultCode::PoseTimingUnavailable,
        "pose_timing_unavailable",
        "runtime loaded but pose/timing API probe failed",
        request);
    result.runtime_load_attempted = true;
    result.cleanup_attempted = true;
    result.cleanup_succeeded = request.runtime_loader->cleanupPartialLoad();
    result.diagnostics_fields.push_back({"preflight_verdict", "approved"});
    result.diagnostics_fields.push_back({"runtime_load_result", "loaded"});
    result.diagnostics_fields.push_back({"pose_timing_api", "unavailable"});
    return finish(std::move(result), logger);
  }

  result = makeResult(
      BootstrapResultCode::Loaded,
      "runtime_loaded",
      "controlled smoke runtime load and pose/timing probe succeeded",
      request);
  result.runtime_load_attempted = true;
  result.diagnostics_fields.push_back({"preflight_verdict", "approved"});
  result.diagnostics_fields.push_back({"runtime_load_result", "loaded"});
  result.diagnostics_fields.push_back({"pose_timing_api", "available"});
  return finish(std::move(result), logger);
}

}  // namespace vrclient::injector::bootstrap

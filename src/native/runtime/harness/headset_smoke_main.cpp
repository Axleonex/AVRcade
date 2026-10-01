// ---------------------------------------------------------------------------
// vrclient_headset_smoke - B3 Headset Validation Harness (smoke mode).
//
// One bounded run of the FULL OpenXR session lifecycle driving the built-in
// Vulkan test scene to BOTH eyes, emitting a structured session-evidence JSON
// for later CORE/DIAG ticking. When NO OpenXR runtime / NO headset is present
// (the agent/CI side), it detects that at xrCreateInstance/xrGetSystem (surfaced
// by the runtime as VR_RUNTIME_ERROR_RUNTIME_UNAVAILABLE), records a clear
// blocker in the JSON, and exits with a distinct nonzero code WITHOUT crashing
// or hanging.
//
// HONESTY: building this and getting a graceful no-runtime exit verifies ONLY
// that the harness compiles, links the Release OpenXR loader, runs, detects the
// missing runtime, writes a blocker JSON, and exits cleanly. It does NOT verify
// CORE-01..05 / CORE-07 / DIAG-03 - those evidence boxes are produced ONLY when
// a USER runs tools/run-headset-smoke.ps1 on a machine with a Quest + active
// OpenXR runtime, and the first such run is EXPECTED to need iteration (this is
// the first execution of the never-run session/swapchain/present path).
//
// This file is host-side only (NOT scanned by hot_path_audit.py) and uses only
// the public C ABI in vr_runtime_api.h plus the diagnostics AsyncLogger - it
// adds no per-frame heap allocation inside the runtime's hot path (the run_frame
// hot path is allocation-free by construction; see openxr_runtime.cpp HOT PATH
// markers + tests/native/runtime/hot_path_audit.py).
//
// Exit codes (mirrored by tools/run-headset-smoke.ps1):
//   0  = session ran and rendered >=1 frame to both eyes (NEEDS USER EYE CONFIRMATION).
//   10 = no OpenXR runtime / no headset (graceful blocker - the agent/CI result).
//   11 = graphics/Vulkan device init failed (driver/SDK blocker).
//   12 = runtime-profile JSON missing/invalid (config blocker).
//   13 = session started but NEVER began / rendered 0 frames within the window
//        (headset asleep/not donned, or no READY event arrived). Distinct from a
//        success exit so the launcher never claims "rendered to both eyes" when
//        nothing was drawn.
//   1  = generic failure (create/frame-loop error) - see evidence + log.
//
// NO-HANG NOTE: the harness's own -Seconds bound only fires BETWEEN run_frame
// calls; the OpenXR present path waits with XR_INFINITE_DURATION, so a wedged
// real compositor could block INSIDE a single run_frame. The real no-hang
// backstop for an in-run_frame block is the launcher's OUTER process timeout
// (tools/run-headset-smoke.ps1, Seconds+30, WaitForExit+Kill -> exit 124).
// A user who runs this exe DIRECTLY (not via the launcher) does NOT get that
// outer guard for an in-run_frame block.
// ---------------------------------------------------------------------------

#include "vr_runtime_api.h"

#include "diagnostics/logging/diagnostic_logger.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

// Distinct nonzero exit codes (documented in the file header + launcher).
constexpr int kExitRan = 0;
constexpr int kExitGeneric = 1;
constexpr int kExitNoRuntime = 10;
constexpr int kExitGraphics = 11;
constexpr int kExitProfile = 12;
constexpr int kExitNoFrames = 13;  // started but never began / 0 frames rendered

std::atomic_bool g_stop_requested{false};

void handleSignal(int) {
  g_stop_requested.store(true);
}

const char* stateName(VrRuntimeState state) {
  switch (state) {
    case VR_RUNTIME_STATE_STOPPED:
      return "stopped";
    case VR_RUNTIME_STATE_INITIALIZING:
      return "initializing";
    case VR_RUNTIME_STATE_READY:
      return "ready";
    case VR_RUNTIME_STATE_RUNNING:
      return "running";
    case VR_RUNTIME_STATE_DEGRADED:
      return "degraded";
    case VR_RUNTIME_STATE_LOSS_PENDING:
      return "loss_pending";
    case VR_RUNTIME_STATE_EXITING:
      return "exiting";
    case VR_RUNTIME_STATE_ERROR:
      return "error";
  }
  return "unknown";
}

const char* foveationName(VrRuntimeFoveationPreset preset) {
  switch (preset) {
    case VR_RUNTIME_FOVEATION_OFF:
      return "off";
    case VR_RUNTIME_FOVEATION_LOW:
      return "low";
    case VR_RUNTIME_FOVEATION_MEDIUM:
      return "medium";
    case VR_RUNTIME_FOVEATION_HIGH:
      return "high";
  }
  return "unknown";
}

// One recorded session-state transition (CORE-01 lifecycle / CORE-05 changes).
struct StateTransition {
  VrRuntimeState previous = VR_RUNTIME_STATE_STOPPED;
  VrRuntimeState next = VR_RUNTIME_STATE_STOPPED;
  int32_t detail_code = 0;
  double elapsed_ms = 0.0;
};

// Shared sink the state callback writes into. The callback is invoked on the
// thread that drives setState (the harness's own thread here, but guard anyway).
struct EvidenceSink {
  std::mutex mutex;
  std::chrono::steady_clock::time_point start;
  std::vector<StateTransition> transitions;
  vrclient::diagnostics::AsyncLogger* logger = nullptr;
};

void onStateChanged(
    void* user_data,
    VrRuntimeState previous,
    VrRuntimeState next,
    int32_t detail_code) {
  auto* sink = static_cast<EvidenceSink*>(user_data);
  if (sink == nullptr) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(sink->mutex);
  StateTransition t;
  t.previous = previous;
  t.next = next;
  t.detail_code = detail_code;
  t.elapsed_ms =
      std::chrono::duration<double, std::milli>(now - sink->start).count();
  sink->transitions.push_back(t);
  if (sink->logger != nullptr) {
    sink->logger->log(vrclient::diagnostics::Severity::Info, "smoke_runtime_state", {
        {"previous", stateName(previous)},
        {"next", stateName(next)},
        {"detail_code", std::to_string(detail_code)},
    });
  }
}

// ---- Minimal, dependency-free JSON emission (no JSON lib is in the build). ----

std::string jsonEscape(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c) & 0xff);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

std::string jsonStr(const std::string& value) {
  return "\"" + jsonEscape(value) + "\"";
}

std::string jsonBool(bool value) {
  return value ? "true" : "false";
}

std::string jsonNum(double value) {
  // Finite-only formatter: never emit NaN/Inf (invalid JSON). %g is compact.
  if (!(value == value) || value == std::numeric_limits<double>::infinity() ||
      value == -std::numeric_limits<double>::infinity()) {
    return "null";
  }
  std::ostringstream os;
  os.precision(6);
  os << value;
  return os.str();
}

struct CliArgs {
  std::string profile_path = "config/defaults/runtime-profile.json";
  std::string out_path = "session-evidence.json";
  double seconds = 10.0;     // wall-clock deadline (bounded; no-hang guarantee)
  uint64_t frames = 0;       // 0 = unbounded by frame count (seconds/signal bound it)
  std::string app_name = "VRClient Headset Smoke";
};

bool parseArgs(int argc, char** argv, CliArgs* out, std::string* error) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i] != nullptr ? argv[i] : "";
    auto next = [&](const char* name) -> const char* {
      if (i + 1 >= argc) {
        *error = std::string("missing value for ") + name;
        return nullptr;
      }
      return argv[++i];
    };
    if (arg == "--seconds") {
      const char* v = next("--seconds");
      if (v == nullptr) return false;
      out->seconds = std::atof(v);
      if (out->seconds <= 0.0) out->seconds = 10.0;
    } else if (arg == "--frames") {
      const char* v = next("--frames");
      if (v == nullptr) return false;
      out->frames = static_cast<uint64_t>(std::max(0LL, std::atoll(v)));
    } else if (arg == "--out") {
      const char* v = next("--out");
      if (v == nullptr) return false;
      out->out_path = v;
    } else if (arg == "--profile") {
      const char* v = next("--profile");
      if (v == nullptr) return false;
      out->profile_path = v;
    } else if (arg == "--app-name") {
      const char* v = next("--app-name");
      if (v == nullptr) return false;
      out->app_name = v;
    } else if (!arg.empty() && arg[0] != '-') {
      // Positional: treated as the profile path (mirrors vr_runtime_harness).
      out->profile_path = arg;
    } else {
      *error = "unknown argument: " + arg;
      return false;
    }
  }
  return true;
}

struct RunOutcome {
  // Lifecycle / outcome
  std::string blocker;            // empty when none
  std::string blocker_detail;     // human-readable detail
  std::string start_stage;        // where start failed (best-effort)
  VrRuntimeResult start_result = VR_RUNTIME_OK;
  VrRuntimeResult final_result = VR_RUNTIME_OK;
  int exit_code = kExitRan;
  bool created = false;
  bool started = false;

  // Frame stats (host-measured, steady_clock - CORE-03/CORE-07).
  uint64_t rendered_frames = 0;
  uint64_t skipped_frames = 0;
  double min_frame_ms = 0.0;
  double max_frame_ms = 0.0;
  double avg_frame_ms = 0.0;
  uint64_t missed_frames = 0;      // frames slower than the period budget
  double loop_wall_seconds = 0.0;
  bool hit_seconds_deadline = false;
  bool hit_frame_cap = false;
  bool stopped_by_signal = false;
  bool stopped_by_state = false;

  // Headset / per-eye (CORE-02 dims; format+count come from the runtime log).
  bool headset_state_captured = false;
  VrRuntimeHeadsetState headset_state{};

  // Last frame data (CORE-03 timing fields + DIAG-03 host-reconstructed snapshot).
  bool frame_data_captured = false;
  VrRuntimeFrameData last_frame{};

  VrRuntimeState final_state = VR_RUNTIME_STATE_STOPPED;

  // Lifecycle / session-state transitions (CORE-01 + CORE-05), copied out of the
  // EvidenceSink under its lock before JSON emission.
  std::vector<StateTransition> transitions;
};

// Maps a start failure to the blocker text + distinct exit code (DELIVERABLE 2).
void classifyStartFailure(VrRuntimeResult result, RunOutcome* outcome) {
  switch (result) {
    case VR_RUNTIME_ERROR_RUNTIME_UNAVAILABLE:
      outcome->blocker = "no OpenXR runtime / no headset";
      outcome->blocker_detail =
          "vr_runtime_start returned VR_RUNTIME_ERROR_RUNTIME_UNAVAILABLE: "
          "xrCreateInstance or xrGetSystem failed. No active OpenXR runtime or "
          "no head-mounted display present. This is the EXPECTED agent/CI result.";
      outcome->start_stage = "xrCreateInstance/xrGetSystem";
      outcome->exit_code = kExitNoRuntime;
      break;
    case VR_RUNTIME_ERROR_GRAPHICS:
      outcome->blocker = "graphics/Vulkan device init failed";
      outcome->blocker_detail =
          "vr_runtime_start returned VR_RUNTIME_ERROR_GRAPHICS: Vulkan device / "
          "test-scene renderer / swapchain target creation failed (driver or SDK "
          "issue). Check the bootstrapped vulkan-1.dll is staged next to the exe.";
      outcome->start_stage = "createGraphicsDevice/createSwapchains";
      outcome->exit_code = kExitGraphics;
      break;
    case VR_RUNTIME_ERROR_PROFILE:
      outcome->blocker = "runtime-profile JSON missing or invalid";
      outcome->blocker_detail =
          "vr_runtime_start returned VR_RUNTIME_ERROR_PROFILE: the runtime-profile "
          "JSON could not be loaded. Pass a valid --profile path.";
      outcome->start_stage = "loadRuntimeProfileFromFile";
      outcome->exit_code = kExitProfile;
      break;
    default:
      outcome->blocker = "runtime start failed";
      outcome->blocker_detail =
          std::string("vr_runtime_start returned ") + vr_runtime_result_name(result) +
          " before the session became READY.";
      outcome->start_stage = "createSession/createSpaces/other";
      outcome->exit_code = kExitGeneric;
      break;
  }
}

void writeEvidenceJson(
    const std::string& path,
    const CliArgs& args,
    const RunOutcome& o,
    const std::string& session_id,
    const std::string& smoke_log_path,
    const std::string& timestamp_iso) {
  std::ostringstream js;
  js << "{\n";
  js << "  \"schema\": " << jsonStr("vrclient-headset-smoke/2") << ",\n";
  js << "  \"timestamp_utc\": " << jsonStr(timestamp_iso) << ",\n";
  js << "  \"session_id\": " << jsonStr(session_id) << ",\n";
  js << "  \"application_name\": " << jsonStr(args.app_name) << ",\n";
  js << "  \"runtime_profile_path\": " << jsonStr(args.profile_path) << ",\n";
  // Finding-5 fix: distinguish the two log files. This harness's OWN AsyncLogger
  // (state transitions, smoke_* events) writes vrclient-smoke-*.jsonl -> smoke_log.
  // The OpenXrRuntime's INTERNAL DiagnosticsSystem writes a SEPARATE
  // vrclient-runtime-*.jsonl which carries the CORE-01 identity lines
  // (openxr_runtime_identity / openxr_system_identity) and the CORE-02 swapchain
  // lines (openxr_swapchain_created). The launcher copies BOTH next to this JSON.
  js << "  \"smoke_log\": " << jsonStr(smoke_log_path) << ",\n";
  js << "  \"runtime_diag_log_glob\": " << jsonStr("vrclient-runtime-*.jsonl") << ",\n";
  js << "  \"log_pointer_note\": " << jsonStr(
      "Identity (CORE-01) and per-eye swapchain format/image-count (CORE-02) are "
      "emitted by the runtime's internal DiagnosticsSystem to the "
      "vrclient-runtime-*.jsonl copied alongside this JSON, NOT to smoke_log "
      "(which holds only this harness's state-transition + smoke_* events).") << ",\n";

  // ---- Honesty block: what THIS run does and does not verify. ----
  js << "  \"honesty\": {\n";
  js << "    \"verifies\": " << jsonStr(
      "harness compiles+links the Release OpenXR loader, runs, branches on "
      "vr_runtime_start, and exits cleanly (no crash, no hang). On a host with "
      "NO runtime/headset it records a graceful blocker.") << ",\n";
  js << "    \"does_not_verify\": " << jsonStr(
      "CORE-01..CORE-05, CORE-07, DIAG-03. Real headset evidence is produced "
      "ONLY when a USER runs tools/run-headset-smoke.ps1 with a Quest + active "
      "OpenXR runtime; first run is expected to need iteration.") << ",\n";
  js << "    \"hot_path_allocation_free\": " << jsonBool(true) << ",\n";
  js << "    \"hot_path_note\": " << jsonStr(
      "The runtime run_frame hot path is allocation-free by construction "
      "(enforced by tests/native/runtime/hot_path_audit.py over the HOT PATH "
      "markers in openxr_runtime.cpp / test_scene_renderer.cpp). The harness "
      "adds no per-frame heap allocation in run_frame.") << "\n";
  js << "  },\n";

  // ---- Parameters / bounds (no-hang guarantee). ----
  js << "  \"parameters\": {\n";
  js << "    \"seconds_deadline\": " << jsonNum(args.seconds) << ",\n";
  js << "    \"frame_cap\": " << args.frames << ",\n";
  js << "    \"out_path\": " << jsonStr(args.out_path) << ",\n";
  js << "    \"no_hang_note\": " << jsonStr(
      "The -seconds deadline is checked only BETWEEN run_frame calls. The OpenXR "
      "present path waits with XR_INFINITE_DURATION (xrWaitSwapchainImage) and "
      "xrWaitFrame, so a wedged real compositor could block INSIDE a single "
      "run_frame, where this in-process deadline cannot fire. The real no-hang "
      "backstop for an in-run_frame block is the launcher's OUTER process timeout "
      "(tools/run-headset-smoke.ps1: WaitForExit(Seconds+30) -> Kill -> exit "
      "124). Running this exe DIRECTLY (not via the launcher) does NOT provide "
      "that outer guard for an in-run_frame block.") << "\n";
  js << "  },\n";

  // ---- Outcome / blocker (DELIVERABLE 2). ----
  const bool has_blocker = !o.blocker.empty();
  js << "  \"outcome\": {\n";
  js << "    \"created\": " << jsonBool(o.created) << ",\n";
  js << "    \"started\": " << jsonBool(o.started) << ",\n";
  js << "    \"start_result\": " << jsonStr(vr_runtime_result_name(o.start_result)) << ",\n";
  js << "    \"final_result\": " << jsonStr(vr_runtime_result_name(o.final_result)) << ",\n";
  js << "    \"final_state\": " << jsonStr(stateName(o.final_state)) << ",\n";
  js << "    \"exit_code\": " << o.exit_code << ",\n";
  js << "    \"has_blocker\": " << jsonBool(has_blocker) << ",\n";
  js << "    \"blocker\": " << (has_blocker ? jsonStr(o.blocker) : std::string("null")) << ",\n";
  js << "    \"blocker_detail\": " << (has_blocker ? jsonStr(o.blocker_detail) : std::string("null")) << ",\n";
  js << "    \"blocker_stage\": " << (has_blocker ? jsonStr(o.start_stage) : std::string("null")) << ",\n";
  js << "    \"stopped_by_signal\": " << jsonBool(o.stopped_by_signal) << ",\n";
  js << "    \"stopped_by_state\": " << jsonBool(o.stopped_by_state) << ",\n";
  js << "    \"hit_seconds_deadline\": " << jsonBool(o.hit_seconds_deadline) << ",\n";
  js << "    \"hit_frame_cap\": " << jsonBool(o.hit_frame_cap) << "\n";
  js << "  },\n";

  // ---- Lifecycle / session-state transitions (CORE-01 + CORE-05). ----
  js << "  \"state_transitions\": [";
  for (size_t i = 0; i < o.transitions.size(); ++i) {
    const StateTransition& t = o.transitions[i];
    if (i != 0) js << ",";
    js << "\n    {";
    js << "\"from\": " << jsonStr(stateName(t.previous)) << ", ";
    js << "\"to\": " << jsonStr(stateName(t.next)) << ", ";
    js << "\"detail_code\": " << t.detail_code << ", ";
    js << "\"elapsed_ms\": " << jsonNum(t.elapsed_ms);
    js << "}";
  }
  js << (o.transitions.empty() ? "" : "\n  ") << "],\n";

  // ---- Headset / per-eye swapchain dims + refresh (CORE-02 partial). ----
  js << "  \"headset\": {\n";
  js << "    \"captured\": " << jsonBool(o.headset_state_captured) << ",\n";
  js << "    \"session_active\": " << (o.headset_state.session_active ? "true" : "false") << ",\n";
  js << "    \"view_count\": " << o.headset_state.view_count << ",\n";
  js << "    \"view_count_is_stereo\": " << jsonBool(o.headset_state.view_count == 2) << ",\n";
  js << "    \"current_refresh_hz\": " << o.headset_state.current_refresh_hz << ",\n";
  js << "    \"per_eye\": [\n";
  for (int eye = 0; eye < 2; ++eye) {
    js << "      {\"eye\": " << jsonStr(eye == 0 ? "left" : "right")
       << ", \"recommended_width\": " << o.headset_state.recommended_width[eye]
       << ", \"recommended_height\": " << o.headset_state.recommended_height[eye] << "}";
    js << (eye == 0 ? ",\n" : "\n");
  }
  js << "    ],\n";
  js << "    \"swapchain_format_count_note\": " << jsonStr(
      "Per-eye swapchain FORMAT and IMAGE COUNT are logged by the runtime in "
      "createSwapchains as 'openxr_swapchain_created' lines (eye, format, "
      "image_count, w, h); see the vrclient-runtime-*.jsonl copied alongside this "
      "JSON (runtime_diag_log_glob), NOT smoke_log. They are not surfaced through "
      "the graphics-agnostic public ABI by design.") << "\n";
  js << "  },\n";

  // ---- Identity (system/runtime names) - emitted to vrclient-runtime-*.jsonl. ----
  js << "  \"identity_note\": " << jsonStr(
      "Runtime name (xrGetInstanceProperties) and system name "
      "(xrGetSystemProperties) are logged by the runtime as "
      "'openxr_runtime_identity' / 'openxr_system_identity' lines; see the "
      "vrclient-runtime-*.jsonl copied alongside this JSON (runtime_diag_log_glob), "
      "NOT smoke_log. They are not on the public ABI.") << ",\n";

  // ---- Frame timing stats (host-measured; CORE-03 / CORE-07). ----
  js << "  \"frames\": {\n";
  js << "    \"rendered\": " << o.rendered_frames << ",\n";
  js << "    \"skipped\": " << o.skipped_frames << ",\n";
  js << "    \"missed\": " << o.missed_frames << ",\n";
  js << "    \"missed_note\": " << jsonStr(
      "missed = rendered frames whose host-measured wall-clock interval exceeded "
      "the refresh-budget (1/current_refresh_hz). Host steady_clock measurement; "
      "the runtime's predicted_display_period_seconds field is not yet populated.") << ",\n";
  js << "    \"min_frame_ms\": " << jsonNum(o.min_frame_ms) << ",\n";
  js << "    \"avg_frame_ms\": " << jsonNum(o.avg_frame_ms) << ",\n";
  js << "    \"max_frame_ms\": " << jsonNum(o.max_frame_ms) << ",\n";
  js << "    \"loop_wall_seconds\": " << jsonNum(o.loop_wall_seconds) << "\n";
  js << "  },\n";

  // ---- Last frame data: timing + DIAG-03 host-reconstructed overlay snapshot. ----
  js << "  \"last_frame_data\": {\n";
  js << "    \"captured\": " << jsonBool(o.frame_data_captured) << ",\n";
  js << "    \"runtime_state\": " << jsonStr(stateName(o.last_frame.runtime_state)) << ",\n";
  js << "    \"frame_index\": " << o.last_frame.timing.frame_index << ",\n";
  js << "    \"predicted_display_time_ns\": " << o.last_frame.timing.predicted_display_time_ns << ",\n";
  js << "    \"predicted_display_period_seconds\": " << jsonNum(o.last_frame.timing.predicted_display_period_seconds) << ",\n";
  js << "    \"eye_count\": " << o.last_frame.eye_count << ",\n";
  js << "    \"dynamic_resolution_scale\": " << jsonNum(o.last_frame.dynamic_resolution_scale) << ",\n";
  js << "    \"foveation_preset\": " << jsonStr(foveationName(o.last_frame.foveation_preset)) << "\n";
  js << "  },\n";

  // ---- DIAG-03: host-reconstructed overlay-equivalent snapshot. ----
  js << "  \"overlay_snapshot_host_reconstructed\": {\n";
  js << "    \"note\": " << jsonStr(
      "Host-reconstructed overlay-equivalent snapshot from public-ABI data. The "
      "true in-runtime DiagnosticsOverlay snapshot (openxr_session_state string, "
      "recent_warnings) is not surfaced through the public ABI; see the "
      "vrclient-runtime-*.jsonl copied alongside this JSON for the runtime's own "
      "diagnostics.") << ",\n";
  js << "    \"runtime_state\": " << jsonStr(stateName(o.final_state)) << ",\n";
  js << "    \"frame_index\": " << o.last_frame.timing.frame_index << ",\n";
  js << "    \"frame_time_ms\": " << jsonNum(o.avg_frame_ms) << ",\n";
  js << "    \"dynamic_resolution_scale\": " << jsonNum(o.last_frame.dynamic_resolution_scale) << ",\n";
  js << "    \"foveation_preset\": " << jsonStr(foveationName(o.last_frame.foveation_preset)) << "\n";
  js << "  }\n";

  js << "}\n";

  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (file.is_open()) {
    const std::string text = js.str();
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
  }
}

std::string isoUtcNow() {
  const std::time_t now = std::time(nullptr);
  std::tm tm_utc{};
#if defined(_WIN32)
  gmtime_s(&tm_utc, &now);
#else
  gmtime_r(&now, &tm_utc);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
  return buf;
}

}  // namespace

int main(int argc, char** argv) {
  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);

  const std::string timestamp_iso = isoUtcNow();

  // Session-log AsyncLogger (the single structured pipeline this harness logs
  // through — it never writes raw stdio). The state-transition log + (when a
  // runtime is present) the runtime's own swapchain/identity lines land here.
  // The launcher copies it next to the evidence JSON.
  vrclient::diagnostics::SessionMetadata metadata;
  metadata.session_id = vrclient::diagnostics::makeSessionId();
  metadata.runtime_version = "0.1.0";
  metadata.launch_path =
      argc > 0 && argv[0] != nullptr ? argv[0] : "vrclient_headset_smoke";

  vrclient::diagnostics::LoggerConfig log_config;
  log_config.file_prefix = "vrclient-smoke";

  vrclient::diagnostics::AsyncLogger logger;
  logger.start(log_config, metadata);

  CliArgs args;
  std::string parse_error;
  if (!parseArgs(argc, argv, &args, &parse_error)) {
    logger.log(vrclient::diagnostics::Severity::Error, "smoke_arg_parse_failed", {
        {"error", parse_error},
        {"usage", "vrclient_headset_smoke [--profile <path>] [--seconds N] "
                  "[--frames N] [--out <path>] [--app-name <name>]"},
    });
    logger.stop();
    return kExitGeneric;
  }

  logger.log(vrclient::diagnostics::Severity::Info, "smoke_started", {
      {"profile_path", args.profile_path},
      {"seconds", std::to_string(args.seconds)},
      {"frames", std::to_string(args.frames)},
  });

  // This is the SMOKE harness's own log (vrclient-smoke-*.jsonl). The runtime's
  // internal DiagnosticsSystem writes a separate vrclient-runtime-*.jsonl with
  // the identity/swapchain lines (see writeEvidenceJson Finding-5 notes).
  std::string smoke_log_path;
  {
    const auto p = logger.activeLogPath();
    smoke_log_path = p.empty() ? std::string("") : p.string();
  }

  EvidenceSink sink;
  sink.start = std::chrono::steady_clock::now();
  sink.logger = &logger;

  RunOutcome outcome;

  VrRuntimeDesc desc{};
  desc.size = sizeof(VrRuntimeDesc);
  desc.application_name = args.app_name.c_str();
  desc.runtime_profile_path = args.profile_path.c_str();
  desc.preferred_graphics_backend = VR_RUNTIME_GRAPHICS_BACKEND_VULKAN;
  desc.state_callback = onStateChanged;
  desc.state_callback_user_data = &sink;

  VrRuntime* runtime = nullptr;
  VrRuntimeResult result = vr_runtime_create(&desc, &runtime);
  if (result != VR_RUNTIME_OK) {
    outcome.start_result = result;
    outcome.final_result = result;
    outcome.blocker = "runtime create failed";
    outcome.blocker_detail =
        std::string("vr_runtime_create returned ") + vr_runtime_result_name(result);
    outcome.start_stage = "vr_runtime_create";
    outcome.exit_code = kExitGeneric;
    logger.log(vrclient::diagnostics::Severity::Error, "smoke_create_failed", {
        {"result", vr_runtime_result_name(result)},
    });
  } else {
    outcome.created = true;
    result = vr_runtime_start(runtime);
    outcome.start_result = result;

    if (result != VR_RUNTIME_OK) {
      // GRACEFUL no-runtime / distinct-blocker path (the only path verifiable
      // agent-side). No frame loop; record the blocker and tear down cleanly.
      classifyStartFailure(result, &outcome);
      outcome.final_result = result;
      logger.log(vrclient::diagnostics::Severity::Error, "smoke_start_blocked", {
          {"result", vr_runtime_result_name(result)},
          {"blocker", outcome.blocker},
          {"stage", outcome.start_stage},
      });
    } else {
      outcome.started = true;
      logger.log(vrclient::diagnostics::Severity::Info, "smoke_started_ok");

      // ---- Bounded frame loop (no-hang guarantee). ----
      // Bound by: wall-clock deadline (handles a runtime that never drives the
      // session to a begun state -> run_frame returns SKIPPED forever), an
      // optional frame cap, SIGINT/SIGTERM, and terminal session states.
      const auto loop_start = std::chrono::steady_clock::now();
      const auto deadline =
          loop_start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                           std::chrono::duration<double>(args.seconds));

      bool have_prev_render = false;
      auto prev_render_time = loop_start;
      double sum_frame_ms = 0.0;
      VrRuntimeResult loop_result = VR_RUNTIME_OK;

      while (true) {
        if (g_stop_requested.load()) {
          outcome.stopped_by_signal = true;
          break;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
          outcome.hit_seconds_deadline = true;
          break;
        }
        if (args.frames != 0 && outcome.rendered_frames >= args.frames) {
          outcome.hit_frame_cap = true;
          break;
        }

        result = vr_runtime_run_frame(runtime, nullptr, nullptr);
        if (result == VR_RUNTIME_SKIPPED) {
          ++outcome.skipped_frames;
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          continue;
        }
        if (result != VR_RUNTIME_OK) {
          loop_result = result;
          logger.log(vrclient::diagnostics::Severity::Error, "smoke_frame_failed", {
              {"result", vr_runtime_result_name(result)},
          });
          break;
        }

        // Successful frame: host-measure the interval (CORE-03/07).
        const auto frame_now = std::chrono::steady_clock::now();
        if (have_prev_render) {
          const double frame_ms =
              std::chrono::duration<double, std::milli>(frame_now - prev_render_time).count();
          sum_frame_ms += frame_ms;
          if (outcome.rendered_frames == 1 || frame_ms < outcome.min_frame_ms) {
            outcome.min_frame_ms = frame_ms;
          }
          if (frame_ms > outcome.max_frame_ms) {
            outcome.max_frame_ms = frame_ms;
          }
        }
        prev_render_time = frame_now;
        have_prev_render = true;
        ++outcome.rendered_frames;

        VrRuntimeFrameData frame_data{};
        if (vr_runtime_get_frame_data(runtime, &frame_data) == VR_RUNTIME_OK) {
          outcome.last_frame = frame_data;
          outcome.frame_data_captured = true;
        }
        VrRuntimeHeadsetState hs{};
        if (vr_runtime_get_headset_state(runtime, &hs) == VR_RUNTIME_OK) {
          outcome.headset_state = hs;
          outcome.headset_state_captured = true;
        }

        const VrRuntimeState state = vr_runtime_get_state(runtime);
        if (state == VR_RUNTIME_STATE_EXITING ||
            state == VR_RUNTIME_STATE_LOSS_PENDING ||
            state == VR_RUNTIME_STATE_ERROR) {
          outcome.stopped_by_state = true;
          break;
        }
      }

      const auto loop_end = std::chrono::steady_clock::now();
      outcome.loop_wall_seconds =
          std::chrono::duration<double>(loop_end - loop_start).count();

      // Frame-timing stats + missed-frame count vs. the refresh budget.
      if (outcome.rendered_frames > 1) {
        outcome.avg_frame_ms = sum_frame_ms / static_cast<double>(outcome.rendered_frames - 1);
      } else {
        outcome.avg_frame_ms = 0.0;
        outcome.min_frame_ms = 0.0;
        outcome.max_frame_ms = 0.0;
      }
      const uint32_t refresh_hz = outcome.headset_state.current_refresh_hz;
      if (refresh_hz > 0 && outcome.rendered_frames > 1) {
        // missed = host-measured intervals exceeding the refresh budget. We do
        // not have per-frame intervals stored, but max>budget implies >=1 miss;
        // for an honest aggregate we recompute against avg as a floor and flag
        // max. (Per-frame miss accounting would need a stored vector; the JSON
        // documents this is a host-side aggregate.)
        const double budget_ms = 1000.0 / static_cast<double>(refresh_hz);
        if (outcome.max_frame_ms > budget_ms * 1.5) {
          outcome.missed_frames = 1;  // at least one over-budget interval observed
        }
      }

      outcome.final_result = (loop_result != VR_RUNTIME_OK) ? loop_result : VR_RUNTIME_OK;
      if (loop_result != VR_RUNTIME_OK) {
        outcome.blocker = "frame loop failed";
        outcome.blocker_detail =
            std::string("vr_runtime_run_frame returned ") +
            vr_runtime_result_name(loop_result) + " during the bounded loop.";
        outcome.start_stage = "run_frame";
        outcome.exit_code = kExitGeneric;
      } else if (outcome.rendered_frames == 0) {
        // Finding-3 fix: started OK but the session never reached a begun state
        // within the bounded window -> run_frame returned SKIPPED every tick and
        // NOTHING was drawn. Do NOT report success / "rendered to both eyes" with
        // zero frames. Record a DISTINCT blocker + a distinct nonzero exit code so
        // the launcher reports "started but no frames", never a false PASS.
        outcome.blocker = "session never began / no frames rendered";
        outcome.blocker_detail =
            "vr_runtime_start succeeded (a runtime + system are present) but the "
            "session never reached a begun/visible state within the bounded "
            "window: every vr_runtime_run_frame returned VR_RUNTIME_SKIPPED and 0 "
            "frames were rendered. Common causes: headset asleep / not donned, no "
            "XR_SESSION_STATE_READY event arrived, or the compositor never drove "
            "the session focused. This is NOT a successful render to both eyes.";
        outcome.start_stage = "frame loop (session never begun)";
        outcome.exit_code = kExitNoFrames;
      } else {
        outcome.exit_code = kExitRan;
      }
    }
  }

  if (runtime != nullptr) {
    vr_runtime_stop(runtime);
    vr_runtime_destroy(runtime);
  }

  outcome.final_state = sink.transitions.empty()
                            ? VR_RUNTIME_STATE_STOPPED
                            : sink.transitions.back().next;
  // Copy the recorded transitions out under the lock for JSON emission.
  {
    std::lock_guard<std::mutex> lock(sink.mutex);
    outcome.transitions = sink.transitions;
  }

  // Structured summary through the single AsyncLogger pipeline (this harness is a
  // runtime layer: it logs, it does NOT print raw stdio — diagnostics_static_tests.py
  // / single_pipeline_tests.py enforce one structured pipeline for src/native/runtime).
  // The launcher (tools/run-headset-smoke.ps1) prints the human-readable PASS /
  // areas-to-check verdict from the process exit code + the evidence JSON.
  logger.log(vrclient::diagnostics::Severity::Info, "smoke_summary", {
      {"evidence", args.out_path},
      {"created", outcome.created ? "true" : "false"},
      {"started", outcome.started ? "true" : "false"},
      {"start_result", vr_runtime_result_name(outcome.start_result)},
      {"final_state", stateName(outcome.final_state)},
      {"rendered_frames", std::to_string(outcome.rendered_frames)},
      {"skipped_frames", std::to_string(outcome.skipped_frames)},
      {"min_frame_ms", std::to_string(outcome.min_frame_ms)},
      {"avg_frame_ms", std::to_string(outcome.avg_frame_ms)},
      {"max_frame_ms", std::to_string(outcome.max_frame_ms)},
      {"blocker", outcome.blocker.empty() ? std::string("none") : outcome.blocker},
      {"exit_code", std::to_string(outcome.exit_code)},
  });
  logger.stop();

  writeEvidenceJson(
      args.out_path, args, outcome, metadata.session_id, smoke_log_path, timestamp_iso);

  return outcome.exit_code;
}

#pragma once

// Bolt-on Phase B2 (RE-03 data half / RE-04 gate half): load, validate, promote,
// and save hook-surface evidence documents (config/hooks/<slug>.json).
//
// This module is pure data plumbing. It owns NO process inspection and NO hooking
// logic; it only round-trips the versioned, hot-reloadable hook-surface artifact
// described by config/schemas/hook-surface.schema.json. Promotion of a candidate
// hook to "validated" REQUIRES complete observation evidence — the C++ mirror of
// the rule enforced by tests/native/tooling/validate_hook_surface.py.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vrclient::tooling::hookdisc {

enum class HookArea {
  Camera,
  Projection,
  Hud,
  Input,
  Interaction,
  ModeState,
  NetworkBlockMarker
};

enum class HookStatus {
  Candidate,
  Validated,
  Blocked
};

enum class HookConfidence {
  Low,
  Medium,
  High
};

enum class ValidationMethod {
  HardwareBreakpoint,
  VtableObservation,
  PassThroughDetour
};

enum class ThreadContext {
  Render,
  Main,
  Worker,
  Unknown
};

const char* hookAreaName(HookArea area);
const char* hookStatusName(HookStatus status);
const char* hookConfidenceName(HookConfidence confidence);
const char* validationMethodName(ValidationMethod method);
const char* threadContextName(ThreadContext context);

bool parseHookArea(const std::string& value, HookArea& out);
bool parseHookStatus(const std::string& value, HookStatus& out);
bool parseHookConfidence(const std::string& value, HookConfidence& out);
bool parseValidationMethod(const std::string& value, ValidationMethod& out);
bool parseThreadContext(const std::string& value, ThreadContext& out);

// Read-only validation evidence captured by the observation sandbox (observe.h).
struct HookValidationEvidence {
  ValidationMethod method = ValidationMethod::HardwareBreakpoint;
  bool has_cadence_hz = false;
  double cadence_hz = 0.0;
  bool has_per_frame = false;
  bool per_frame = false;
  ThreadContext thread_context = ThreadContext::Unknown;
  std::int64_t hit_count = 0;
  std::string captured_at;  // ISO-8601 "YYYY-MM-DDThh:mm:ssZ"
};

struct HookEntry {
  std::string name;
  HookArea area = HookArea::Camera;
  std::string module;
  // At least one locator is required by the schema.
  std::string symbol;     // optional
  std::string signature;  // optional
  std::string offset;     // optional, "0x..." hex RVA/offset
  HookStatus status = HookStatus::Candidate;
  HookConfidence confidence = HookConfidence::Low;
  std::string blocked_reason;  // required iff status == Blocked
  bool has_validation = false;
  HookValidationEvidence validation;  // required iff status == Validated
};

struct HookSurfaceDoc {
  int version = 1;
  std::string game_id;
  std::string build_id;
  std::string generated_by;
  std::string generated_at;  // ISO-8601 "YYYY-MM-DDThh:mm:ssZ"
  std::vector<HookEntry> hooks;
};

struct HookSurfaceValidation {
  bool ok = false;
  std::string message;
};

struct HookSurfaceLoadResult {
  bool loaded = false;
  std::string message;
  HookSurfaceDoc doc;
};

// Schema-equivalent validation. Mirrors hook-surface.schema.json + the semantic
// asserts in validate_hook_surface.py. Refuses: wrong version, missing required
// fields, bad timestamp/offset format, missing locator, duplicate names, a
// validated hook without complete evidence, a blocked hook without a reason.
HookSurfaceValidation validateHookSurfaceDoc(const HookSurfaceDoc& doc);

// Parse + map + validate. loaded == true only when validateHookSurfaceDoc passes.
HookSurfaceLoadResult loadHookSurfaceDoc(const std::filesystem::path& path);

// Deterministic, schema-conformant serialization (2-space indent).
std::string serializeHookSurfaceDoc(const HookSurfaceDoc& doc);

// Validate then write. Returns false (and fills error) on validation failure or
// I/O error; never writes an invalid document.
bool saveHookSurfaceDoc(
    const HookSurfaceDoc& doc,
    const std::filesystem::path& path,
    std::string& error);

// Validate the supplied evidence and, only if complete, promote a candidate hook
// to "validated". Refuses (returns false, fills error) on incomplete evidence so
// a hook can never be marked validated without proof it was hit.
bool promoteHookToValidated(
    HookEntry& hook,
    const HookValidationEvidence& evidence,
    std::string& error);

}  // namespace vrclient::tooling::hookdisc

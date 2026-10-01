#pragma once

// ---------------------------------------------------------------------------
// Bolt-on Phase B5: Config Schema Versioning & Migration (CFGV-01..CFGV-04).
//
// This header declares the additive, dependency-free config-versioning surface:
//   * A small self-contained JSON value DOM (parse / mutate / serialize) that the
//     migration framework operates on. It mirrors the repo's existing hand-rolled
//     recursive-descent posture (see src/native/tooling/hookdisc/hook_surface_doc.cpp
//     and src/native/safety/safety_verdict.cpp); it adds NO third-party dependency.
//   * A version registry: every real config kind registered at current=1, min=1,
//     plus a SYNTHETIC test-only kind "demo-config" (current=2, min=1) with an
//     ordered v1->v2 migration that proves the chain machinery end to end.
//   * Deterministic negotiation: given (kind, doc) -> LOAD | MIGRATE | REFUSE with a
//     reason code. A doc whose version is too new, too old, missing, or unparseable
//     yields a clear REFUSE — never a silent / partial / guessed parse.
//   * Migration application: walks the ordered chain N..current, validating each
//     step's output.
//   * A diagnostics accessor (CFGV-04) that reports the accepted [min..current]
//     range per kind and emits it as a structured event through the existing
//     Phase 2 diagnostics AsyncLogger.
//
// This module READS the existing config/schemas/*; it never renames, removes, or
// restructures a real schema or loader (the B-track additive-only rule, and the
// very policy B5 enforces: CFGV-03 no-rename/no-remove-without-migration).
// ---------------------------------------------------------------------------

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace vrclient::diagnostics {
class AsyncLogger;
}  // namespace vrclient::diagnostics

namespace vrclient::config {

// ---------------------------------------------------------------------------
// JsonValue: a minimal, self-contained JSON DOM.
//
// Numbers track an integer flag (mirroring hook_surface_doc.cpp's JsonValue) so a
// schema "version" integer is never silently widened to / mistaken for a float.
// Migrations mutate this DOM (add / rename / remove a field, set a default);
// `serialize()` round-trips it back to text.
// ---------------------------------------------------------------------------
class JsonValue {
 public:
  enum class Type { Null, Bool, Int, Double, String, Array, Object };

  JsonValue() = default;

  static JsonValue makeNull();
  static JsonValue makeBool(bool value);
  static JsonValue makeInt(std::int64_t value);
  static JsonValue makeDouble(double value);
  static JsonValue makeString(std::string value);
  static JsonValue makeArray();
  static JsonValue makeObject();

  Type type() const { return type_; }
  bool isNull() const { return type_ == Type::Null; }
  bool isBool() const { return type_ == Type::Bool; }
  bool isInt() const { return type_ == Type::Int; }
  bool isDouble() const { return type_ == Type::Double; }
  bool isNumber() const { return type_ == Type::Int || type_ == Type::Double; }
  bool isString() const { return type_ == Type::String; }
  bool isArray() const { return type_ == Type::Array; }
  bool isObject() const { return type_ == Type::Object; }

  bool asBool() const { return bool_; }
  std::int64_t asInt() const { return int_; }
  double asDouble() const { return type_ == Type::Int ? static_cast<double>(int_) : double_; }
  const std::string& asString() const { return string_; }

  // Array access.
  std::vector<JsonValue>& elements() { return elements_; }
  const std::vector<JsonValue>& elements() const { return elements_; }

  // Object access. Insertion order is preserved (first-seen), matching the repo's
  // existing readers and keeping serialize() deterministic.
  std::vector<std::pair<std::string, JsonValue>>& members() { return members_; }
  const std::vector<std::pair<std::string, JsonValue>>& members() const {
    return members_;
  }

  const JsonValue* find(const std::string& key) const;
  JsonValue* find(const std::string& key);
  bool has(const std::string& key) const { return find(key) != nullptr; }

  // Object mutation helpers used by migrations. setMember replaces an existing
  // member in place (preserving position) or appends a new one.
  void setMember(const std::string& key, JsonValue value);
  bool removeMember(const std::string& key);

  // Rename a field, preserving its value and its position. Returns false if the
  // source field is absent (so a migration can detect a corrupt input rather than
  // silently no-op). If `to` already exists it is overwritten.
  bool renameMember(const std::string& from, const std::string& to);

  // Set a field only if it is currently absent (used to add a defaulted field).
  bool setDefault(const std::string& key, JsonValue value);

  std::string serialize() const;

 private:
  Type type_ = Type::Null;
  bool bool_ = false;
  std::int64_t int_ = 0;
  double double_ = 0.0;
  std::string string_;
  std::vector<JsonValue> elements_;
  std::vector<std::pair<std::string, JsonValue>> members_;
};

// Parse text into a JsonValue. Returns true on success. On any malformed input
// (trailing junk, unterminated structure, excessive nesting depth, bad escape /
// number) it returns false and sets `error` — it NEVER returns a partial DOM.
// kMaxDepth bounds recursion against adversarial deeply-nested input.
bool parseJson(const std::string& text, JsonValue& out, std::string& error);
constexpr int kJsonMaxDepth = 64;

// Upper bound on any sane schema `version` integer. The JSON reader stores numbers
// as int64, so a config can declare an arbitrarily large (or 2^32-aliased) version.
// readVersion() rejects anything outside [1 .. kMaxSupportedSchemaVersion] BEFORE
// narrowing to int, so an oversized/garbage version becomes a deterministic
// refuse_invalid_version instead of being silently truncated into the in-range
// window and loaded as the current version (the no-silent-misparse property). The
// bound is intentionally generous (far beyond any plausible real schema version);
// its only job is to keep the value inside `int` range so the narrowing is exact.
constexpr std::int64_t kMaxSupportedSchemaVersion = 1000000;

// ---------------------------------------------------------------------------
// Negotiation decision (CFGV-01).
// ---------------------------------------------------------------------------
enum class ConfigDecision {
  Load,     // doc version == current: load as-is.
  Migrate,  // min <= doc version < current: apply the ordered migration chain.
  Refuse,   // out of range, missing, or unparseable: deterministic refusal.
};

const char* configDecisionName(ConfigDecision decision);

// Stable reason codes. Every Refuse carries one; Load/Migrate carry the
// corresponding success code so logs and tests can assert on a single string.
namespace reason {
inline constexpr const char* kLoadCurrent = "load_current_version";
inline constexpr const char* kMigrateInRange = "migrate_in_range";
inline constexpr const char* kRefuseTooNew = "refuse_too_new";
inline constexpr const char* kRefuseUnsupportedOld = "refuse_unsupported_old";
inline constexpr const char* kRefuseInvalidVersion = "refuse_invalid_version";
inline constexpr const char* kRefuseUnparseable = "refuse_unparseable_config";
inline constexpr const char* kRefuseUnknownKind = "refuse_unknown_kind";
inline constexpr const char* kRefuseMigrationFailed = "refuse_migration_failed";
}  // namespace reason

struct ConfigVersionDecision {
  ConfigDecision decision = ConfigDecision::Refuse;
  std::string reason_code = reason::kRefuseUnparseable;
  std::string config_kind;
  // from_version is the document's declared version (0 when missing/unparseable).
  int from_version = 0;
  // to_version is the registry's current version for the kind (0 for unknown kind).
  int to_version = 0;
  std::string detail;  // human-readable context; never load-bearing for the decision.

  bool isLoad() const { return decision == ConfigDecision::Load; }
  bool isMigrate() const { return decision == ConfigDecision::Migrate; }
  bool isRefuse() const { return decision == ConfigDecision::Refuse; }
};

// ---------------------------------------------------------------------------
// Migration framework.
//
// A MigrationStep transforms a doc from `from_version` to `from_version + 1`.
// `apply` mutates the DOM and returns "" on success or a non-empty error string.
// `validate` checks the OUTPUT doc is structurally acceptable for the target
// version (the synthetic v2 validator mirrors a schema check). A real future bump
// supplies its own apply+validate pair; the framework stays generic.
// ---------------------------------------------------------------------------
struct MigrationStep {
  int from_version = 0;
  int to_version = 0;
  std::string id;  // e.g. "demo-config:v1->v2"
  std::function<std::string(JsonValue&)> apply;
  std::function<std::string(const JsonValue&)> validate;
};

struct ConfigKindSpec {
  std::string kind;
  int min_version = 1;
  int current_version = 1;
  // Ordered chain; steps[i].from_version == i + min_version. Empty for kinds with
  // no migration yet (every real kind today). Length == current - min.
  std::vector<MigrationStep> migrations;
};

// Result of applying the migration chain.
struct MigrationResult {
  bool ok = false;
  std::string reason_code;  // reason::kMigrateInRange on success, otherwise a refuse code.
  std::string detail;
  int from_version = 0;
  int to_version = 0;
  std::vector<std::string> applied_step_ids;
  JsonValue document;  // the upgraded doc (only meaningful when ok == true).
};

// Reported accepted range for a single kind (CFGV-04).
struct ConfigVersionSupport {
  std::string config_kind;
  int min_version = 1;
  int current_version = 1;
};

// ---------------------------------------------------------------------------
// ConfigVersionRegistry: the single authority for LOAD / MIGRATE / REFUSE.
// ---------------------------------------------------------------------------
class ConfigVersionRegistry {
 public:
  // Builds the registry seeded with the real kinds (current=1, min=1) plus the
  // synthetic "demo-config" kind (current=2, min=1) with its v1->v2 migration.
  static ConfigVersionRegistry makeDefault();

  // Slugs of the real production config kinds. Each is registered at
  // current=1, min=1 (matching the on-disk schemas, all of which are version 1).
  static const std::vector<std::string>& realKinds();
  static constexpr const char* kSyntheticKind = "demo-config";

  void registerKind(ConfigKindSpec spec);
  const ConfigKindSpec* findKind(const std::string& kind) const;

  // Reads the integer "version" field from an already-parsed DOM.
  // Returns true and sets `version` only when the field is present AND an integer.
  // A missing or non-integer version returns false (caller -> REFUSE invalid).
  static bool readVersion(const JsonValue& doc, int& version);

  // CFGV-01: decide LOAD / MIGRATE / REFUSE for a doc supplied as RAW TEXT. This
  // is the no-silent-misparse entry point: it parses internally and a parse failure
  // becomes REFUSE(unparseable), never a guess.
  ConfigVersionDecision negotiateText(const std::string& kind,
                                      const std::string& text) const;

  // CFGV-01 for an already-parsed DOM (used after the caller has parsed once).
  ConfigVersionDecision negotiate(const std::string& kind,
                                  const JsonValue& doc) const;

  // CFGV-02: negotiate, and if the decision is MIGRATE, run the chain and return
  // the upgraded document. On LOAD the original doc is returned unchanged; on
  // REFUSE no document is produced (ok == false).
  MigrationResult negotiateAndMigrate(const std::string& kind,
                                      const std::string& text) const;

  // Apply the ordered migration chain to `doc`, validating each step's output.
  MigrationResult migrate(const std::string& kind, JsonValue doc,
                          int from_version) const;

  // CFGV-04: the accepted-version matrix for diagnostics.
  std::vector<ConfigVersionSupport> supportMatrix() const;

  // CFGV-04: emit the matrix through the existing AsyncLogger. nullptr-safe (a no-op
  // when no logger is supplied), matching the repo's logDetection precedent. Emits
  // one structured "config_version_support" event per kind. Returns the number of
  // events emitted (0 when logger == nullptr).
  std::size_t emitSupportMatrix(diagnostics::AsyncLogger* logger) const;

 private:
  // Sorted by kind for deterministic supportMatrix()/emit ordering.
  std::map<std::string, ConfigKindSpec> kinds_;
};

// ---------------------------------------------------------------------------
// The synthetic "demo-config" v1->v2 migration, exposed so tests can reference the
// exact transform. The migration is real-shaped: it renames `display_name` ->
// `title` and adds a defaulted `comfort_mode` field. The v2 validator below is the
// in-code mirror of the synthetic v2 schema.
// ---------------------------------------------------------------------------
MigrationStep makeDemoConfigV1ToV2();
std::string validateDemoConfigV1(const JsonValue& doc);
std::string validateDemoConfigV2(const JsonValue& doc);

}  // namespace vrclient::config

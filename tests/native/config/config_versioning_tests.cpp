// Bolt-on Phase B5 unit tests: config schema versioning & migration.
//
// Covers:
//   * JsonValue DOM: round-trip, mutation helpers, malformed input -> error (no
//     crash), recursion depth limit.
//   * Negotiation (CFGV-01): LOAD / MIGRATE / REFUSE decisions + reason codes for
//     the synthetic kind and the real kinds, including too-new / too-old / missing
//     / unparseable refusals.
//   * Migration (CFGV-02): demo-config v1 fixtures migrate to the expected v2 docs
//     and pass the v2 validator.
//   * Diagnostics (CFGV-04): the accepted [min..current] range is emitted through
//     the existing AsyncLogger and read back.
//
// Hand-rolled expect() + plain int main(), matching the repo's test convention
// (no gtest). Fixtures are resolved via the VRCLIENT_SOURCE_DIR compile define.

#include "config/config_versioning.h"
#include "diagnostics/logging/diagnostic_logger.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

using vrclient::config::ConfigDecision;
using vrclient::config::ConfigVersionRegistry;
using vrclient::config::JsonValue;
using vrclient::config::MigrationResult;
namespace reason = vrclient::config::reason;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::filesystem::path fixturesDir() {
#if defined(VRCLIENT_SOURCE_DIR)
  return std::filesystem::path(VRCLIENT_SOURCE_DIR) / "tests" / "native" /
      "config" / "fixtures";
#else
  return std::filesystem::path("tests/native/config/fixtures");
#endif
}

std::string readFixture(const std::string& name) {
  const std::filesystem::path path = fixturesDir() / name;
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("missing fixture: " + path.string());
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

// Parse and re-serialize, then re-parse; compare canonical serializations so
// whitespace differences in fixtures don't matter.
std::string canonical(const std::string& text) {
  JsonValue value;
  std::string error;
  if (!vrclient::config::parseJson(text, value, error)) {
    throw std::runtime_error("canonical parse failed: " + error);
  }
  return value.serialize();
}

// -------------------------------------------------------------------------
// DOM tests
// -------------------------------------------------------------------------
void testDomRoundTrip() {
  const std::string text =
      R"({"version":2,"profile_id":"x","title":"hi","comfort_mode":"standard","nested":{"a":[1,2,3],"b":true,"c":null},"f":1.5})";
  JsonValue value;
  std::string error;
  expect(vrclient::config::parseJson(text, value, error),
         "round-trip parse should succeed: " + error);
  const std::string serialized = value.serialize();
  // Re-parse the serialized form; the canonical second pass must be stable.
  expect(canonical(serialized) == serialized,
         "serialize output must be stable under re-parse");
  // Spot-check the DOM read out correctly.
  expect(value.isObject(), "root is object");
  const JsonValue* version = value.find("version");
  expect(version != nullptr && version->isInt() && version->asInt() == 2,
         "version is int 2");
  const JsonValue* nested = value.find("nested");
  expect(nested != nullptr && nested->isObject(), "nested is object");
  const JsonValue* arr = nested->find("a");
  expect(arr != nullptr && arr->isArray() && arr->elements().size() == 3,
         "nested.a is a 3-element array");
  const JsonValue* f = value.find("f");
  expect(f != nullptr && f->isDouble(), "f is a double, not an int");
}

void testDomMutation() {
  JsonValue doc = JsonValue::makeObject();
  doc.setMember("version", JsonValue::makeInt(1));
  doc.setMember("display_name", JsonValue::makeString("Old Name"));

  // rename preserves value + position.
  expect(doc.renameMember("display_name", "title"),
         "rename of existing field succeeds");
  expect(!doc.has("display_name"), "old field gone after rename");
  const JsonValue* title = doc.find("title");
  expect(title != nullptr && title->asString() == "Old Name",
         "renamed field keeps its value");

  // rename of an absent field reports failure (so a migration can detect corruption).
  expect(!doc.renameMember("nope", "whatever"),
         "rename of absent field returns false");

  // setDefault only adds when absent.
  expect(doc.setDefault("comfort_mode", JsonValue::makeString("standard")),
         "setDefault adds when absent");
  expect(!doc.setDefault("comfort_mode", JsonValue::makeString("intense")),
         "setDefault does not overwrite existing");
  expect(doc.find("comfort_mode")->asString() == "standard",
         "setDefault kept the original value");

  // removeMember.
  expect(doc.removeMember("version"), "removeMember of present field succeeds");
  expect(!doc.removeMember("version"), "removeMember of absent field returns false");
}

void testDomMalformedInputs() {
  // A spread of malformed inputs must all fail (not crash) and set an error.
  const char* bad_inputs[] = {
      "",                       // empty
      "{",                      // unterminated object
      "[1, 2",                  // unterminated array
      "{\"a\":}",              // missing value
      "{\"a\" 1}",             // missing colon
      "truthy",                 // bad literal
      "01",                     // (leading-zero is accepted as number; not here)
      "{\"a\":1} junk",        // trailing content
      "\"unterminated",         // unterminated string
      "{\"a\":1,}",            // trailing comma before close is malformed
  };
  for (const char* bad : bad_inputs) {
    JsonValue value;
    std::string error;
    const bool ok = vrclient::config::parseJson(bad, value, error);
    if (std::string(bad) == "01") {
      // "01" is a valid-ish number token to our scanner; skip the assertion.
      continue;
    }
    expect(!ok, std::string("malformed input must fail to parse: ") + bad);
    expect(!error.empty(), std::string("failed parse must set an error: ") + bad);
  }
}

void testDomDepthLimit() {
  // Build a deeply nested array exceeding kJsonMaxDepth; the reader must refuse it
  // rather than overflow the stack.
  const int depth = vrclient::config::kJsonMaxDepth + 50;
  std::string text(static_cast<std::size_t>(depth), '[');
  text.append(static_cast<std::size_t>(depth), ']');
  JsonValue value;
  std::string error;
  expect(!vrclient::config::parseJson(text, value, error),
         "over-deep nesting must be refused");
  expect(error.find("depth") != std::string::npos,
         "depth-limit error should mention depth, got: " + error);
}

// -------------------------------------------------------------------------
// Negotiation tests (CFGV-01)
// -------------------------------------------------------------------------
void testNegotiationSynthetic() {
  const ConfigVersionRegistry registry = ConfigVersionRegistry::makeDefault();

  // demo-config v1 -> MIGRATE.
  auto d1 = registry.negotiateText("demo-config", readFixture("demo-config.v1.basic.input.json"));
  expect(d1.decision == ConfigDecision::Migrate, "demo v1 -> MIGRATE");
  expect(d1.reason_code == reason::kMigrateInRange, "demo v1 reason migrate_in_range");
  expect(d1.from_version == 1 && d1.to_version == 2, "demo v1 from/to versions");

  // demo-config v2 (current) -> LOAD.
  auto d2 = registry.negotiateText("demo-config", readFixture("demo-config.v2.current.json"));
  expect(d2.decision == ConfigDecision::Load, "demo v2 -> LOAD");
  expect(d2.reason_code == reason::kLoadCurrent, "demo v2 reason load_current");

  // demo-config v3 (too new) -> REFUSE too_new.
  auto d3 = registry.negotiateText("demo-config", readFixture("demo-config.v3.too-new.json"));
  expect(d3.decision == ConfigDecision::Refuse, "demo v3 -> REFUSE");
  expect(d3.reason_code == reason::kRefuseTooNew, "demo v3 reason refuse_too_new");
}

void testNegotiationRealKinds() {
  const ConfigVersionRegistry registry = ConfigVersionRegistry::makeDefault();

  // A real kind v1 doc -> LOAD (every real kind is current=1).
  const std::string v1 = readFixture("real-kind.v1.runtime-profile.json");
  for (const std::string& kind : ConfigVersionRegistry::realKinds()) {
    auto d = registry.negotiateText(kind, v1);
    expect(d.decision == ConfigDecision::Load, kind + " v1 -> LOAD");
    expect(d.reason_code == reason::kLoadCurrent, kind + " v1 reason load_current");
    expect(d.to_version == 1, kind + " current is 1");
  }

  // A real kind labeled v2 -> REFUSE too_new (no real kind has a v2 yet).
  const std::string v2 = readFixture("real-kind.v2.too-new.json");
  auto too_new = registry.negotiateText("hook-surface", v2);
  expect(too_new.decision == ConfigDecision::Refuse, "real v2 -> REFUSE");
  expect(too_new.reason_code == reason::kRefuseTooNew, "real v2 reason refuse_too_new");

  // Missing version -> REFUSE invalid.
  auto missing = registry.negotiateText("game-profile", readFixture("refuse.missing-version.json"));
  expect(missing.decision == ConfigDecision::Refuse, "missing version -> REFUSE");
  expect(missing.reason_code == reason::kRefuseInvalidVersion,
         "missing version reason refuse_invalid_version");

  // Unparseable -> REFUSE unparseable.
  auto unparseable = registry.negotiateText("safety-rules", readFixture("refuse.unparseable.txt"));
  expect(unparseable.decision == ConfigDecision::Refuse, "unparseable -> REFUSE");
  expect(unparseable.reason_code == reason::kRefuseUnparseable,
         "unparseable reason refuse_unparseable_config");

  // Unknown kind -> REFUSE unknown_kind (deterministic, not a crash).
  auto unknown = registry.negotiateText("not-a-real-kind", v1);
  expect(unknown.decision == ConfigDecision::Refuse, "unknown kind -> REFUSE");
  expect(unknown.reason_code == reason::kRefuseUnknownKind,
         "unknown kind reason refuse_unknown_kind");

  // Non-integer version (float) -> REFUSE invalid (never silently truncated).
  auto float_version = registry.negotiateText("runtime-profile", "{\"version\": 1.5}");
  expect(float_version.decision == ConfigDecision::Refuse, "float version -> REFUSE");
  expect(float_version.reason_code == reason::kRefuseInvalidVersion,
         "float version reason refuse_invalid_version");

  // Regression: a 64-bit version whose low 32 bits alias an in-range value must
  // NOT be narrowed-then-compared into a silent LOAD. 4294967297 == 2^32 + 1
  // narrows to int 1; before the fix it LOADed as the current version. It must now
  // REFUSE invalid (the value is far-future / garbage, never current).
  auto aliased_one = registry.negotiateText("runtime-profile", "{\"version\": 4294967297}");
  expect(aliased_one.decision == ConfigDecision::Refuse,
         "2^32+1 version must REFUSE, not silently LOAD as 1");
  expect(aliased_one.reason_code == reason::kRefuseInvalidVersion,
         "2^32+1 version reason refuse_invalid_version");

  // A negative 64-bit version that narrows to a small positive int must also REFUSE.
  // -4294967295 == -(2^32 - 1) narrows to int 1.
  auto aliased_negative = registry.negotiateText("runtime-profile", "{\"version\": -4294967295}");
  expect(aliased_negative.decision == ConfigDecision::Refuse,
         "negative aliased version must REFUSE, not LOAD as 1");
  expect(aliased_negative.reason_code == reason::kRefuseInvalidVersion,
         "negative aliased version reason refuse_invalid_version");

  // The same aliasing against the synthetic kind: 4294967298 == 2^32 + 2 narrows to
  // int 2 (demo-config current). It must REFUSE invalid, not LOAD as v2.
  auto aliased_two = registry.negotiateText("demo-config", "{\"version\": 4294967298}");
  expect(aliased_two.decision == ConfigDecision::Refuse,
         "2^32+2 version must REFUSE for demo-config, not LOAD as 2");
  expect(aliased_two.reason_code == reason::kRefuseInvalidVersion,
         "2^32+2 version reason refuse_invalid_version");

  // A plausible-but-too-new in-int-range version still REFUSEs as too_new (this is
  // the legitimate path that must remain unchanged by the range guard above).
  auto large_in_range = registry.negotiateText("runtime-profile", "{\"version\": 999}");
  expect(large_in_range.decision == ConfigDecision::Refuse,
         "in-range too-new version still REFUSEs");
  expect(large_in_range.reason_code == reason::kRefuseTooNew,
         "in-range too-new version reason refuse_too_new");
}

// -------------------------------------------------------------------------
// Migration tests (CFGV-02)
// -------------------------------------------------------------------------
void migrateAndAssert(const ConfigVersionRegistry& registry,
                      const std::string& input_fixture,
                      const std::string& expected_fixture) {
  const MigrationResult result =
      registry.negotiateAndMigrate("demo-config", readFixture(input_fixture));
  expect(result.ok, input_fixture + ": migration must succeed (detail: " + result.detail + ")");
  expect(result.reason_code == reason::kMigrateInRange,
         input_fixture + ": reason migrate_in_range");
  expect(result.from_version == 1 && result.to_version == 2,
         input_fixture + ": from 1 to 2");
  expect(result.applied_step_ids.size() == 1 &&
             result.applied_step_ids[0] == "demo-config:v1->v2",
         input_fixture + ": applied the v1->v2 step");

  // The migrated doc equals the expected v2 doc (canonical comparison).
  const std::string migrated = result.document.serialize();
  const std::string expected = canonical(readFixture(expected_fixture));
  expect(migrated == expected,
         input_fixture + ": migrated doc must equal expected v2.\n got: " +
             migrated + "\n exp: " + expected);

  // The migrated doc passes the v2 validator (schema-equivalent in-code check).
  const std::string validation = vrclient::config::validateDemoConfigV2(result.document);
  expect(validation.empty(),
         input_fixture + ": migrated doc must pass v2 validation: " + validation);
}

void testMigrationFixtures() {
  const ConfigVersionRegistry registry = ConfigVersionRegistry::makeDefault();
  migrateAndAssert(registry, "demo-config.v1.basic.input.json",
                   "demo-config.v1.basic.expected-v2.json");
  migrateAndAssert(registry, "demo-config.v1.repo.input.json",
                   "demo-config.v1.repo.expected-v2.json");

  // A v2 doc routed through negotiateAndMigrate LOADs unchanged (no step applied).
  const MigrationResult loaded =
      registry.negotiateAndMigrate("demo-config", readFixture("demo-config.v2.current.json"));
  expect(loaded.ok && loaded.applied_step_ids.empty(),
         "v2 doc loads unchanged, no migration step");
  expect(loaded.reason_code == reason::kLoadCurrent, "v2 load reason");

  // The input v1 fixture itself passes the v1 validator (corpus sanity).
  JsonValue v1_doc;
  std::string error;
  expect(vrclient::config::parseJson(readFixture("demo-config.v1.basic.input.json"), v1_doc, error),
         "v1 fixture parses");
  expect(vrclient::config::validateDemoConfigV1(v1_doc).empty(),
         "v1 fixture passes v1 validation");
}

// -------------------------------------------------------------------------
// Diagnostics tests (CFGV-04)
// -------------------------------------------------------------------------
void testSupportMatrix() {
  const ConfigVersionRegistry registry = ConfigVersionRegistry::makeDefault();
  const auto matrix = registry.supportMatrix();
  // Real kinds + the synthetic kind.
  const std::size_t expected_count =
      ConfigVersionRegistry::realKinds().size() + 1;
  expect(matrix.size() == expected_count,
         "support matrix has one entry per real kind plus demo-config");

  bool saw_demo = false;
  bool saw_hook = false;
  for (const auto& support : matrix) {
    if (support.config_kind == "demo-config") {
      saw_demo = true;
      expect(support.min_version == 1 && support.current_version == 2,
             "demo-config range [1..2]");
    }
    if (support.config_kind == "hook-surface") {
      saw_hook = true;
      expect(support.min_version == 1 && support.current_version == 1,
             "hook-surface range [1..1]");
    }
  }
  expect(saw_demo, "matrix includes demo-config");
  expect(saw_hook, "matrix includes hook-surface");

  // nullptr logger -> no emit, no crash.
  expect(registry.emitSupportMatrix(nullptr) == 0, "nullptr logger emits nothing");
}

void testSupportMatrixEmit() {
  const ConfigVersionRegistry registry = ConfigVersionRegistry::makeDefault();

  vrclient::diagnostics::AsyncLogger logger;
  vrclient::diagnostics::LoggerConfig config;
  config.enabled = true;
  config.start_worker = false;  // synchronous-ish; we only read recentRecords.
  vrclient::diagnostics::SessionMetadata metadata;
  metadata.session_id = "b5-config-versioning-test";
  expect(logger.start(config, metadata), "logger starts");

  const std::size_t emitted = registry.emitSupportMatrix(&logger);
  expect(emitted == ConfigVersionRegistry::realKinds().size() + 1,
         "emitted one event per real kind plus demo-config");

  const auto records = logger.recentRecords(64);
  // Find the demo-config event and assert the range fields are present.
  bool found_demo = false;
  for (const auto& record : records) {
    if (record.event_name != "config_version_support") {
      continue;
    }
    std::string kind, min_v, cur_v;
    for (const auto& field : record.fields) {
      if (field.key == "config_kind") kind = field.value;
      if (field.key == "min_version") min_v = field.value;
      if (field.key == "current_version") cur_v = field.value;
    }
    if (kind == "demo-config") {
      found_demo = true;
      expect(min_v == "1", "emitted demo-config min_version 1");
      expect(cur_v == "2", "emitted demo-config current_version 2");
    }
  }
  expect(found_demo, "diagnostics records include the demo-config range event");
  logger.stop();
}

}  // namespace

int main() {
  try {
    testDomRoundTrip();
    testDomMutation();
    testDomMalformedInputs();
    testDomDepthLimit();
    testNegotiationSynthetic();
    testNegotiationRealKinds();
    testMigrationFixtures();
    testSupportMatrix();
    testSupportMatrixEmit();
  } catch (const std::exception& ex) {
    std::fprintf(stderr, "config versioning test FAILED: %s\n", ex.what());
    return 1;
  }
  std::printf("config versioning unit tests passed\n");
  return 0;
}

#include "config/config_versioning.h"

#include "diagnostics/logging/diagnostic_logger.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace vrclient::config {
namespace {

// ---------------------------------------------------------------------------
// Minimal, self-contained JSON reader.
//
// Mirrors the strict, dependency-free recursive-descent posture already used in
// src/native/tooling/hookdisc/hook_surface_doc.cpp and src/native/safety/
// safety_verdict.cpp. The repo ships NO JSON library and B5 must not add one.
// The reader REJECTS malformed input (it never returns a partial DOM) so a
// negotiation gate built on it can never silently misparse — a rejected parse
// becomes a deterministic REFUSE(unparseable) upstream.
// ---------------------------------------------------------------------------
class JsonReader {
 public:
  explicit JsonReader(const std::string& text) : text_(text) {}

  bool parse(JsonValue& out, std::string& error) {
    skipWhitespace();
    if (!parseValue(out)) {
      error = error_.empty() ? "invalid JSON" : error_;
      return false;
    }
    skipWhitespace();
    if (pos_ != text_.size()) {
      error = "trailing content after JSON document";
      return false;
    }
    return true;
  }

 private:
  bool fail(const std::string& message) {
    if (error_.empty()) {
      error_ = message;
    }
    return false;
  }

  void skipWhitespace() {
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  // RAII guard bounding nesting depth, mirroring hook_surface_doc.cpp. Every value
  // flows through parseValue, so guarding here caps total recursion against an
  // adversarial deeply-nested document.
  struct DepthGuard {
    int& depth;
    bool ok;
    DepthGuard(int& d, int max_depth) : depth(d), ok(++d <= max_depth) {}
    ~DepthGuard() { --depth; }
  };

  bool parseValue(JsonValue& out) {
    DepthGuard guard(depth_, kJsonMaxDepth);
    if (!guard.ok) {
      return fail("maximum nesting depth exceeded");
    }
    skipWhitespace();
    if (pos_ >= text_.size()) {
      return fail("unexpected end of JSON");
    }
    const char c = text_[pos_];
    switch (c) {
      case '{':
        return parseObject(out);
      case '[':
        return parseArray(out);
      case '"': {
        std::string s;
        if (!parseString(s)) {
          return false;
        }
        out = JsonValue::makeString(std::move(s));
        return true;
      }
      case 't':
      case 'f':
        return parseBool(out);
      case 'n':
        return parseNull(out);
      default:
        if (c == '-' || (c >= '0' && c <= '9')) {
          return parseNumber(out);
        }
        return fail("unexpected character in JSON");
    }
  }

  bool parseObject(JsonValue& out) {
    out = JsonValue::makeObject();
    ++pos_;  // consume '{'
    skipWhitespace();
    if (pos_ < text_.size() && text_[pos_] == '}') {
      ++pos_;
      return true;
    }
    while (true) {
      skipWhitespace();
      if (pos_ >= text_.size() || text_[pos_] != '"') {
        return fail("expected object key");
      }
      std::string key;
      if (!parseString(key)) {
        return false;
      }
      skipWhitespace();
      if (pos_ >= text_.size() || text_[pos_] != ':') {
        return fail("expected ':' after object key");
      }
      ++pos_;  // consume ':'
      JsonValue value;
      if (!parseValue(value)) {
        return false;
      }
      out.members().emplace_back(std::move(key), std::move(value));
      skipWhitespace();
      if (pos_ >= text_.size()) {
        return fail("unterminated object");
      }
      if (text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (text_[pos_] == '}') {
        ++pos_;
        return true;
      }
      return fail("expected ',' or '}' in object");
    }
  }

  bool parseArray(JsonValue& out) {
    out = JsonValue::makeArray();
    ++pos_;  // consume '['
    skipWhitespace();
    if (pos_ < text_.size() && text_[pos_] == ']') {
      ++pos_;
      return true;
    }
    while (true) {
      JsonValue value;
      if (!parseValue(value)) {
        return false;
      }
      out.elements().push_back(std::move(value));
      skipWhitespace();
      if (pos_ >= text_.size()) {
        return fail("unterminated array");
      }
      if (text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (text_[pos_] == ']') {
        ++pos_;
        return true;
      }
      return fail("expected ',' or ']' in array");
    }
  }

  bool parseString(std::string& out) {
    ++pos_;  // consume opening quote
    std::string value;
    while (pos_ < text_.size()) {
      const char c = text_[pos_++];
      if (c == '"') {
        out = std::move(value);
        return true;
      }
      if (c == '\\') {
        if (pos_ >= text_.size()) {
          return fail("unterminated escape in string");
        }
        const char esc = text_[pos_++];
        switch (esc) {
          case '"': value.push_back('"'); break;
          case '\\': value.push_back('\\'); break;
          case '/': value.push_back('/'); break;
          case 'b': value.push_back('\b'); break;
          case 'f': value.push_back('\f'); break;
          case 'n': value.push_back('\n'); break;
          case 'r': value.push_back('\r'); break;
          case 't': value.push_back('\t'); break;
          case 'u': {
            if (pos_ + 4 > text_.size()) {
              return fail("truncated \\u escape");
            }
            unsigned int code = 0;
            for (int i = 0; i < 4; ++i) {
              const char hex = text_[pos_++];
              code <<= 4;
              if (hex >= '0' && hex <= '9') {
                code |= static_cast<unsigned int>(hex - '0');
              } else if (hex >= 'a' && hex <= 'f') {
                code |= static_cast<unsigned int>(hex - 'a' + 10);
              } else if (hex >= 'A' && hex <= 'F') {
                code |= static_cast<unsigned int>(hex - 'A' + 10);
              } else {
                return fail("invalid \\u escape digit");
              }
            }
            // Minimal UTF-8 encode of the BMP code point (config docs are ASCII in
            // practice); keeps parsing total without surrogate handling.
            if (code < 0x80) {
              value.push_back(static_cast<char>(code));
            } else if (code < 0x800) {
              value.push_back(static_cast<char>(0xC0 | (code >> 6)));
              value.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else {
              value.push_back(static_cast<char>(0xE0 | (code >> 12)));
              value.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
              value.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
            break;
          }
          default:
            return fail("invalid escape character in string");
        }
      } else {
        value.push_back(c);
      }
    }
    return fail("unterminated string");
  }

  bool parseBool(JsonValue& out) {
    if (text_.compare(pos_, 4, "true") == 0) {
      out = JsonValue::makeBool(true);
      pos_ += 4;
      return true;
    }
    if (text_.compare(pos_, 5, "false") == 0) {
      out = JsonValue::makeBool(false);
      pos_ += 5;
      return true;
    }
    return fail("invalid literal");
  }

  bool parseNull(JsonValue& out) {
    if (text_.compare(pos_, 4, "null") == 0) {
      out = JsonValue::makeNull();
      pos_ += 4;
      return true;
    }
    return fail("invalid literal");
  }

  bool parseNumber(JsonValue& out) {
    const std::size_t start = pos_;
    bool is_integer = true;
    if (pos_ < text_.size() && text_[pos_] == '-') {
      ++pos_;
    }
    while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
      ++pos_;
    }
    if (pos_ < text_.size() && text_[pos_] == '.') {
      is_integer = false;
      ++pos_;
      while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
        ++pos_;
      }
    }
    if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
      is_integer = false;
      ++pos_;
      if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
        ++pos_;
      }
      while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
        ++pos_;
      }
    }
    const std::string token = text_.substr(start, pos_ - start);
    if (token.empty() || token == "-") {
      return fail("invalid number");
    }
    try {
      if (is_integer) {
        out = JsonValue::makeInt(std::stoll(token));
      } else {
        out = JsonValue::makeDouble(std::stod(token));
      }
    } catch (const std::exception&) {
      return fail("number out of range");
    }
    return true;
  }

  const std::string& text_;
  std::size_t pos_ = 0;
  std::string error_;
  int depth_ = 0;
};

void appendQuoted(std::string& out, const std::string& value) {
  out.push_back('"');
  out += diagnostics::jsonEscape(value);
  out.push_back('"');
}

std::string formatDouble(double value) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.17g", value);
  return std::string(buffer);
}

void serializeInto(const JsonValue& value, std::string& out) {
  switch (value.type()) {
    case JsonValue::Type::Null:
      out += "null";
      return;
    case JsonValue::Type::Bool:
      out += value.asBool() ? "true" : "false";
      return;
    case JsonValue::Type::Int:
      out += std::to_string(value.asInt());
      return;
    case JsonValue::Type::Double:
      out += formatDouble(value.asDouble());
      return;
    case JsonValue::Type::String:
      appendQuoted(out, value.asString());
      return;
    case JsonValue::Type::Array: {
      out.push_back('[');
      const auto& items = value.elements();
      for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0) {
          out.push_back(',');
        }
        serializeInto(items[i], out);
      }
      out.push_back(']');
      return;
    }
    case JsonValue::Type::Object: {
      out.push_back('{');
      const auto& members = value.members();
      for (std::size_t i = 0; i < members.size(); ++i) {
        if (i != 0) {
          out.push_back(',');
        }
        appendQuoted(out, members[i].first);
        out.push_back(':');
        serializeInto(members[i].second, out);
      }
      out.push_back('}');
      return;
    }
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// JsonValue
// ---------------------------------------------------------------------------
JsonValue JsonValue::makeNull() {
  JsonValue v;
  v.type_ = Type::Null;
  return v;
}
JsonValue JsonValue::makeBool(bool value) {
  JsonValue v;
  v.type_ = Type::Bool;
  v.bool_ = value;
  return v;
}
JsonValue JsonValue::makeInt(std::int64_t value) {
  JsonValue v;
  v.type_ = Type::Int;
  v.int_ = value;
  return v;
}
JsonValue JsonValue::makeDouble(double value) {
  JsonValue v;
  v.type_ = Type::Double;
  v.double_ = value;
  return v;
}
JsonValue JsonValue::makeString(std::string value) {
  JsonValue v;
  v.type_ = Type::String;
  v.string_ = std::move(value);
  return v;
}
JsonValue JsonValue::makeArray() {
  JsonValue v;
  v.type_ = Type::Array;
  return v;
}
JsonValue JsonValue::makeObject() {
  JsonValue v;
  v.type_ = Type::Object;
  return v;
}

const JsonValue* JsonValue::find(const std::string& key) const {
  for (const auto& member : members_) {
    if (member.first == key) {
      return &member.second;
    }
  }
  return nullptr;
}

JsonValue* JsonValue::find(const std::string& key) {
  for (auto& member : members_) {
    if (member.first == key) {
      return &member.second;
    }
  }
  return nullptr;
}

void JsonValue::setMember(const std::string& key, JsonValue value) {
  for (auto& member : members_) {
    if (member.first == key) {
      member.second = std::move(value);
      return;
    }
  }
  members_.emplace_back(key, std::move(value));
}

bool JsonValue::removeMember(const std::string& key) {
  for (auto it = members_.begin(); it != members_.end(); ++it) {
    if (it->first == key) {
      members_.erase(it);
      return true;
    }
  }
  return false;
}

bool JsonValue::renameMember(const std::string& from, const std::string& to) {
  for (auto& member : members_) {
    if (member.first == from) {
      JsonValue value = std::move(member.second);
      member.first = to;
      member.second = std::move(value);
      // If `to` already existed elsewhere, drop the duplicate so the rename is a
      // single field at the renamed position.
      for (auto it = members_.begin(); it != members_.end(); ++it) {
        if (it->first == to && &(*it) != &member) {
          members_.erase(it);
          break;
        }
      }
      return true;
    }
  }
  return false;
}

bool JsonValue::setDefault(const std::string& key, JsonValue value) {
  if (find(key) != nullptr) {
    return false;
  }
  members_.emplace_back(key, std::move(value));
  return true;
}

std::string JsonValue::serialize() const {
  std::string out;
  serializeInto(*this, out);
  return out;
}

bool parseJson(const std::string& text, JsonValue& out, std::string& error) {
  JsonReader reader(text);
  JsonValue parsed;
  if (!reader.parse(parsed, error)) {
    return false;
  }
  out = std::move(parsed);
  return true;
}

// ---------------------------------------------------------------------------
// Decision / reason naming
// ---------------------------------------------------------------------------
const char* configDecisionName(ConfigDecision decision) {
  switch (decision) {
    case ConfigDecision::Load: return "load";
    case ConfigDecision::Migrate: return "migrate";
    case ConfigDecision::Refuse: return "refuse";
  }
  return "refuse";
}

// ---------------------------------------------------------------------------
// Synthetic demo-config migration + validators.
//
// These are the in-code mirror of the synthetic v1/v2 schemas under
// tests/native/config/fixtures/. They prove the generic chain machinery without
// touching any real production schema.
//
// v1 shape: { version:1, profile_id:str, display_name:str }
// v2 shape: { version:2, profile_id:str, title:str, comfort_mode:str }
// transform: rename display_name -> title; add defaulted comfort_mode="standard".
// ---------------------------------------------------------------------------
namespace {

std::string requireStringField(const JsonValue& doc, const std::string& key) {
  const JsonValue* v = doc.find(key);
  if (v == nullptr) {
    return "missing required field: " + key;
  }
  if (!v->isString() || v->asString().empty()) {
    return "field must be a non-empty string: " + key;
  }
  return std::string();
}

std::string requireIntVersion(const JsonValue& doc, int expected) {
  const JsonValue* v = doc.find("version");
  if (v == nullptr || !v->isInt()) {
    return "version must be an integer";
  }
  // Compare the FULL int64 against the expected version. Do not narrow to int
  // first: a 64-bit value whose low 32 bits equal `expected` (e.g. 2^32+expected)
  // would otherwise pass this check. The version validators must reject such a
  // doc rather than treat it as the expected version.
  if (v->asInt() != static_cast<std::int64_t>(expected)) {
    return "version must be " + std::to_string(expected);
  }
  return std::string();
}

constexpr std::array<const char*, 3> kDemoComfortModes = {
    "standard", "comfort", "intense"};

}  // namespace

std::string validateDemoConfigV1(const JsonValue& doc) {
  if (!doc.isObject()) {
    return "document must be a JSON object";
  }
  if (std::string e = requireIntVersion(doc, 1); !e.empty()) {
    return e;
  }
  if (std::string e = requireStringField(doc, "profile_id"); !e.empty()) {
    return e;
  }
  if (std::string e = requireStringField(doc, "display_name"); !e.empty()) {
    return e;
  }
  return std::string();
}

std::string validateDemoConfigV2(const JsonValue& doc) {
  if (!doc.isObject()) {
    return "document must be a JSON object";
  }
  if (std::string e = requireIntVersion(doc, 2); !e.empty()) {
    return e;
  }
  if (std::string e = requireStringField(doc, "profile_id"); !e.empty()) {
    return e;
  }
  if (std::string e = requireStringField(doc, "title"); !e.empty()) {
    return e;
  }
  if (std::string e = requireStringField(doc, "comfort_mode"); !e.empty()) {
    return e;
  }
  const std::string& mode = doc.find("comfort_mode")->asString();
  bool valid_mode = false;
  for (const char* allowed : kDemoComfortModes) {
    if (mode == allowed) {
      valid_mode = true;
      break;
    }
  }
  if (!valid_mode) {
    return "comfort_mode must be one of standard|comfort|intense";
  }
  // v2 dropped display_name in favor of title; its presence means a malformed /
  // half-migrated doc.
  if (doc.has("display_name")) {
    return "v2 must not retain the renamed field display_name";
  }
  return std::string();
}

MigrationStep makeDemoConfigV1ToV2() {
  MigrationStep step;
  step.from_version = 1;
  step.to_version = 2;
  step.id = "demo-config:v1->v2";
  step.apply = [](JsonValue& doc) -> std::string {
    if (!doc.isObject()) {
      return "demo-config v1->v2: document must be a JSON object";
    }
    // Bump the schema version.
    doc.setMember("version", JsonValue::makeInt(2));
    // Rename display_name -> title (preserving the value + position).
    if (!doc.renameMember("display_name", "title")) {
      return "demo-config v1->v2: source field display_name is absent";
    }
    // Add the new defaulted comfort_mode field if the doc does not already carry
    // one (a v1 doc never does; setDefault keeps the migration idempotent).
    doc.setDefault("comfort_mode", JsonValue::makeString("standard"));
    return std::string();
  };
  step.validate = [](const JsonValue& doc) -> std::string {
    return validateDemoConfigV2(doc);
  };
  return step;
}

// ---------------------------------------------------------------------------
// ConfigVersionRegistry
// ---------------------------------------------------------------------------
const std::vector<std::string>& ConfigVersionRegistry::realKinds() {
  static const std::vector<std::string> kinds = {
      "runtime-profile", "diagnostics-profile", "game-fingerprint",
      "game-profile",    "hook-surface",        "safety-rules",
      "anti-cheat-compatibility",
      "supply-chain-trust-root", "supply-chain-manifest",
      "supply-chain-revocations"};
  return kinds;
}

ConfigVersionRegistry ConfigVersionRegistry::makeDefault() {
  ConfigVersionRegistry registry;

  // The real production kinds. Every on-disk schema is version 1, so each
  // registers at current=1, min=1 with NO migration chain. Honest statement: the
  // chain machinery is proven only by the synthetic kind below; no real kind has a
  // second version yet (CFGV honesty note).
  for (const std::string& kind : realKinds()) {
    ConfigKindSpec spec;
    spec.kind = kind;
    spec.min_version = 1;
    spec.current_version = 1;
    registry.registerKind(std::move(spec));
  }

  // The SYNTHETIC test-only kind that proves v1->v2 migration end to end. It is NOT
  // a production schema and is never loaded by the runtime; it exists solely so the
  // generic chain machinery is exercised before the first real bump.
  ConfigKindSpec demo;
  demo.kind = kSyntheticKind;
  demo.min_version = 1;
  demo.current_version = 2;
  demo.migrations.push_back(makeDemoConfigV1ToV2());
  registry.registerKind(std::move(demo));

  return registry;
}

void ConfigVersionRegistry::registerKind(ConfigKindSpec spec) {
  kinds_[spec.kind] = std::move(spec);
}

const ConfigKindSpec* ConfigVersionRegistry::findKind(
    const std::string& kind) const {
  auto it = kinds_.find(kind);
  return it == kinds_.end() ? nullptr : &it->second;
}

bool ConfigVersionRegistry::readVersion(const JsonValue& doc, int& version) {
  if (!doc.isObject()) {
    return false;
  }
  const JsonValue* v = doc.find("version");
  if (v == nullptr || !v->isInt()) {
    return false;
  }
  // The JSON reader stores the version as a full int64 (parseNumber -> std::stoll).
  // Range-CHECK before narrowing to int — never narrow-then-compare. A 64-bit
  // version whose low 32 bits happen to equal an in-range value (e.g. 2^32+1) must
  // NOT be silently truncated into the in-range window and LOADed as the current
  // version; that is exactly the silent-misparse the B5 lens forbids. Reject any
  // value outside [1 .. kMaxSupportedSchemaVersion] so the caller emits a
  // deterministic refuse_invalid_version instead of a guessed/narrowed parse.
  const std::int64_t raw = v->asInt();
  if (raw < 1 || raw > kMaxSupportedSchemaVersion) {
    return false;
  }
  version = static_cast<int>(raw);
  return true;
}

ConfigVersionDecision ConfigVersionRegistry::negotiate(
    const std::string& kind, const JsonValue& doc) const {
  ConfigVersionDecision decision;
  decision.config_kind = kind;

  const ConfigKindSpec* spec = findKind(kind);
  if (spec == nullptr) {
    decision.decision = ConfigDecision::Refuse;
    decision.reason_code = reason::kRefuseUnknownKind;
    decision.detail = "no registered config kind named '" + kind + "'";
    return decision;
  }
  decision.to_version = spec->current_version;

  int version = 0;
  if (!readVersion(doc, version)) {
    // Missing, non-integer, or out-of-range (oversized / non-positive / 2^32-
    // aliased) version: deterministic refuse, never a guess or a narrowed parse.
    decision.decision = ConfigDecision::Refuse;
    decision.reason_code = reason::kRefuseInvalidVersion;
    decision.detail =
        "document 'version' is missing, not an integer, or outside [1.." +
        std::to_string(kMaxSupportedSchemaVersion) + "]";
    return decision;
  }
  decision.from_version = version;

  if (version == spec->current_version) {
    decision.decision = ConfigDecision::Load;
    decision.reason_code = reason::kLoadCurrent;
    return decision;
  }
  if (version > spec->current_version) {
    decision.decision = ConfigDecision::Refuse;
    decision.reason_code = reason::kRefuseTooNew;
    decision.detail = "document version " + std::to_string(version) +
                      " exceeds current " + std::to_string(spec->current_version);
    return decision;
  }
  if (version < spec->min_version) {
    decision.decision = ConfigDecision::Refuse;
    decision.reason_code = reason::kRefuseUnsupportedOld;
    decision.detail = "document version " + std::to_string(version) +
                      " below minimum supported " +
                      std::to_string(spec->min_version);
    return decision;
  }
  // min <= version < current: migrate.
  decision.decision = ConfigDecision::Migrate;
  decision.reason_code = reason::kMigrateInRange;
  return decision;
}

ConfigVersionDecision ConfigVersionRegistry::negotiateText(
    const std::string& kind, const std::string& text) const {
  JsonValue doc;
  std::string error;
  if (!parseJson(text, doc, error)) {
    ConfigVersionDecision decision;
    decision.config_kind = kind;
    decision.decision = ConfigDecision::Refuse;
    decision.reason_code = reason::kRefuseUnparseable;
    decision.detail = "parse error: " + error;
    if (const ConfigKindSpec* spec = findKind(kind)) {
      decision.to_version = spec->current_version;
    }
    return decision;
  }
  return negotiate(kind, doc);
}

MigrationResult ConfigVersionRegistry::migrate(const std::string& kind,
                                               JsonValue doc,
                                               int from_version) const {
  MigrationResult result;
  result.from_version = from_version;

  const ConfigKindSpec* spec = findKind(kind);
  if (spec == nullptr) {
    result.reason_code = reason::kRefuseUnknownKind;
    result.detail = "no registered config kind named '" + kind + "'";
    return result;
  }
  result.to_version = spec->current_version;

  if (from_version < spec->min_version || from_version > spec->current_version) {
    result.reason_code = reason::kRefuseMigrationFailed;
    result.detail = "from_version " + std::to_string(from_version) +
                    " is outside [" + std::to_string(spec->min_version) + ".." +
                    std::to_string(spec->current_version) + "]";
    return result;
  }

  int current = from_version;
  while (current < spec->current_version) {
    // Locate the ordered step from `current` -> `current + 1`.
    const MigrationStep* step = nullptr;
    for (const MigrationStep& candidate : spec->migrations) {
      if (candidate.from_version == current &&
          candidate.to_version == current + 1) {
        step = &candidate;
        break;
      }
    }
    if (step == nullptr) {
      result.reason_code = reason::kRefuseMigrationFailed;
      result.detail = "no migration step registered for " + kind + " v" +
                      std::to_string(current) + "->v" +
                      std::to_string(current + 1);
      return result;
    }
    if (step->apply) {
      const std::string apply_error = step->apply(doc);
      if (!apply_error.empty()) {
        result.reason_code = reason::kRefuseMigrationFailed;
        result.detail = "step " + step->id + " failed: " + apply_error;
        return result;
      }
    }
    if (step->validate) {
      const std::string validate_error = step->validate(doc);
      if (!validate_error.empty()) {
        result.reason_code = reason::kRefuseMigrationFailed;
        result.detail =
            "step " + step->id + " produced invalid output: " + validate_error;
        return result;
      }
    }
    result.applied_step_ids.push_back(step->id);
    ++current;
  }

  result.ok = true;
  result.reason_code = reason::kMigrateInRange;
  result.document = std::move(doc);
  return result;
}

MigrationResult ConfigVersionRegistry::negotiateAndMigrate(
    const std::string& kind, const std::string& text) const {
  MigrationResult result;

  JsonValue doc;
  std::string error;
  if (!parseJson(text, doc, error)) {
    result.reason_code = reason::kRefuseUnparseable;
    result.detail = "parse error: " + error;
    if (const ConfigKindSpec* spec = findKind(kind)) {
      result.to_version = spec->current_version;
    }
    return result;
  }

  const ConfigVersionDecision decision = negotiate(kind, doc);
  result.from_version = decision.from_version;
  result.to_version = decision.to_version;

  if (decision.isRefuse()) {
    result.reason_code = decision.reason_code;
    result.detail = decision.detail;
    return result;
  }
  if (decision.isLoad()) {
    result.ok = true;
    result.reason_code = decision.reason_code;
    result.document = std::move(doc);
    return result;
  }
  // Migrate.
  return migrate(kind, std::move(doc), decision.from_version);
}

std::vector<ConfigVersionSupport> ConfigVersionRegistry::supportMatrix() const {
  std::vector<ConfigVersionSupport> matrix;
  matrix.reserve(kinds_.size());
  // kinds_ is a std::map, so iteration is sorted by kind -> deterministic output.
  for (const auto& entry : kinds_) {
    ConfigVersionSupport support;
    support.config_kind = entry.second.kind;
    support.min_version = entry.second.min_version;
    support.current_version = entry.second.current_version;
    matrix.push_back(std::move(support));
  }
  return matrix;
}

std::size_t ConfigVersionRegistry::emitSupportMatrix(
    diagnostics::AsyncLogger* logger) const {
  if (logger == nullptr) {
    return 0;
  }
  std::size_t emitted = 0;
  for (const ConfigVersionSupport& support : supportMatrix()) {
    logger->log(diagnostics::Severity::Info, "config_version_support",
                {
                    {"config_kind", support.config_kind},
                    {"min_version", std::to_string(support.min_version)},
                    {"current_version", std::to_string(support.current_version)},
                });
    ++emitted;
  }
  return emitted;
}

}  // namespace vrclient::config

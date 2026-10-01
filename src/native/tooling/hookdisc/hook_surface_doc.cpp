#include "tooling/hookdisc/hook_surface_doc.h"

#include "diagnostics/logging/diagnostic_logger.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace vrclient::tooling::hookdisc {
namespace {

// ---------------------------------------------------------------------------
// Minimal, self-contained JSON reader.
//
// The repo deliberately avoids a JSON library (game_fingerprint.cpp parses with
// regex). This is offline tooling, not a hot path, so a small recursive-descent
// reader is the least-surprising way to round-trip the hook-surface document
// without adding a third-party dependency.
// ---------------------------------------------------------------------------
struct JsonValue {
  enum class Type { Null, Bool, Number, String, Array, Object };
  Type type = Type::Null;
  bool boolean = false;
  double number = 0.0;
  bool number_is_integer = false;
  std::int64_t integer = 0;
  std::string text;
  std::vector<JsonValue> elements;
  std::vector<std::pair<std::string, JsonValue>> members;

  const JsonValue* find(const std::string& key) const {
    for (const auto& member : members) {
      if (member.first == key) {
        return &member.second;
      }
    }
    return nullptr;
  }
};

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

  // RAII guard bounding nesting depth. Every value (including each object member
  // value and array element) flows through parseValue, so guarding it here caps
  // total recursion and prevents a stack overflow on adversarial deeply-nested
  // input (loadHookSurfaceDoc accepts arbitrary user-supplied paths).
  struct DepthGuard {
    int& depth;
    bool ok;
    DepthGuard(int& d, int max_depth) : depth(d), ok(++d <= max_depth) {}
    ~DepthGuard() { --depth; }
  };

  bool parseValue(JsonValue& out) {
    DepthGuard guard(depth_, kMaxDepth);
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
      case '"':
        out.type = JsonValue::Type::String;
        return parseString(out.text);
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
    out.type = JsonValue::Type::Object;
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
      out.members.emplace_back(std::move(key), std::move(value));
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
    out.type = JsonValue::Type::Array;
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
      out.elements.push_back(std::move(value));
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
            // Minimal UTF-8 encode of the BMP code point. The hook-surface
            // documents are ASCII in practice; this keeps parsing total.
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
      out.type = JsonValue::Type::Bool;
      out.boolean = true;
      pos_ += 4;
      return true;
    }
    if (text_.compare(pos_, 5, "false") == 0) {
      out.type = JsonValue::Type::Bool;
      out.boolean = false;
      pos_ += 5;
      return true;
    }
    return fail("invalid literal");
  }

  bool parseNull(JsonValue& out) {
    if (text_.compare(pos_, 4, "null") == 0) {
      out.type = JsonValue::Type::Null;
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
    out.type = JsonValue::Type::Number;
    out.number_is_integer = is_integer;
    try {
      out.number = std::stod(token);
      if (is_integer) {
        out.integer = std::stoll(token);
      }
    } catch (const std::exception&) {
      return fail("number out of range");
    }
    return true;
  }

  static constexpr int kMaxDepth = 64;

  const std::string& text_;
  std::size_t pos_ = 0;
  std::string error_;
  int depth_ = 0;
};

const std::regex& timestampRegex() {
  static const std::regex re(R"(^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$)");
  return re;
}

const std::regex& offsetRegex() {
  static const std::regex re(R"(^0x[0-9A-Fa-f]+$)");
  return re;
}

bool isTimestamp(const std::string& value) {
  return std::regex_match(value, timestampRegex());
}

bool isOffset(const std::string& value) {
  return std::regex_match(value, offsetRegex());
}

std::string formatNumber(double value) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.6g", value);
  return std::string(buffer);
}

void appendQuoted(std::string& out, const std::string& value) {
  out.push_back('"');
  out += diagnostics::jsonEscape(value);
  out.push_back('"');
}

// Validation of a single evidence object, shared by document validation and
// promoteHookToValidated. Empty return == valid; otherwise an error message.
std::string evidenceError(const HookValidationEvidence& evidence) {
  if (!evidence.has_cadence_hz && !evidence.has_per_frame) {
    return "validation requires observed cadence_hz or a per_frame flag";
  }
  if (evidence.has_cadence_hz && evidence.cadence_hz <= 0.0) {
    return "validation cadence_hz must be positive";
  }
  if (evidence.hit_count < 1) {
    return "validation requires hit_count >= 1";
  }
  if (evidence.captured_at.empty() || !isTimestamp(evidence.captured_at)) {
    return "validation requires a captured_at timestamp (YYYY-MM-DDThh:mm:ssZ)";
  }
  return std::string();
}

}  // namespace

const char* hookAreaName(HookArea area) {
  switch (area) {
    case HookArea::Camera: return "camera";
    case HookArea::Projection: return "projection";
    case HookArea::Hud: return "hud";
    case HookArea::Input: return "input";
    case HookArea::Interaction: return "interaction";
    case HookArea::ModeState: return "mode_state";
    case HookArea::NetworkBlockMarker: return "network_block_marker";
  }
  return "camera";
}

const char* hookStatusName(HookStatus status) {
  switch (status) {
    case HookStatus::Candidate: return "candidate";
    case HookStatus::Validated: return "validated";
    case HookStatus::Blocked: return "blocked";
  }
  return "candidate";
}

const char* hookConfidenceName(HookConfidence confidence) {
  switch (confidence) {
    case HookConfidence::Low: return "low";
    case HookConfidence::Medium: return "medium";
    case HookConfidence::High: return "high";
  }
  return "low";
}

const char* validationMethodName(ValidationMethod method) {
  switch (method) {
    case ValidationMethod::HardwareBreakpoint: return "hardware_breakpoint";
    case ValidationMethod::VtableObservation: return "vtable_observation";
    case ValidationMethod::PassThroughDetour: return "pass_through_detour";
  }
  return "hardware_breakpoint";
}

const char* threadContextName(ThreadContext context) {
  switch (context) {
    case ThreadContext::Render: return "render";
    case ThreadContext::Main: return "main";
    case ThreadContext::Worker: return "worker";
    case ThreadContext::Unknown: return "unknown";
  }
  return "unknown";
}

bool parseHookArea(const std::string& value, HookArea& out) {
  if (value == "camera") { out = HookArea::Camera; return true; }
  if (value == "projection") { out = HookArea::Projection; return true; }
  if (value == "hud") { out = HookArea::Hud; return true; }
  if (value == "input") { out = HookArea::Input; return true; }
  if (value == "interaction") { out = HookArea::Interaction; return true; }
  if (value == "mode_state") { out = HookArea::ModeState; return true; }
  if (value == "network_block_marker") { out = HookArea::NetworkBlockMarker; return true; }
  return false;
}

bool parseHookStatus(const std::string& value, HookStatus& out) {
  if (value == "candidate") { out = HookStatus::Candidate; return true; }
  if (value == "validated") { out = HookStatus::Validated; return true; }
  if (value == "blocked") { out = HookStatus::Blocked; return true; }
  return false;
}

bool parseHookConfidence(const std::string& value, HookConfidence& out) {
  if (value == "low") { out = HookConfidence::Low; return true; }
  if (value == "medium") { out = HookConfidence::Medium; return true; }
  if (value == "high") { out = HookConfidence::High; return true; }
  return false;
}

bool parseValidationMethod(const std::string& value, ValidationMethod& out) {
  if (value == "hardware_breakpoint") { out = ValidationMethod::HardwareBreakpoint; return true; }
  if (value == "vtable_observation") { out = ValidationMethod::VtableObservation; return true; }
  if (value == "pass_through_detour") { out = ValidationMethod::PassThroughDetour; return true; }
  return false;
}

bool parseThreadContext(const std::string& value, ThreadContext& out) {
  if (value == "render") { out = ThreadContext::Render; return true; }
  if (value == "main") { out = ThreadContext::Main; return true; }
  if (value == "worker") { out = ThreadContext::Worker; return true; }
  if (value == "unknown") { out = ThreadContext::Unknown; return true; }
  return false;
}

HookSurfaceValidation validateHookSurfaceDoc(const HookSurfaceDoc& doc) {
  HookSurfaceValidation result;
  if (doc.version != 1) {
    result.message = "hook-surface version must be 1";
    return result;
  }
  if (doc.game_id.empty()) {
    result.message = "game_id must be non-empty";
    return result;
  }
  if (doc.build_id.empty()) {
    result.message = "build_id must be non-empty";
    return result;
  }
  if (doc.generated_by.empty()) {
    result.message = "generated_by must be non-empty";
    return result;
  }
  if (!isTimestamp(doc.generated_at)) {
    result.message = "generated_at must match YYYY-MM-DDThh:mm:ssZ";
    return result;
  }
  if (doc.hooks.empty()) {
    result.message = "hooks must not be empty";
    return result;
  }

  std::set<std::string> seen_names;
  for (const auto& hook : doc.hooks) {
    const std::string label = hook.name.empty() ? "<unnamed>" : hook.name;
    if (hook.name.empty()) {
      result.message = "hook needs a non-empty name";
      return result;
    }
    if (!seen_names.insert(hook.name).second) {
      result.message = "hook names must be unique: " + hook.name;
      return result;
    }
    if (hook.module.empty()) {
      result.message = label + ": hook needs a non-empty module";
      return result;
    }
    if (hook.symbol.empty() && hook.signature.empty() && hook.offset.empty()) {
      result.message = label + ": hook needs at least one of symbol, signature, or offset";
      return result;
    }
    if (!hook.offset.empty() && !isOffset(hook.offset)) {
      result.message = label + ": offset must match ^0x[0-9A-Fa-f]+$";
      return result;
    }
    if (hook.status == HookStatus::Validated) {
      if (!hook.has_validation) {
        result.message = label + ": status=validated requires a validation evidence object";
        return result;
      }
      const std::string err = evidenceError(hook.validation);
      if (!err.empty()) {
        result.message = label + ": " + err;
        return result;
      }
    }
    if (hook.status == HookStatus::Blocked && hook.blocked_reason.empty()) {
      result.message = label + ": status=blocked requires a non-empty blocked_reason";
      return result;
    }
  }

  result.ok = true;
  return result;
}

HookSurfaceLoadResult loadHookSurfaceDoc(const std::filesystem::path& path) {
  HookSurfaceLoadResult result;

  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    result.message = "unable to open hook-surface document: " + path.string();
    return result;
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  const std::string text = buffer.str();

  JsonValue root;
  std::string error;
  JsonReader reader(text);
  if (!reader.parse(root, error)) {
    result.message = "JSON parse error: " + error;
    return result;
  }
  if (root.type != JsonValue::Type::Object) {
    result.message = "hook-surface document must be a JSON object";
    return result;
  }

  HookSurfaceDoc doc;

  if (const JsonValue* v = root.find("version")) {
    if (v->type != JsonValue::Type::Number || !v->number_is_integer) {
      result.message = "version must be an integer";
      return result;
    }
    doc.version = static_cast<int>(v->integer);
  } else {
    result.message = "missing required field: version";
    return result;
  }

  auto readString = [&](const char* key, std::string& out) -> bool {
    const JsonValue* v = root.find(key);
    if (!v || v->type != JsonValue::Type::String) {
      result.message = std::string("missing or non-string field: ") + key;
      return false;
    }
    out = v->text;
    return true;
  };

  if (!readString("game_id", doc.game_id)) return result;
  if (!readString("build_id", doc.build_id)) return result;
  if (!readString("generated_by", doc.generated_by)) return result;
  if (!readString("generated_at", doc.generated_at)) return result;

  const JsonValue* hooks = root.find("hooks");
  if (!hooks || hooks->type != JsonValue::Type::Array) {
    result.message = "missing or non-array field: hooks";
    return result;
  }

  for (const JsonValue& item : hooks->elements) {
    if (item.type != JsonValue::Type::Object) {
      result.message = "each hook must be a JSON object";
      return result;
    }
    HookEntry hook;

    auto hookString = [&](const char* key, std::string& out, bool required) -> bool {
      const JsonValue* v = item.find(key);
      if (!v) {
        if (required) {
          result.message = std::string("hook missing required field: ") + key;
          return false;
        }
        return true;
      }
      if (v->type != JsonValue::Type::String) {
        result.message = std::string("hook field must be a string: ") + key;
        return false;
      }
      out = v->text;
      return true;
    };

    std::string area_text;
    std::string status_text;
    std::string confidence_text;
    if (!hookString("name", hook.name, true)) return result;
    if (!hookString("area", area_text, true)) return result;
    if (!hookString("module", hook.module, true)) return result;
    if (!hookString("status", status_text, true)) return result;
    if (!hookString("confidence", confidence_text, true)) return result;
    if (!hookString("symbol", hook.symbol, false)) return result;
    if (!hookString("signature", hook.signature, false)) return result;
    if (!hookString("offset", hook.offset, false)) return result;
    if (!hookString("blocked_reason", hook.blocked_reason, false)) return result;

    if (!parseHookArea(area_text, hook.area)) {
      result.message = "invalid area: " + area_text;
      return result;
    }
    if (!parseHookStatus(status_text, hook.status)) {
      result.message = "invalid status: " + status_text;
      return result;
    }
    if (!parseHookConfidence(confidence_text, hook.confidence)) {
      result.message = "invalid confidence: " + confidence_text;
      return result;
    }

    if (const JsonValue* validation = item.find("validation")) {
      if (validation->type != JsonValue::Type::Object) {
        result.message = "validation must be a JSON object";
        return result;
      }
      hook.has_validation = true;
      HookValidationEvidence& ev = hook.validation;

      const JsonValue* method = validation->find("method");
      std::string method_text;
      if (method && method->type == JsonValue::Type::String) {
        method_text = method->text;
      }
      if (!parseValidationMethod(method_text, ev.method)) {
        result.message = "invalid or missing validation.method";
        return result;
      }
      const JsonValue* thread = validation->find("thread_context");
      std::string thread_text;
      if (thread && thread->type == JsonValue::Type::String) {
        thread_text = thread->text;
      }
      if (!parseThreadContext(thread_text, ev.thread_context)) {
        result.message = "invalid or missing validation.thread_context";
        return result;
      }
      const JsonValue* hit = validation->find("hit_count");
      if (!hit || hit->type != JsonValue::Type::Number || !hit->number_is_integer) {
        result.message = "validation.hit_count must be an integer";
        return result;
      }
      ev.hit_count = hit->integer;
      const JsonValue* captured = validation->find("captured_at");
      if (!captured || captured->type != JsonValue::Type::String) {
        result.message = "validation.captured_at must be a string";
        return result;
      }
      ev.captured_at = captured->text;
      if (const JsonValue* cadence = validation->find("cadence_hz")) {
        if (cadence->type != JsonValue::Type::Number) {
          result.message = "validation.cadence_hz must be a number";
          return result;
        }
        ev.has_cadence_hz = true;
        ev.cadence_hz = cadence->number;
      }
      if (const JsonValue* per_frame = validation->find("per_frame")) {
        if (per_frame->type != JsonValue::Type::Bool) {
          result.message = "validation.per_frame must be a boolean";
          return result;
        }
        ev.has_per_frame = true;
        ev.per_frame = per_frame->boolean;
      }
    }

    doc.hooks.push_back(std::move(hook));
  }

  const HookSurfaceValidation validation = validateHookSurfaceDoc(doc);
  if (!validation.ok) {
    result.message = validation.message;
    return result;
  }

  result.loaded = true;
  result.doc = std::move(doc);
  return result;
}

std::string serializeHookSurfaceDoc(const HookSurfaceDoc& doc) {
  std::string out;
  out += "{\n";
  out += "  \"version\": " + std::to_string(doc.version) + ",\n";
  out += "  \"game_id\": "; appendQuoted(out, doc.game_id); out += ",\n";
  out += "  \"build_id\": "; appendQuoted(out, doc.build_id); out += ",\n";
  out += "  \"generated_by\": "; appendQuoted(out, doc.generated_by); out += ",\n";
  out += "  \"generated_at\": "; appendQuoted(out, doc.generated_at); out += ",\n";
  out += "  \"hooks\": [\n";

  for (std::size_t i = 0; i < doc.hooks.size(); ++i) {
    const HookEntry& hook = doc.hooks[i];
    out += "    {\n";
    out += "      \"name\": "; appendQuoted(out, hook.name); out += ",\n";
    out += "      \"area\": "; appendQuoted(out, hookAreaName(hook.area)); out += ",\n";
    out += "      \"module\": "; appendQuoted(out, hook.module); out += ",\n";
    if (!hook.symbol.empty()) {
      out += "      \"symbol\": "; appendQuoted(out, hook.symbol); out += ",\n";
    }
    if (!hook.signature.empty()) {
      out += "      \"signature\": "; appendQuoted(out, hook.signature); out += ",\n";
    }
    if (!hook.offset.empty()) {
      out += "      \"offset\": "; appendQuoted(out, hook.offset); out += ",\n";
    }
    out += "      \"status\": "; appendQuoted(out, hookStatusName(hook.status)); out += ",\n";
    out += "      \"confidence\": "; appendQuoted(out, hookConfidenceName(hook.confidence));

    if (hook.status == HookStatus::Blocked && !hook.blocked_reason.empty()) {
      out += ",\n      \"blocked_reason\": ";
      appendQuoted(out, hook.blocked_reason);
    }

    if (hook.has_validation) {
      const HookValidationEvidence& ev = hook.validation;
      out += ",\n      \"validation\": {\n";
      out += "        \"method\": "; appendQuoted(out, validationMethodName(ev.method)); out += ",\n";
      if (ev.has_cadence_hz) {
        out += "        \"cadence_hz\": " + formatNumber(ev.cadence_hz) + ",\n";
      }
      if (ev.has_per_frame) {
        out += std::string("        \"per_frame\": ") + (ev.per_frame ? "true" : "false") + ",\n";
      }
      out += "        \"thread_context\": "; appendQuoted(out, threadContextName(ev.thread_context)); out += ",\n";
      out += "        \"hit_count\": " + std::to_string(ev.hit_count) + ",\n";
      out += "        \"captured_at\": "; appendQuoted(out, ev.captured_at); out += "\n";
      out += "      }";
    }

    out += "\n    }";
    out += (i + 1 < doc.hooks.size()) ? ",\n" : "\n";
  }

  out += "  ]\n";
  out += "}\n";
  return out;
}

bool saveHookSurfaceDoc(
    const HookSurfaceDoc& doc,
    const std::filesystem::path& path,
    std::string& error) {
  const HookSurfaceValidation validation = validateHookSurfaceDoc(doc);
  if (!validation.ok) {
    error = "refusing to save invalid hook-surface document: " + validation.message;
    return false;
  }
  std::error_code ec;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), ec);
  }
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    error = "unable to open for write: " + path.string();
    return false;
  }
  stream << serializeHookSurfaceDoc(doc);
  if (!stream.good()) {
    error = "write failure for: " + path.string();
    return false;
  }
  return true;
}

bool promoteHookToValidated(
    HookEntry& hook,
    const HookValidationEvidence& evidence,
    std::string& error) {
  const std::string err = evidenceError(evidence);
  if (!err.empty()) {
    error = "cannot promote '" + hook.name + "' to validated: " + err;
    return false;
  }
  hook.status = HookStatus::Validated;
  hook.has_validation = true;
  hook.validation = evidence;
  return true;
}

}  // namespace vrclient::tooling::hookdisc

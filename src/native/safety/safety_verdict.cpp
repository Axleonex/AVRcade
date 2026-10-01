#include "safety/safety_verdict.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <utility>

namespace vrclient::safety {
namespace {

// ---------------------------------------------------------------------------
// Minimal, self-contained JSON reader.
//
// The repo deliberately ships NO external JSON library (game_fingerprint uses a
// hand-rolled regex reader). The safety rule set has nested arrays-of-objects,
// which a flat regex reader cannot parse robustly, so this is a small recursive
// descent parser over the JSON subset we emit in default-rules.json: objects,
// arrays, strings, integers, booleans. It is intentionally strict and rejects
// malformed input (rejected -> rule set not loaded -> default-block).
// ---------------------------------------------------------------------------

struct JsonValue {
  enum class Type { Null, Bool, Int, String, Array, Object } type = Type::Null;
  bool bool_value = false;
  long long int_value = 0;
  std::string string_value;
  std::vector<JsonValue> array_value;
  std::vector<std::pair<std::string, JsonValue>> object_value;

  const JsonValue* find(const std::string& key) const {
    for (const auto& [k, v] : object_value) {
      if (k == key) {
        return &v;
      }
    }
    return nullptr;
  }
};

class JsonParser {
 public:
  explicit JsonParser(std::string_view text) : text_(text) {}

  bool parse(JsonValue& out) {
    skipWhitespace();
    if (!parseValue(out)) {
      return false;
    }
    skipWhitespace();
    return pos_ == text_.size();
  }

 private:
  void skipWhitespace() {
    while (pos_ < text_.size()) {
      const char ch = text_[pos_];
      if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  bool parseValue(JsonValue& out) {
    skipWhitespace();
    if (pos_ >= text_.size()) {
      return false;
    }
    const char ch = text_[pos_];
    switch (ch) {
      case '{':
        return parseObject(out);
      case '[':
        return parseArray(out);
      case '"':
        return parseString(out);
      case 't':
      case 'f':
        return parseBool(out);
      case 'n':
        return parseNull(out);
      default:
        if (ch == '-' || (ch >= '0' && ch <= '9')) {
          return parseNumber(out);
        }
        return false;
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
      JsonValue key;
      if (!parseString(key)) {
        return false;
      }
      skipWhitespace();
      if (pos_ >= text_.size() || text_[pos_] != ':') {
        return false;
      }
      ++pos_;  // consume ':'
      JsonValue value;
      if (!parseValue(value)) {
        return false;
      }
      out.object_value.emplace_back(key.string_value, std::move(value));
      skipWhitespace();
      if (pos_ >= text_.size()) {
        return false;
      }
      if (text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (text_[pos_] == '}') {
        ++pos_;
        return true;
      }
      return false;
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
      out.array_value.push_back(std::move(value));
      skipWhitespace();
      if (pos_ >= text_.size()) {
        return false;
      }
      if (text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (text_[pos_] == ']') {
        ++pos_;
        return true;
      }
      return false;
    }
  }

  bool parseString(JsonValue& out) {
    if (pos_ >= text_.size() || text_[pos_] != '"') {
      return false;
    }
    ++pos_;  // consume opening quote
    std::string result;
    while (pos_ < text_.size()) {
      const char ch = text_[pos_++];
      if (ch == '"') {
        out.type = JsonValue::Type::String;
        out.string_value = std::move(result);
        return true;
      }
      if (ch == '\\') {
        if (pos_ >= text_.size()) {
          return false;
        }
        const char esc = text_[pos_++];
        switch (esc) {
          case '"': result.push_back('"'); break;
          case '\\': result.push_back('\\'); break;
          case '/': result.push_back('/'); break;
          case 'n': result.push_back('\n'); break;
          case 't': result.push_back('\t'); break;
          case 'r': result.push_back('\r'); break;
          case 'b': result.push_back('\b'); break;
          case 'f': result.push_back('\f'); break;
          default: return false;  // we do not emit \uXXXX in our config
        }
      } else {
        result.push_back(ch);
      }
    }
    return false;  // unterminated string
  }

  bool parseBool(JsonValue& out) {
    if (text_.compare(pos_, 4, "true") == 0) {
      pos_ += 4;
      out.type = JsonValue::Type::Bool;
      out.bool_value = true;
      return true;
    }
    if (text_.compare(pos_, 5, "false") == 0) {
      pos_ += 5;
      out.type = JsonValue::Type::Bool;
      out.bool_value = false;
      return true;
    }
    return false;
  }

  bool parseNull(JsonValue& out) {
    if (text_.compare(pos_, 4, "null") == 0) {
      pos_ += 4;
      out.type = JsonValue::Type::Null;
      return true;
    }
    return false;
  }

  bool parseNumber(JsonValue& out) {
    const std::size_t start = pos_;
    if (pos_ < text_.size() && text_[pos_] == '-') {
      ++pos_;
    }
    bool any_digits = false;
    while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
      ++pos_;
      any_digits = true;
    }
    if (!any_digits) {
      return false;
    }
    // We only consume integers in the safety config; reject fractions/exponents
    // so an unexpected numeric form fails loudly into default-block.
    if (pos_ < text_.size() && (text_[pos_] == '.' || text_[pos_] == 'e' ||
                                text_[pos_] == 'E')) {
      return false;
    }
    out.type = JsonValue::Type::Int;
    out.int_value = std::stoll(std::string(text_.substr(start, pos_ - start)));
    return true;
  }

  std::string_view text_;
  std::size_t pos_ = 0;
};

std::string readText(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::in | std::ios::binary);
  if (!input) {
    return {};
  }
  return std::string(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
}

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

std::vector<std::string> readStringArray(const JsonValue* value) {
  std::vector<std::string> result;
  if (value == nullptr || value->type != JsonValue::Type::Array) {
    return result;
  }
  for (const JsonValue& item : value->array_value) {
    if (item.type == JsonValue::Type::String) {
      result.push_back(item.string_value);
    }
  }
  return result;
}

bool readBoolField(const JsonValue* object, const std::string& key, bool fallback) {
  const JsonValue* field = object->find(key);
  if (field != nullptr && field->type == JsonValue::Type::Bool) {
    return field->bool_value;
  }
  return fallback;
}

std::string readStringField(
    const JsonValue* object, const std::string& key, const std::string& fallback) {
  const JsonValue* field = object->find(key);
  if (field != nullptr && field->type == JsonValue::Type::String) {
    return field->string_value;
  }
  return fallback;
}

// ---------------------------------------------------------------------------
// Verdict construction helpers.
// ---------------------------------------------------------------------------

const char* targetScope(bool controlled_smoke_target) {
  return controlled_smoke_target ? "controlled_smoke" : "selected_game";
}

SafetyVerdictResult makeResult(
    SafetyVerdict verdict,
    std::string reason_code,
    std::string message_key,
    std::string message,
    const SafetyEvaluationRequest& request) {
  SafetyVerdictResult result;
  result.verdict = verdict;
  result.reason_code = std::move(reason_code);
  result.message_key = std::move(message_key);
  result.message = std::move(message);
  result.diagnostics_fields = {
      {"safety_verdict", safetyVerdictName(result.verdict)},
      {"reason_code", result.reason_code},
      {"message_key", result.message_key},
      {"game_id", request.identity.game_id},
      {"build_id", request.identity.build_id},
      {"adapter_id", request.adapter_id},
      {"requested_launch_mode", request.requested_launch_mode},
      {"modding_posture", moddingPostureName(request.detection.modding_posture)},
      {"rule_set_version", request.expected_rule_set_version},
  };
  return result;
}

void logVerdict(
    const SafetyVerdictResult& result, diagnostics::AsyncLogger* logger) {
  if (logger == nullptr) {
    return;
  }
  logger->log(
      result.verdict == SafetyVerdict::Allow ? diagnostics::Severity::Info
      : result.verdict == SafetyVerdict::Warn ? diagnostics::Severity::Warning
                                              : diagnostics::Severity::Warning,
      "safety_verdict",
      result.diagnostics_fields);
}

SafetyVerdictResult finish(
    SafetyVerdictResult result, diagnostics::AsyncLogger* logger) {
  logVerdict(result, logger);
  return result;
}

bool listContains(const std::vector<std::string>& list, const std::string& value) {
  const std::string needle = lower(value);
  return std::any_of(list.begin(), list.end(), [&needle](const std::string& item) {
    return lower(item) == needle;
  });
}

bool postureAcceptable(
    const std::vector<ModdingPosture>& acceptable, ModdingPosture posture) {
  if (acceptable.empty()) {
    // Least-permissive default: an EMPTY list means NO posture is acceptable, so
    // the favorable-posture branch never fires and the verdict can rise no higher
    // than Warn. The schema pins minItems>=1 so a shipped rule must list its
    // acceptable postures explicitly; this keeps the permissive interpretation a
    // deliberate, reviewed data choice rather than an accidental fallback.
    return false;
  }
  return std::find(acceptable.begin(), acceptable.end(), posture) !=
      acceptable.end();
}

SafetyVerdictResult makeArtifactTrustResult(
    const SafetyEvaluationRequest& request,
    const SafetyEvaluationRequest::ArtifactTrustObservation& artifact) {
  using ArtifactTrustState = SafetyEvaluationRequest::ArtifactTrustState;
  SafetyVerdict verdict = SafetyVerdict::Block;
  std::string reason_code = artifact.reason_code;
  std::string message_key;
  std::string message;

  switch (artifact.state) {
    case ArtifactTrustState::Trusted:
      return makeResult(
          SafetyVerdict::Allow, "approved", "safety.allow.approved",
          "artifact is trusted", request);
    case ArtifactTrustState::NotEvaluated:
      verdict = SafetyVerdict::UnknownBlocked;
      reason_code = reason_code.empty()
          ? "artifact_trust_not_evaluated"
          : reason_code;
      message_key = "safety.unknown.supply_chain.not_evaluated";
      message = "artifact trust was not evaluated; refusing by default";
      break;
    case ArtifactTrustState::Unsigned:
      reason_code = reason_code.empty() ? "artifact_unsigned" : reason_code;
      message_key = "safety.block.supply_chain.unsigned_artifact";
      message = "artifact is unsigned; refusing";
      break;
    case ArtifactTrustState::HashMismatch:
      reason_code = reason_code.empty() ? "artifact_hash_mismatch" : reason_code;
      message_key = "safety.block.supply_chain.hash_mismatch";
      message = "artifact hash does not match its signed manifest; refusing";
      break;
    case ArtifactTrustState::SignatureInvalid:
      reason_code =
          reason_code.empty() ? "artifact_signature_invalid" : reason_code;
      message_key = "safety.block.supply_chain.signature_invalid";
      message = "artifact signature failed trust-root verification; refusing";
      break;
    case ArtifactTrustState::UntrustedKey:
      reason_code = reason_code.empty() ? "artifact_untrusted_key" : reason_code;
      message_key = "safety.block.supply_chain.untrusted_key";
      message = "artifact signature does not chain to a trusted key; refusing";
      break;
    case ArtifactTrustState::Revoked:
      reason_code = reason_code.empty() ? "artifact_revoked" : reason_code;
      message_key = "safety.block.supply_chain.revoked_artifact";
      message = "artifact or signing key is revoked; refusing";
      break;
    case ArtifactTrustState::UnsupportedAlgorithm:
      reason_code =
          reason_code.empty() ? "artifact_signature_algorithm_unsupported"
                              : reason_code;
      message_key = "safety.block.supply_chain.unsupported_algorithm";
      message = "artifact signature algorithm is unsupported; refusing";
      break;
    case ArtifactTrustState::UnreadableArtifact:
      reason_code = reason_code.empty() ? "artifact_unreadable" : reason_code;
      message_key = "safety.block.supply_chain.unreadable_artifact";
      message = "artifact could not be read for trust verification; refusing";
      break;
    case ArtifactTrustState::InvalidConfig:
      reason_code = reason_code.empty() ? "artifact_invalid_config" : reason_code;
      message_key = "safety.block.supply_chain.invalid_config";
      message = "artifact signing metadata is invalid; refusing";
      break;
  }

  SafetyVerdictResult result =
      makeResult(verdict, reason_code, message_key, message, request);
  result.diagnostics_fields.push_back({"artifact_id", artifact.artifact_id});
  result.diagnostics_fields.push_back({"artifact_type", artifact.artifact_type});
  result.diagnostics_fields.push_back(
      {"artifact_trust_state", artifactTrustStateName(artifact.state)});
  return result;
}

}  // namespace

const char* safetyVerdictName(SafetyVerdict verdict) {
  switch (verdict) {
    case SafetyVerdict::Allow:
      return "allow";
    case SafetyVerdict::Warn:
      return "warn";
    case SafetyVerdict::Block:
      return "block";
    case SafetyVerdict::UnknownBlocked:
      return "unknown-blocked";
  }
  return "unknown-blocked";
}

bool isRefusal(SafetyVerdict verdict) {
  return verdict == SafetyVerdict::Block || verdict == SafetyVerdict::UnknownBlocked;
}

const char* artifactTrustStateName(
    SafetyEvaluationRequest::ArtifactTrustState state) {
  using ArtifactTrustState = SafetyEvaluationRequest::ArtifactTrustState;
  switch (state) {
    case ArtifactTrustState::NotEvaluated:
      return "not-evaluated";
    case ArtifactTrustState::Trusted:
      return "trusted";
    case ArtifactTrustState::Unsigned:
      return "unsigned";
    case ArtifactTrustState::HashMismatch:
      return "hash-mismatch";
    case ArtifactTrustState::SignatureInvalid:
      return "signature-invalid";
    case ArtifactTrustState::UntrustedKey:
      return "untrusted-key";
    case ArtifactTrustState::Revoked:
      return "revoked";
    case ArtifactTrustState::UnsupportedAlgorithm:
      return "unsupported-algorithm";
    case ArtifactTrustState::UnreadableArtifact:
      return "unreadable-artifact";
    case ArtifactTrustState::InvalidConfig:
      return "invalid-config";
  }
  return "not-evaluated";
}

const SafetyRuleEntry* SafetyRuleSet::findRule(const std::string& game_id) const {
  for (const SafetyRuleEntry& entry : rules) {
    if (entry.game_id == game_id) {
      return &entry;
    }
  }
  return nullptr;
}

SafetyRuleLoadResult loadSafetyRuleSet(const std::filesystem::path& path) {
  SafetyRuleLoadResult result;
  const std::string text = readText(path);
  if (text.empty()) {
    result.message = "safety rule set is missing or empty";
    return result;
  }

  JsonValue root;
  JsonParser parser(text);
  if (!parser.parse(root) || root.type != JsonValue::Type::Object) {
    result.message = "safety rule set is not valid JSON";
    return result;
  }

  const JsonValue* version = root.find("version");
  if (version == nullptr || version->type != JsonValue::Type::Int ||
      version->int_value < 1) {
    result.message = "safety rule set version is missing or invalid";
    return result;
  }

  SafetyRuleSet rule_set;
  rule_set.version = static_cast<int>(version->int_value);
  rule_set.rule_set_version =
      readStringField(&root, "rule_set_version", "");
  if (rule_set.rule_set_version.empty()) {
    result.message = "safety rule set is missing rule_set_version";
    return result;
  }

  const JsonValue* indicators = root.find("anti_cheat_indicators");
  const JsonValue* online_flags = root.find("online_only_flags");
  const JsonValue* drm = root.find("storefront_drm_indicators");
  rule_set.indicators.known_anti_cheat_indicators = readStringArray(indicators);
  rule_set.indicators.online_only_flags = readStringArray(online_flags);
  rule_set.indicators.storefront_drm_indicators = readStringArray(drm);

  const JsonValue* rules = root.find("rules");
  if (rules == nullptr || rules->type != JsonValue::Type::Array ||
      rules->array_value.empty()) {
    result.message = "safety rule set has no rules";
    return result;
  }
  for (const JsonValue& rule_value : rules->array_value) {
    if (rule_value.type != JsonValue::Type::Object) {
      result.message = "safety rule entry is not an object";
      return result;
    }
    SafetyRuleEntry entry;
    entry.game_id = readStringField(&rule_value, "game_id", "");
    if (entry.game_id.empty()) {
      result.message = "safety rule entry is missing game_id";
      return result;
    }
    entry.controlled_smoke_target =
        readBoolField(&rule_value, "controlled_smoke_target", false);
    entry.allow_offline = readBoolField(&rule_value, "allow_offline", false);
    // Autonomy floor (mirrors the B2 hook-discovery safety_gate): only the
    // controlled smoke target may auto-approve without explicit human
    // authorization. A non-smoke entry that sets allow_offline:true would reach a
    // fully autonomous Allow through this engine while the B2 gate would refuse
    // it -- the two layers must state the SAME floor. Reject such an entry at load
    // time so the rule set fails to load (-> missing_rule_set -> default-block)
    // rather than silently disagreeing with the B2 autonomy invariant.
    if (entry.allow_offline && !entry.controlled_smoke_target) {
      result.message =
          "safety rule entry sets allow_offline without controlled_smoke_target "
          "(violates the autonomy floor: only the controlled smoke target may "
          "auto-approve)";
      return result;
    }
    entry.allow_warn_on_uncertain =
        readBoolField(&rule_value, "allow_warn_on_uncertain", false);
    entry.adapter_id = readStringField(&rule_value, "adapter_id", "");
    entry.allowed_launch_modes =
        readStringArray(rule_value.find("allowed_launch_modes"));
    for (const std::string& posture :
         readStringArray(rule_value.find("acceptable_modding_postures"))) {
      entry.acceptable_modding_postures.push_back(parseModdingPosture(posture));
    }
    rule_set.rules.push_back(std::move(entry));
  }

  result.loaded = true;
  result.message = "safety rule set loaded";
  result.rule_set = std::move(rule_set);
  return result;
}

SafetyVerdictResult evaluateSafetyVerdict(
    const SafetyEvaluationRequest& request,
    const SafetyRuleSet& rule_set,
    diagnostics::AsyncLogger* logger) {
  // 1. Missing / unloaded rule set -> default-block (SAFE-04).
  if (!rule_set.loaded()) {
    return finish(makeResult(
        SafetyVerdict::UnknownBlocked,
        "missing_rule_set",
        "safety.unknown.missing_rule_set",
        "no safety rule set is loaded; refusing by default",
        request), logger);
  }

  // 2. Identity must be KnownSupported. Unknown / ambiguous / unsupported all
  //    block (default-block on unknown identity / build).
  if (request.identity.status != versioning::DetectionStatus::KnownSupported) {
    const bool unknownish =
        request.identity.status == versioning::DetectionStatus::Unknown ||
        request.identity.status == versioning::DetectionStatus::Ambiguous;
    return finish(makeResult(
        unknownish ? SafetyVerdict::UnknownBlocked : SafetyVerdict::Block,
        unknownish ? "unknown_game_or_build" : "unsupported_identity",
        unknownish ? "safety.unknown.identity" : "safety.block.unsupported_identity",
        "target identity is not a known supported build",
        request), logger);
  }

  // 3. A per-game rule entry must exist (missing rule for a game == block).
  const SafetyRuleEntry* entry = rule_set.findRule(request.identity.game_id);
  if (entry == nullptr) {
    return finish(makeResult(
        SafetyVerdict::UnknownBlocked,
        "no_rule_for_game",
        "safety.unknown.no_rule_for_game",
        "no safety rule entry exists for this game; refusing by default",
        request), logger);
  }

  // 4. Requested launch mode must be permitted by the entry. An EMPTY allow-list
  //    means NO launch mode is permitted (least-permissive / default-block): an
  //    empty list blocks every launch mode rather than permitting all. The schema
  //    pins minItems>=1 so a shipped rule cannot silently turn this gate off, but
  //    the engine enforces the invariant defensively in case malformed data ever
  //    reaches the loader.
  if (entry->allowed_launch_modes.empty() ||
      !listContains(entry->allowed_launch_modes, request.requested_launch_mode)) {
    return finish(makeResult(
        SafetyVerdict::Block,
        "launch_mode_not_permitted",
        "safety.block.launch_mode_not_permitted",
        "requested launch mode is not permitted for this game",
        request), logger);
  }

  // B6 supply-chain trust input. Verification remains outside this hot path:
  // callers hand in data-only trust observations from the manifest/signature
  // verifier. Anything except Trusted fails closed before the preflight spine.
  for (const SafetyEvaluationRequest::ArtifactTrustObservation& artifact :
       request.artifact_trust) {
    if (artifact.state !=
        SafetyEvaluationRequest::ArtifactTrustState::Trusted) {
      return finish(makeArtifactTrustResult(request, artifact), logger);
    }
  }

  // 5. The Phase 3 preflight spine is the authoritative precondition: identity,
  //    source allowlist, anti-cheat risk, online risk, architecture, runtime
  //    hash. If it refuses, propagate its reason as a block. Reuse, do not
  //    re-derive.
  const injector::safety::SafetyPreflightResult preflight =
      injector::safety::runSafetyPreflight(request.preflight, logger);
  if (preflight.verdict != injector::safety::SafetyVerdict::Approved) {
    SafetyVerdictResult result = makeResult(
        SafetyVerdict::Block,
        preflight.reason_code,
        "safety.block.preflight." + preflight.reason_code,
        preflight.message,
        request);
    result.preflight_reason_code = preflight.reason_code;
    return finish(std::move(result), logger);
  }

  // 6. DEFENSIVE detection signals (T02). Any Risky dimension is a hard block.
  //    Any Uncertain dimension is unknown-blocked, unless the rule explicitly
  //    permits warn-on-uncertain (then it becomes a Warn requiring user ack).
  const DetectionSignals signals =
      classifyDetectionSignals(request.detection, rule_set.indicators);

  if (signals.anti_cheat == RiskSignal::Risky) {
    return finish(makeResult(
        SafetyVerdict::Block,
        "anti_cheat_detected",
        "safety.block.anti_cheat_detected",
        "a known anti-cheat indicator was observed; refusing (no bypass)",
        request), logger);
  }
  if (signals.online_mode == RiskSignal::Risky) {
    return finish(makeResult(
        SafetyVerdict::Block,
        "online_only_mode_detected",
        "safety.block.online_only_mode_detected",
        "a known online-only mode flag was observed; refusing",
        request), logger);
  }
  if (signals.storefront_drm == RiskSignal::Risky) {
    return finish(makeResult(
        SafetyVerdict::Block,
        "storefront_drm_detected",
        "safety.block.storefront_drm_detected",
        "a known storefront DRM indicator was observed; refusing",
        request), logger);
  }

  const bool any_uncertain =
      signals.anti_cheat == RiskSignal::Uncertain ||
      signals.online_mode == RiskSignal::Uncertain ||
      signals.storefront_drm == RiskSignal::Uncertain;
  if (any_uncertain && !entry->allow_warn_on_uncertain) {
    return finish(makeResult(
        SafetyVerdict::UnknownBlocked,
        "uncertain_detection_signal",
        "safety.unknown.uncertain_detection_signal",
        "detection signal is uncertain; refusing by default",
        request), logger);
  }

  // 7. Modding posture is a FAVORABLE signal only. An explicitly conflicting
  //    posture blocks. An unacceptable (but non-conflicting) posture downgrades
  //    to Warn; it can NEVER lift a block already decided above.
  if (request.detection.modding_posture ==
      ModdingPosture::ConflictingWithIntegrityPolicy) {
    return finish(makeResult(
        SafetyVerdict::Block,
        "modding_conflicts_with_integrity_policy",
        "safety.block.modding_conflicts_with_integrity_policy",
        "declared modding posture conflicts with integrity policy; refusing",
        request), logger);
  }

  const bool posture_ok =
      postureAcceptable(entry->acceptable_modding_postures,
                        request.detection.modding_posture);

  // 8. Decide the favorable terminal verdict. Allow requires the rule to
  //    explicitly permit it (controlled smoke target OR allow_offline), all
  //    detection signals Clear, and acceptable posture. Otherwise Warn (explicit
  //    user acknowledgement required downstream).
  const bool all_clear =
      signals.anti_cheat == RiskSignal::Clear &&
      signals.online_mode == RiskSignal::Clear &&
      signals.storefront_drm == RiskSignal::Clear;
  const bool rule_allows =
      entry->controlled_smoke_target || entry->allow_offline;

  if (rule_allows && all_clear && posture_ok) {
    return finish(makeResult(
        SafetyVerdict::Allow,
        "approved",
        "safety.allow.approved",
        std::string("target passed full safety evaluation (") +
            targetScope(entry->controlled_smoke_target) + ")",
        request), logger);
  }

  return finish(makeResult(
      SafetyVerdict::Warn,
      "acknowledgement_required",
      "safety.warn.acknowledgement_required",
      "target passed core checks but requires explicit user acknowledgement",
      request), logger);
}

}  // namespace vrclient::safety

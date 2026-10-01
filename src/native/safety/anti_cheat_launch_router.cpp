#include "safety/anti_cheat_launch_router.h"

#include "config/config_versioning.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string_view>
#include <utility>

namespace vrclient::safety {
namespace {

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

bool equalsInsensitive(const std::string& lhs, const std::string& rhs) {
  return lower(lhs) == lower(rhs);
}

std::string requireString(
    const config::JsonValue& object,
    const std::string& key,
    std::string& error) {
  const config::JsonValue* field = object.find(key);
  if (field == nullptr || !field->isString() || field->asString().empty()) {
    error = "missing or invalid string field: " + key;
    return {};
  }
  return field->asString();
}

bool requireBool(
    const config::JsonValue& object,
    const std::string& key,
    std::string& error) {
  const config::JsonValue* field = object.find(key);
  if (field == nullptr || !field->isBool()) {
    error = "missing or invalid bool field: " + key;
    return false;
  }
  return field->asBool();
}

bool oneOf(std::string_view value, std::initializer_list<std::string_view> allowed) {
  for (std::string_view item : allowed) {
    if (value == item) {
      return true;
    }
  }
  return false;
}

bool readReasonCodes(
    const config::JsonValue& variant,
    AntiCheatRouteReasonCodes& out,
    std::string& error) {
  const config::JsonValue* reasons = variant.find("reason_codes");
  if (reasons == nullptr || !reasons->isObject()) {
    error = "variant is missing reason_codes object";
    return false;
  }
  out.launch_only = requireString(*reasons, "launch_only", error);
  if (!error.empty()) {
    return false;
  }
  out.block = requireString(*reasons, "block", error);
  if (!error.empty()) {
    return false;
  }
  out.injection_disabled = requireString(*reasons, "injection_disabled", error);
  if (!error.empty()) {
    return false;
  }
  out.injector_capable = requireString(*reasons, "injector_capable", error);
  return error.empty();
}

bool validateVariant(const AntiCheatLaunchVariant& variant, std::string& error) {
  if (!oneOf(variant.launch_mode, {"injector", "launch_only", "blocked"})) {
    error = "variant " + variant.variant_id + " has invalid launch_mode";
    return false;
  }
  if (!oneOf(variant.anti_cheat_policy,
             {"launch-only-when-detected", "block-when-detected",
              "not-anti-cheat-target"})) {
    error = "variant " + variant.variant_id + " has invalid anti_cheat_policy";
    return false;
  }
  if (variant.launch_mode == "launch_only") {
    if (!variant.legitimate_non_injection_path || variant.injection_capable) {
      error = "launch-only variant " + variant.variant_id +
          " must be legitimate and non-injectable";
      return false;
    }
  }
  if (variant.launch_mode == "blocked") {
    if (variant.legitimate_non_injection_path || variant.injection_capable) {
      error = "blocked variant " + variant.variant_id +
          " must not be launchable or injectable";
      return false;
    }
  }
  if (variant.launch_mode == "injector" && !variant.injection_capable) {
    error = "injector variant " + variant.variant_id +
        " must explicitly be injection_capable";
    return false;
  }
  if (variant.anti_cheat_policy == "launch-only-when-detected" &&
      variant.launch_mode != "launch_only") {
    error = "AC launch-only policy must point at a launch_only variant";
    return false;
  }
  if (variant.anti_cheat_policy == "block-when-detected" &&
      variant.injection_capable) {
    error = "AC block policy must not be injection_capable";
    return false;
  }
  return true;
}

bool buildMatches(const AntiCheatLaunchVariant& variant, const std::string& build_id) {
  return variant.build_id == "*" ||
      equalsInsensitive(variant.build_id, build_id);
}

bool variantMatchesRequest(
    const AntiCheatLaunchVariant& variant,
    const AntiCheatLaunchRequest& request) {
  if (!equalsInsensitive(variant.game_id, request.game_id)) {
    return false;
  }
  if (!buildMatches(variant, request.build_id)) {
    return false;
  }
  if (!request.selected_variant_id.empty() &&
      !equalsInsensitive(variant.variant_id, request.selected_variant_id)) {
    return false;
  }
  return true;
}

using VariantPredicate = bool (*)(const AntiCheatLaunchVariant&);

const AntiCheatLaunchVariant* findBestVariant(
    const AntiCheatCompatibilitySet& set,
    const AntiCheatLaunchRequest& request,
    VariantPredicate predicate) {
  const AntiCheatLaunchVariant* wildcard = nullptr;
  for (const AntiCheatLaunchVariant& variant : set.variants) {
    if (!variantMatchesRequest(variant, request) || !predicate(variant)) {
      continue;
    }
    if (equalsInsensitive(variant.build_id, request.build_id)) {
      return &variant;
    }
    if (wildcard == nullptr && variant.build_id == "*") {
      wildcard = &variant;
    }
  }
  return wildcard;
}

bool isAcLaunchOnlyVariant(const AntiCheatLaunchVariant& variant) {
  return variant.anti_cheat_policy == "launch-only-when-detected" &&
      variant.launch_mode == "launch_only" &&
      variant.legitimate_non_injection_path &&
      !variant.injection_capable;
}

bool isBlockWhenDetectedVariant(const AntiCheatLaunchVariant& variant) {
  return variant.anti_cheat_policy == "block-when-detected" &&
      variant.launch_mode == "blocked" &&
      !variant.injection_capable;
}

bool isInjectorVariant(const AntiCheatLaunchVariant& variant) {
  return variant.launch_mode == "injector" && variant.injection_capable &&
      variant.anti_cheat_policy != "launch-only-when-detected";
}

bool isLaunchOnlyVariant(const AntiCheatLaunchVariant& variant) {
  return variant.launch_mode == "launch_only" &&
      variant.legitimate_non_injection_path &&
      !variant.injection_capable;
}

AntiCheatLaunchRoute makeRoute(
    AntiCheatRouteKind route,
    bool injection_allowed,
    std::string reason_code,
    std::string message_key,
    std::string message,
    const AntiCheatLaunchRequest& request,
    const DetectionSignals& signals,
    const AntiCheatLaunchVariant* variant = nullptr) {
  AntiCheatLaunchRoute result;
  result.route = route;
  result.injection_allowed = injection_allowed;
  result.reason_code = std::move(reason_code);
  result.message_key = std::move(message_key);
  result.message = std::move(message);
  result.detection_signals = signals;
  if (variant != nullptr) {
    result.variant_id = variant->variant_id;
    result.variant_type = variant->variant_type;
  }
  result.diagnostics_fields = {
      {"anti_cheat_route", antiCheatRouteKindName(result.route)},
      {"reason_code", result.reason_code},
      {"message_key", result.message_key},
      {"game_id", request.game_id},
      {"build_id", request.build_id},
      {"selected_variant_id", request.selected_variant_id},
      {"requested_launch_mode", requestedLaunchModeName(request.requested_mode)},
      {"injection_allowed", result.injection_allowed ? "true" : "false"},
      {"anti_cheat_signal", riskSignalName(signals.anti_cheat)},
      {"online_mode_signal", riskSignalName(signals.online_mode)},
      {"storefront_drm_signal", riskSignalName(signals.storefront_drm)},
  };
  if (!result.variant_id.empty()) {
    result.diagnostics_fields.push_back({"variant_id", result.variant_id});
    result.diagnostics_fields.push_back({"variant_type", result.variant_type});
  }
  return result;
}

}  // namespace

const char* antiCheatRouteKindName(AntiCheatRouteKind route) {
  switch (route) {
    case AntiCheatRouteKind::Blocked:
      return "blocked";
    case AntiCheatRouteKind::LaunchOnly:
      return "launch-only";
    case AntiCheatRouteKind::InjectorCapable:
      return "injector-capable";
  }
  return "blocked";
}

const char* requestedLaunchModeName(RequestedLaunchMode mode) {
  switch (mode) {
    case RequestedLaunchMode::Auto:
      return "auto";
    case RequestedLaunchMode::LaunchOnly:
      return "launch-only";
    case RequestedLaunchMode::Inject:
      return "inject";
  }
  return "auto";
}

AntiCheatCompatibilityLoadResult loadAntiCheatCompatibilitySet(
    const std::filesystem::path& path) {
  AntiCheatCompatibilityLoadResult result;
  const std::string text = readText(path);
  if (text.empty()) {
    result.message = "anti-cheat compatibility config is missing or empty";
    return result;
  }

  config::JsonValue root;
  std::string error;
  if (!config::parseJson(text, root, error) || !root.isObject()) {
    result.message = "anti-cheat compatibility config is not valid JSON: " + error;
    return result;
  }

  const config::JsonValue* version = root.find("version");
  if (version == nullptr || !version->isInt() || version->asInt() != 1) {
    result.message = "anti-cheat compatibility version must be 1";
    return result;
  }

  AntiCheatCompatibilitySet set;
  set.version = 1;
  set.compatibility_set_version =
      requireString(root, "compatibility_set_version", error);
  if (!error.empty()) {
    result.message = error;
    return result;
  }

  const config::JsonValue* variants = root.find("variants");
  if (variants == nullptr || !variants->isArray() ||
      variants->elements().empty()) {
    result.message = "anti-cheat compatibility config has no variants";
    return result;
  }

  for (const config::JsonValue& item : variants->elements()) {
    if (!item.isObject()) {
      result.message = "anti-cheat compatibility variant is not an object";
      return result;
    }

    AntiCheatLaunchVariant variant;
    variant.game_id = requireString(item, "game_id", error);
    if (!error.empty()) {
      result.message = error;
      return result;
    }
    variant.build_id = requireString(item, "build_id", error);
    if (!error.empty()) {
      result.message = error;
      return result;
    }
    variant.variant_id = requireString(item, "variant_id", error);
    if (!error.empty()) {
      result.message = error;
      return result;
    }
    variant.variant_type = requireString(item, "variant_type", error);
    if (!error.empty()) {
      result.message = error;
      return result;
    }
    variant.display_name = requireString(item, "display_name", error);
    if (!error.empty()) {
      result.message = error;
      return result;
    }
    variant.source = requireString(item, "source", error);
    if (!error.empty()) {
      result.message = error;
      return result;
    }
    variant.launch_mode = requireString(item, "launch_mode", error);
    if (!error.empty()) {
      result.message = error;
      return result;
    }
    variant.anti_cheat_policy = requireString(item, "anti_cheat_policy", error);
    if (!error.empty()) {
      result.message = error;
      return result;
    }
    variant.legitimate_non_injection_path =
        requireBool(item, "legitimate_non_injection_path", error);
    if (!error.empty()) {
      result.message = error;
      return result;
    }
    variant.injection_capable = requireBool(item, "injection_capable", error);
    if (!error.empty()) {
      result.message = error;
      return result;
    }
    variant.sanctioned_path_label =
        requireString(item, "sanctioned_path_label", error);
    if (!error.empty()) {
      result.message = error;
      return result;
    }
    variant.manager_message_key =
        requireString(item, "manager_message_key", error);
    if (!error.empty()) {
      result.message = error;
      return result;
    }
    if (!readReasonCodes(item, variant.reason_codes, error)) {
      result.message = error;
      return result;
    }
    if (!validateVariant(variant, error)) {
      result.message = error;
      return result;
    }
    set.variants.push_back(std::move(variant));
  }

  result.loaded = true;
  result.message = "anti-cheat compatibility config loaded";
  result.compatibility_set = std::move(set);
  return result;
}

AntiCheatLaunchRoute routeAntiCheatLaunch(
    const AntiCheatLaunchRequest& request,
    const AntiCheatCompatibilitySet& compatibility_set,
    const DetectionIndicatorCatalog& catalog) {
  DetectionSignals signals = classifyDetectionSignals(request.detection, catalog);

  if (!compatibility_set.loaded()) {
    return makeRoute(
        AntiCheatRouteKind::Blocked, false,
        "anti_cheat_compatibility_not_loaded",
        "safety.unknown.anti_cheat.compatibility_not_loaded",
        "anti-cheat launch compatibility data is not loaded; refusing by default",
        request, signals);
  }

  if (signals.anti_cheat == RiskSignal::Uncertain) {
    return makeRoute(
        AntiCheatRouteKind::Blocked, false,
        "anti_cheat_signal_uncertain",
        "safety.unknown.anti_cheat.signal_uncertain",
        "anti-cheat signal is uncertain; refusing by default",
        request, signals);
  }

  if (signals.anti_cheat == RiskSignal::Risky) {
    const AntiCheatLaunchVariant* launch_only =
        findBestVariant(compatibility_set, request, isAcLaunchOnlyVariant);
    if (launch_only != nullptr) {
      return makeRoute(
          AntiCheatRouteKind::LaunchOnly, false,
          launch_only->reason_codes.launch_only,
          launch_only->manager_message_key,
          "anti-cheat detected; launch-only route selected and injection disabled",
          request, signals, launch_only);
    }

    const AntiCheatLaunchVariant* blocked =
        findBestVariant(compatibility_set, request, isBlockWhenDetectedVariant);
    const std::string reason = blocked != nullptr
        ? blocked->reason_codes.block
        : "anti_cheat_no_sanctioned_launch_path";
    const std::string message_key = blocked != nullptr
        ? blocked->manager_message_key
        : "safety.block.anti_cheat.no_sanctioned_launch_path";
    return makeRoute(
        AntiCheatRouteKind::Blocked, false, reason, message_key,
        "anti-cheat detected and no legitimate launch-only path is configured",
        request, signals, blocked);
  }

  if (request.requested_mode == RequestedLaunchMode::LaunchOnly) {
    const AntiCheatLaunchVariant* launch_only =
        findBestVariant(compatibility_set, request, isLaunchOnlyVariant);
    if (launch_only != nullptr) {
      return makeRoute(
          AntiCheatRouteKind::LaunchOnly, false,
          launch_only->reason_codes.launch_only,
          launch_only->manager_message_key,
          "launch-only route selected; injector is not used",
          request, signals, launch_only);
    }
    return makeRoute(
        AntiCheatRouteKind::Blocked, false, "no_compatible_launch_variant",
        "safety.block.launch.no_compatible_variant",
        "no compatible launch-only variant is configured",
        request, signals);
  }

  const AntiCheatLaunchVariant* injector =
      findBestVariant(compatibility_set, request, isInjectorVariant);
  if (injector != nullptr) {
    return makeRoute(
        AntiCheatRouteKind::InjectorCapable, true,
        injector->reason_codes.injector_capable,
        injector->manager_message_key,
        "anti-cheat signal is clear; injector-capable variant may continue to the safety verdict",
        request, signals, injector);
  }

  const AntiCheatLaunchVariant* launch_only =
      findBestVariant(compatibility_set, request, isLaunchOnlyVariant);
  if (launch_only != nullptr) {
    return makeRoute(
        AntiCheatRouteKind::LaunchOnly, false,
        launch_only->reason_codes.launch_only,
        launch_only->manager_message_key,
        "launch-only variant is available; injector is not used",
        request, signals, launch_only);
  }

  return makeRoute(
      AntiCheatRouteKind::Blocked, false, "no_compatible_launch_variant",
      "safety.block.launch.no_compatible_variant",
      "no compatible launch variant is configured",
      request, signals);
}

}  // namespace vrclient::safety

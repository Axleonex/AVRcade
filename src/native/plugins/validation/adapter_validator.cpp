#include "plugins/validation/adapter_validator.h"

#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace vrclient::plugins::validation {
namespace {

std::string readText(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::in | std::ios::binary);
  std::ostringstream text;
  text << input.rdbuf();
  return text.str();
}

void addFinding(
    AdapterValidationResult& result,
    ValidationSeverity severity,
    ValidationCategory category,
    std::string code,
    std::string message) {
  result.findings.push_back({severity, category, std::move(code), std::move(message)});
}

bool manifestContains(const std::string& manifest, const std::string& value) {
  return !value.empty() && manifest.find(value) != std::string::npos;
}

bool categoryPassed(
    const AdapterValidationResult& result,
    ValidationCategory category) {
  for (const ValidationFinding& finding : result.findings) {
    if (finding.category == category && finding.severity == ValidationSeverity::Error) {
      return false;
    }
  }
  return true;
}

}  // namespace

const char* validationSeverityName(ValidationSeverity severity) {
  switch (severity) {
    case ValidationSeverity::Info:
      return "info";
    case ValidationSeverity::Warning:
      return "warning";
    case ValidationSeverity::Error:
      return "error";
  }
  return "error";
}

const char* validationCategoryName(ValidationCategory category) {
  switch (category) {
    case ValidationCategory::LoadCompatible:
      return "load-compatible";
    case ValidationCategory::TargetCompatible:
      return "target-compatible";
    case ValidationCategory::RuntimeSafe:
      return "runtime-safe";
  }
  return "load-compatible";
}

AdapterValidationResult validateAdapterPackage(
    const AdapterValidationRequest& request) {
  AdapterValidationResult result;

  if (request.manifest_required && request.manifest_path.empty()) {
    addFinding(
        result,
        ValidationSeverity::Error,
        ValidationCategory::LoadCompatible,
        "manifest_missing",
        "adapter package requires a manifest path");
  }

  std::string manifest;
  if (!request.manifest_path.empty()) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(request.manifest_path, error)) {
      addFinding(
          result,
          ValidationSeverity::Error,
          ValidationCategory::LoadCompatible,
          "manifest_unreadable",
          "adapter manifest file is missing or unreadable");
    } else {
      manifest = readText(request.manifest_path);
    }
  }

  if (!request.exports.get_abi || !request.exports.get_metadata ||
      !request.exports.create_adapter || !request.exports.destroy_adapter) {
    addFinding(
        result,
        ValidationSeverity::Error,
        ValidationCategory::LoadCompatible,
        "missing_exported_symbol",
        "adapter is missing one or more required ABI exports");
  } else if (request.exports.get_abi() != VRCLIENT_ADAPTER_ABI_VERSION) {
    addFinding(
        result,
        ValidationSeverity::Error,
        ValidationCategory::LoadCompatible,
        "abi_mismatch",
        "adapter ABI version does not match host ABI version");
  }

  const VrAdapterMetadata* metadata =
      request.exports.get_metadata ? request.exports.get_metadata() : nullptr;
  if (!sdk::metadataHasAbiShape(metadata)) {
    addFinding(
        result,
        ValidationSeverity::Error,
        ValidationCategory::LoadCompatible,
        "missing_metadata",
        "adapter metadata is missing or malformed");
  } else {
    if (!sdk::apiVersionInRange(
            request.host_api_version,
            metadata->required_host_api_min,
            metadata->required_host_api_max)) {
      addFinding(
          result,
          ValidationSeverity::Error,
          ValidationCategory::LoadCompatible,
          "unsupported_api_version",
          "adapter API range is incompatible with this host");
    }
    if (!manifest.empty() && !manifestContains(manifest, metadata->adapter_id)) {
      addFinding(
          result,
          ValidationSeverity::Error,
          ValidationCategory::LoadCompatible,
          "manifest_adapter_mismatch",
          "adapter manifest does not declare the exported adapter ID");
    }
    if (!manifest.empty() && !manifestContains(manifest, request.expected_game_id)) {
      addFinding(
          result,
          ValidationSeverity::Error,
          ValidationCategory::TargetCompatible,
          "manifest_game_mismatch",
          "adapter manifest does not declare the expected game ID");
    }
    if (!manifest.empty() && !manifestContains(manifest, request.expected_build_id)) {
      addFinding(
          result,
          ValidationSeverity::Error,
          ValidationCategory::TargetCompatible,
          "manifest_build_mismatch",
          "adapter manifest does not declare the expected build ID");
    }

    host::PluginLoadRequest load_request;
    load_request.plugin_path = request.plugin_path;
    load_request.target_game_id = request.expected_game_id;
    load_request.target_build_id = request.expected_build_id;
    load_request.exports = request.exports;
    load_request.host_api_version = request.host_api_version;
    load_request.plugin_file_exists = request.plugin_file_exists;
    const auto load = host::loadPlugin(load_request);
    if (load.status != host::PluginHostStatus::Loaded) {
      addFinding(
          result,
          load.status == host::PluginHostStatus::WrongTarget
              ? ValidationSeverity::Error
              : ValidationSeverity::Error,
          load.status == host::PluginHostStatus::WrongTarget
              ? ValidationCategory::TargetCompatible
              : ValidationCategory::LoadCompatible,
          load.reason_code,
          load.message);
    } else {
      addFinding(
          result,
          ValidationSeverity::Info,
          ValidationCategory::LoadCompatible,
          "load_compatible",
          "adapter exports and ABI metadata are load-compatible");
      addFinding(
          result,
          ValidationSeverity::Info,
          ValidationCategory::TargetCompatible,
          "target_compatible",
          "adapter metadata supports the expected game/build identity");
      addFinding(
          result,
          ValidationSeverity::Info,
          ValidationCategory::RuntimeSafe,
          "runtime_safe_baseline",
          "adapter package passed baseline runtime-safety validation");
      host::AdapterInstance instance = load.adapter;
      host::unloadPlugin(instance);
    }
  }

  result.load_compatible = categoryPassed(result, ValidationCategory::LoadCompatible);
  result.target_compatible = categoryPassed(result, ValidationCategory::TargetCompatible);
  result.runtime_safe = categoryPassed(result, ValidationCategory::RuntimeSafe);
  result.passed = result.load_compatible && result.target_compatible && result.runtime_safe;
  return result;
}

}  // namespace vrclient::plugins::validation

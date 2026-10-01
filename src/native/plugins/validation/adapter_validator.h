#pragma once

#include "plugins/host/plugin_host.h"

#include <filesystem>
#include <string>
#include <vector>

namespace vrclient::plugins::validation {

enum class ValidationSeverity {
  Info,
  Warning,
  Error
};

enum class ValidationCategory {
  LoadCompatible,
  TargetCompatible,
  RuntimeSafe
};

struct ValidationFinding {
  ValidationSeverity severity = ValidationSeverity::Info;
  ValidationCategory category = ValidationCategory::LoadCompatible;
  std::string code;
  std::string message;
};

struct AdapterValidationRequest {
  std::filesystem::path plugin_path;
  std::filesystem::path manifest_path;
  std::string expected_game_id;
  std::string expected_build_id;
  host::PluginExports exports;
  VrAdapterApiVersion host_api_version = sdk::kHostApiVersion;
  bool manifest_required = true;
  bool plugin_file_exists = true;
};

struct AdapterValidationResult {
  bool passed = false;
  bool load_compatible = false;
  bool target_compatible = false;
  bool runtime_safe = false;
  std::vector<ValidationFinding> findings;
};

const char* validationSeverityName(ValidationSeverity severity);
const char* validationCategoryName(ValidationCategory category);

AdapterValidationResult validateAdapterPackage(
    const AdapterValidationRequest& request);

}  // namespace vrclient::plugins::validation

#pragma once

#include "diagnostics/crash/crash_capture.h"
#include "diagnostics/export/diagnostics_exporter.h"
#include "diagnostics/logging/diagnostic_logger.h"

#include <filesystem>
#include <string>

namespace vrclient::diagnostics {

struct DiagnosticsProfile {
  bool enabled = true;
  bool overlay_enabled = false;
  bool telemetry_aggregation_enabled = false;
  LoggerConfig logging;
  CrashArtifactConfig crash;
  DiagnosticsExportRequest export_request;
};

struct DiagnosticsProfileLoadResult {
  bool loaded = false;
  DiagnosticsProfile profile;
  std::string message;
};

DiagnosticsProfile defaultDiagnosticsProfile();
DiagnosticsProfileLoadResult loadDiagnosticsProfileFromFile(const char* path);

}  // namespace vrclient::diagnostics

#pragma once

#include "diagnostics/logging/diagnostic_logger.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vrclient::injector::process {

enum class DiscoveryFlow {
  DirectLaunch,
  AttachRunning,
  SteamLaunch,
  EpicLaunch,
  ManualPath
};

enum class DiscoveryStatus {
  Ready,
  MultipleCandidates,
  MissingExecutable,
  MissingProcess,
  ProcessRestarted,
  RunningProcessMismatch,
  PrivilegeMismatch,
  ArchitectureMismatch,
  InvalidRequest
};

struct DiagnosticsContextField {
  std::string key;
  std::string value;
};

struct InstallCandidate {
  std::filesystem::path executable_path;
  std::filesystem::path install_root;
  std::string source_hint;
  bool exists = true;
};

struct RunningProcessInfo {
  std::uint32_t process_id = 0;
  std::filesystem::path executable_path;
  std::string process_start_token;
  std::string architecture = "x64";
  std::string privilege_level = "user";
  bool exists = true;
  bool runtime_already_loaded = false;
};

struct TargetDescriptor {
  DiscoveryFlow flow = DiscoveryFlow::DirectLaunch;
  std::filesystem::path executable_path;
  std::filesystem::path install_root;
  std::uint32_t process_id = 0;
  bool running = false;
  std::string source_hint;
  std::string storefront_id;
  std::string game_id_hint;
  std::string build_id_hint;
  std::string architecture = "x64";
  std::string privilege_level = "user";
  std::string process_start_token;
  bool runtime_already_loaded = false;
  std::vector<std::string> command_line_plan;
  std::vector<DiagnosticsContextField> diagnostics_context;
  std::string executable_sha256;
  std::string product_version;
  std::vector<std::string> signature_tags;
  bool identity_evidence_trusted = false;
  std::string identity_evidence_source = "untrusted";
};

struct ProcessDiscoveryRequest {
  DiscoveryFlow flow = DiscoveryFlow::DirectLaunch;
  std::string game_id_hint;
  std::string build_id_hint;
  std::string storefront_id;
  std::filesystem::path executable_path;
  std::filesystem::path install_root;
  std::vector<std::string> launch_arguments;
  std::vector<InstallCandidate> install_candidates;
  RunningProcessInfo running_process;
  std::string expected_process_start_token;
  std::filesystem::path expected_running_executable;
  std::string expected_architecture = "x64";
  std::string current_privilege_level = "user";
  std::string required_privilege_level = "user";
  bool executable_exists = true;
  std::string executable_sha256;
  std::string product_version;
  std::vector<std::string> signature_tags;
  bool identity_evidence_trusted = false;
  std::string identity_evidence_source = "untrusted";
};

struct ProcessDiscoveryResult {
  DiscoveryStatus status = DiscoveryStatus::InvalidRequest;
  std::string reason_code;
  std::string message;
  std::vector<TargetDescriptor> descriptors;
};

const char* discoveryFlowName(DiscoveryFlow flow);
const char* discoveryStatusName(DiscoveryStatus status);

ProcessDiscoveryResult discoverTargets(
    const ProcessDiscoveryRequest& request,
    diagnostics::AsyncLogger* logger = nullptr);

}  // namespace vrclient::injector::process

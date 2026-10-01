#include "injector/process/process_discovery.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace vrclient::injector::process {
namespace {

std::string pathString(const std::filesystem::path& path) {
  return path.lexically_normal().generic_string();
}

std::string discoveryId(
    DiscoveryFlow flow,
    const std::filesystem::path& path,
    std::uint32_t process_id) {
  std::ostringstream out;
  out << discoveryFlowName(flow) << ':' << process_id << ':' << pathString(path);
  return out.str();
}

std::vector<diagnostics::LogField> logFieldsFor(
    const ProcessDiscoveryRequest& request,
    const ProcessDiscoveryResult& result) {
  return {
      {"flow", discoveryFlowName(request.flow)},
      {"status", discoveryStatusName(result.status)},
      {"reason_code", result.reason_code},
      {"game_id_hint", request.game_id_hint},
      {"descriptor_count", std::to_string(result.descriptors.size())},
  };
}

void logResult(
    const ProcessDiscoveryRequest& request,
    const ProcessDiscoveryResult& result,
    diagnostics::AsyncLogger* logger) {
  if (logger == nullptr) {
    return;
  }
  const auto fields = logFieldsFor(request, result);
  logger->log(
      result.status == DiscoveryStatus::Ready ? diagnostics::Severity::Info
                                              : diagnostics::Severity::Warning,
      "process_discovery_result",
      fields);
}

TargetDescriptor makeDescriptor(
    const ProcessDiscoveryRequest& request,
    const std::filesystem::path& executable_path,
    const std::filesystem::path& install_root,
    std::string source_hint,
    bool running,
    std::uint32_t process_id,
    std::string process_start_token,
    bool runtime_already_loaded) {
  TargetDescriptor descriptor;
  descriptor.flow = request.flow;
  descriptor.executable_path = executable_path.lexically_normal();
  descriptor.install_root = install_root.lexically_normal();
  descriptor.process_id = process_id;
  descriptor.running = running;
  descriptor.source_hint = std::move(source_hint);
  descriptor.storefront_id = request.storefront_id;
  descriptor.game_id_hint = request.game_id_hint;
  descriptor.build_id_hint = request.build_id_hint;
  descriptor.architecture = running ? request.running_process.architecture
                                    : request.expected_architecture;
  descriptor.privilege_level = running ? request.running_process.privilege_level
                                       : request.current_privilege_level;
  descriptor.process_start_token = std::move(process_start_token);
  descriptor.runtime_already_loaded = runtime_already_loaded;
  descriptor.identity_evidence_trusted = request.identity_evidence_trusted;
  descriptor.identity_evidence_source = request.identity_evidence_source;
  if (request.identity_evidence_trusted) {
    descriptor.executable_sha256 = request.executable_sha256;
    descriptor.product_version = request.product_version;
    descriptor.signature_tags = request.signature_tags;
  }
  descriptor.command_line_plan.push_back(pathString(descriptor.executable_path));
  descriptor.command_line_plan.insert(
      descriptor.command_line_plan.end(),
      request.launch_arguments.begin(),
      request.launch_arguments.end());
  descriptor.diagnostics_context = {
      {"discovery_id", discoveryId(request.flow, descriptor.executable_path, process_id)},
      {"source", descriptor.source_hint},
      {"flow", discoveryFlowName(request.flow)},
      {"running", running ? "true" : "false"},
  };
  return descriptor;
}

bool privilegeMismatch(const ProcessDiscoveryRequest& request) {
  return !request.required_privilege_level.empty() &&
      request.current_privilege_level != request.required_privilege_level;
}

ProcessDiscoveryResult finish(
    const ProcessDiscoveryRequest& request,
    ProcessDiscoveryResult result,
    diagnostics::AsyncLogger* logger) {
  logResult(request, result, logger);
  return result;
}

ProcessDiscoveryResult invalid(
    const ProcessDiscoveryRequest& request,
    std::string reason_code,
    std::string message,
    diagnostics::AsyncLogger* logger) {
  ProcessDiscoveryResult result;
  result.status = DiscoveryStatus::InvalidRequest;
  result.reason_code = std::move(reason_code);
  result.message = std::move(message);
  return finish(request, std::move(result), logger);
}

ProcessDiscoveryResult missingExecutable(
    const ProcessDiscoveryRequest& request,
    diagnostics::AsyncLogger* logger) {
  ProcessDiscoveryResult result;
  result.status = DiscoveryStatus::MissingExecutable;
  result.reason_code = "missing_executable";
  result.message = "target executable does not exist in the selected discovery flow";
  return finish(request, std::move(result), logger);
}

ProcessDiscoveryResult discoverFromCandidates(
    const ProcessDiscoveryRequest& request,
    diagnostics::AsyncLogger* logger) {
  std::vector<InstallCandidate> candidates = request.install_candidates;
  std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
    return pathString(left.executable_path) < pathString(right.executable_path);
  });

  ProcessDiscoveryResult result;
  for (const InstallCandidate& candidate : candidates) {
    if (!candidate.exists || candidate.executable_path.empty()) {
      continue;
    }
    result.descriptors.push_back(makeDescriptor(
        request,
        candidate.executable_path,
        candidate.install_root,
        candidate.source_hint.empty() ? discoveryFlowName(request.flow) : candidate.source_hint,
        false,
        0,
        {},
        false));
  }

  if (result.descriptors.empty()) {
    return missingExecutable(request, logger);
  }

  result.status = result.descriptors.size() == 1 ? DiscoveryStatus::Ready
                                                 : DiscoveryStatus::MultipleCandidates;
  result.reason_code = result.status == DiscoveryStatus::Ready
      ? "target_descriptor_ready"
      : "multiple_install_candidates";
  result.message = result.status == DiscoveryStatus::Ready
      ? "one deterministic target descriptor was discovered"
      : "multiple install candidates were returned as choices";
  return finish(request, std::move(result), logger);
}

ProcessDiscoveryResult discoverDirect(
    const ProcessDiscoveryRequest& request,
    diagnostics::AsyncLogger* logger) {
  if (request.executable_path.empty()) {
    return invalid(
        request,
        "missing_executable_path",
        "direct launch requires an executable path",
        logger);
  }
  if (!request.executable_exists) {
    return missingExecutable(request, logger);
  }
  if (privilegeMismatch(request)) {
    ProcessDiscoveryResult result;
    result.status = DiscoveryStatus::PrivilegeMismatch;
    result.reason_code = "privilege_mismatch";
    result.message = "current privilege level does not satisfy launch requirements";
    return finish(request, std::move(result), logger);
  }

  ProcessDiscoveryResult result;
  result.status = DiscoveryStatus::Ready;
  result.reason_code = "target_descriptor_ready";
  result.message = "direct launch target descriptor was built";
  result.descriptors.push_back(makeDescriptor(
      request,
      request.executable_path,
      request.install_root,
      "direct",
      false,
      0,
      {},
      false));
  return finish(request, std::move(result), logger);
}

ProcessDiscoveryResult discoverAttach(
    const ProcessDiscoveryRequest& request,
    diagnostics::AsyncLogger* logger) {
  const RunningProcessInfo& process = request.running_process;
  if (!process.exists || process.process_id == 0) {
    ProcessDiscoveryResult result;
    result.status = DiscoveryStatus::MissingProcess;
    result.reason_code = "missing_process";
    result.message = "attach request did not identify a running process";
    return finish(request, std::move(result), logger);
  }
  if (!request.expected_process_start_token.empty() &&
      process.process_start_token != request.expected_process_start_token) {
    ProcessDiscoveryResult result;
    result.status = DiscoveryStatus::ProcessRestarted;
    result.reason_code = "process_restarted";
    result.message = "running process start token changed after discovery";
    return finish(request, std::move(result), logger);
  }
  if (!request.expected_running_executable.empty() &&
      pathString(process.executable_path) != pathString(request.expected_running_executable)) {
    ProcessDiscoveryResult result;
    result.status = DiscoveryStatus::RunningProcessMismatch;
    result.reason_code = "running_process_mismatch";
    result.message = "running process executable does not match the discovered target";
    return finish(request, std::move(result), logger);
  }
  if (!request.expected_architecture.empty() &&
      process.architecture != request.expected_architecture) {
    ProcessDiscoveryResult result;
    result.status = DiscoveryStatus::ArchitectureMismatch;
    result.reason_code = "architecture_mismatch";
    result.message = "running process architecture does not match runtime architecture";
    return finish(request, std::move(result), logger);
  }
  if (!request.required_privilege_level.empty() &&
      process.privilege_level != request.required_privilege_level) {
    ProcessDiscoveryResult result;
    result.status = DiscoveryStatus::PrivilegeMismatch;
    result.reason_code = "privilege_mismatch";
    result.message = "attach process privilege level does not satisfy requirements";
    return finish(request, std::move(result), logger);
  }

  ProcessDiscoveryResult result;
  result.status = DiscoveryStatus::Ready;
  result.reason_code = "target_descriptor_ready";
  result.message = "attach target descriptor was built";
  result.descriptors.push_back(makeDescriptor(
      request,
      process.executable_path,
      request.install_root,
      "attach",
      true,
      process.process_id,
      process.process_start_token,
      process.runtime_already_loaded));
  return finish(request, std::move(result), logger);
}

}  // namespace

const char* discoveryFlowName(DiscoveryFlow flow) {
  switch (flow) {
    case DiscoveryFlow::DirectLaunch:
      return "direct_launch";
    case DiscoveryFlow::AttachRunning:
      return "attach_running";
    case DiscoveryFlow::SteamLaunch:
      return "steam_launch";
    case DiscoveryFlow::EpicLaunch:
      return "epic_launch";
    case DiscoveryFlow::ManualPath:
      return "manual_path";
  }
  return "unknown";
}

const char* discoveryStatusName(DiscoveryStatus status) {
  switch (status) {
    case DiscoveryStatus::Ready:
      return "ready";
    case DiscoveryStatus::MultipleCandidates:
      return "multiple_candidates";
    case DiscoveryStatus::MissingExecutable:
      return "missing_executable";
    case DiscoveryStatus::MissingProcess:
      return "missing_process";
    case DiscoveryStatus::ProcessRestarted:
      return "process_restarted";
    case DiscoveryStatus::RunningProcessMismatch:
      return "running_process_mismatch";
    case DiscoveryStatus::PrivilegeMismatch:
      return "privilege_mismatch";
    case DiscoveryStatus::ArchitectureMismatch:
      return "architecture_mismatch";
    case DiscoveryStatus::InvalidRequest:
      return "invalid_request";
  }
  return "invalid_request";
}

ProcessDiscoveryResult discoverTargets(
    const ProcessDiscoveryRequest& request,
    diagnostics::AsyncLogger* logger) {
  switch (request.flow) {
    case DiscoveryFlow::DirectLaunch:
      return discoverDirect(request, logger);
    case DiscoveryFlow::AttachRunning:
      return discoverAttach(request, logger);
    case DiscoveryFlow::SteamLaunch:
    case DiscoveryFlow::EpicLaunch:
    case DiscoveryFlow::ManualPath:
      return discoverFromCandidates(request, logger);
  }
  return invalid(request, "unknown_flow", "unsupported discovery flow", logger);
}

}  // namespace vrclient::injector::process

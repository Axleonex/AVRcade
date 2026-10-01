#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "diagnostics/logging/diagnostic_logger.h"
#include "injector/bootstrap/bootstrap_smoke.h"
#include "injector/process/process_discovery.h"
#include "safety/detection_inputs.h"
#include "safety/safety_verdict.h"
#include "versioning/game_fingerprint.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace {

using vrclient::injector::bootstrap::BootstrapResultCode;
using vrclient::injector::bootstrap::BootstrapRuntimeLoader;
using vrclient::injector::bootstrap::BootstrapSmokeRequest;
using vrclient::injector::bootstrap::RuntimeLoadAttempt;

constexpr char kAdapterId[] = "phase3-smoke-adapter";
constexpr char kExpectedRuleSetVersion[] = "2026-06-13.1";
constexpr wchar_t kProofSentinelEnv[] = L"VRCLIENT_PROOF_SENTINEL";
constexpr char kProofPayloadModule[] = "vrclient_proof_payload.dll";

struct Options {
  std::filesystem::path attach_loader;
  std::filesystem::path smoke_host;
  std::filesystem::path payload_dll;
  std::filesystem::path sentinel;
  std::filesystem::path blocked_sentinel;
  std::filesystem::path game_config;
  std::filesystem::path safety_rules;
  std::filesystem::path log_dir;
  unsigned timeout_ms = 30000;
};

void fail(const std::string& message) {
  throw std::runtime_error(message);
}

void expect(bool condition, const std::string& message) {
  if (!condition) {
    fail(message);
  }
}

std::filesystem::path repoRoot() {
#if defined(VRCLIENT_SOURCE_DIR)
  return std::filesystem::path(VRCLIENT_SOURCE_DIR);
#else
  return std::filesystem::current_path();
#endif
}

std::filesystem::path defaultGameConfigPath() {
  return repoRoot() / "config" / "games" / "sample-game.json";
}

std::filesystem::path defaultSafetyRulesPath() {
  return repoRoot() / "config" / "safety" / "default-rules.json";
}

std::wstring widen(const std::filesystem::path& path) {
  return path.lexically_normal().wstring();
}

std::wstring widen(const std::string& value) {
  return std::wstring(value.begin(), value.end());
}

std::wstring quoteArg(const std::wstring& arg) {
  std::wstring quoted = L"\"";
  for (wchar_t ch : arg) {
    if (ch == L'"') {
      quoted += L"\\\"";
    } else {
      quoted += ch;
    }
  }
  quoted += L"\"";
  return quoted;
}

std::string pathString(const std::filesystem::path& path) {
  return path.lexically_normal().generic_string();
}

Options parseArgs(int argc, char** argv) {
  Options options;
  options.game_config = defaultGameConfigPath();
  options.safety_rules = defaultSafetyRulesPath();

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&](const char* name) -> std::string {
      if (i + 1 >= argc) {
        fail(std::string("missing value for ") + name);
      }
      return argv[++i];
    };

    if (arg == "--attach-loader") {
      options.attach_loader = next("--attach-loader");
    } else if (arg == "--smoke-host") {
      options.smoke_host = next("--smoke-host");
    } else if (arg == "--payload-dll") {
      options.payload_dll = next("--payload-dll");
    } else if (arg == "--sentinel") {
      options.sentinel = next("--sentinel");
    } else if (arg == "--blocked-sentinel") {
      options.blocked_sentinel = next("--blocked-sentinel");
    } else if (arg == "--game-config") {
      options.game_config = next("--game-config");
    } else if (arg == "--safety-rules") {
      options.safety_rules = next("--safety-rules");
    } else if (arg == "--log-dir") {
      options.log_dir = next("--log-dir");
    } else if (arg == "--timeout-ms") {
      options.timeout_ms = static_cast<unsigned>(std::stoul(next("--timeout-ms")));
    } else {
      fail("unknown argument: " + arg);
    }
  }

  if (options.blocked_sentinel.empty() && !options.sentinel.empty()) {
    options.blocked_sentinel = options.sentinel;
    options.blocked_sentinel += ".blocked";
  }
  if (options.log_dir.empty()) {
    options.log_dir =
        std::filesystem::temp_directory_path() / "vrclient-easyhook-attach-smoke-logs";
  }

  expect(!options.attach_loader.empty(), "--attach-loader is required");
  expect(!options.smoke_host.empty(), "--smoke-host is required");
  expect(!options.payload_dll.empty(), "--payload-dll is required");
  expect(!options.sentinel.empty(), "--sentinel is required");
  expect(std::filesystem::exists(options.attach_loader), "EasyHook attach loader does not exist");
  expect(std::filesystem::exists(options.smoke_host), "smoke host does not exist");
  expect(std::filesystem::exists(options.payload_dll), "proof payload does not exist");
  expect(std::filesystem::exists(options.game_config), "game config does not exist");
  expect(std::filesystem::exists(options.safety_rules), "safety rules do not exist");
  return options;
}

void removeIfExists(const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::remove(path, error);
}

std::string makeToken(const char* suffix) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  std::ostringstream out;
  out << std::hex << GetCurrentProcessId() << "-" << now << "-" << suffix;
  return out.str();
}

std::map<std::string, std::string> parseSentinel(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::in | std::ios::binary);
  if (!input) {
    return {};
  }
  std::string text((std::istreambuf_iterator<char>(input)),
                   std::istreambuf_iterator<char>());
  std::map<std::string, std::string> fields;
  std::size_t start = 0;
  while (start < text.size()) {
    const std::size_t end = text.find_first_of(";\r\n", start);
    const std::string part = text.substr(start, end == std::string::npos
                                                    ? std::string::npos
                                                    : end - start);
    const std::size_t eq = part.find('=');
    if (eq != std::string::npos) {
      fields.emplace(part.substr(0, eq), part.substr(eq + 1));
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return fields;
}

std::string fieldValue(
    const std::map<std::string, std::string>& fields,
    const std::string& key) {
  const auto it = fields.find(key);
  return it == fields.end() ? std::string() : it->second;
}

class ScopedEnvironmentVariable {
 public:
  ScopedEnvironmentVariable(const wchar_t* name, const std::wstring& value)
      : name_(name) {
    const DWORD length = GetEnvironmentVariableW(name_.c_str(), nullptr, 0);
    if (length > 0) {
      had_previous_ = true;
      previous_.resize(length);
      const DWORD copied =
          GetEnvironmentVariableW(name_.c_str(), previous_.data(), length);
      if (copied > 0 && copied < length) {
        previous_.resize(copied);
      }
    }
    ok_ = SetEnvironmentVariableW(name_.c_str(), value.c_str()) == TRUE;
  }

  ~ScopedEnvironmentVariable() {
    if (had_previous_) {
      SetEnvironmentVariableW(name_.c_str(), previous_.c_str());
    } else {
      SetEnvironmentVariableW(name_.c_str(), nullptr);
    }
  }

  bool ok() const { return ok_; }

 private:
  std::wstring name_;
  std::wstring previous_;
  bool had_previous_ = false;
  bool ok_ = false;
};

class ControlledSmokeHost {
 public:
  ControlledSmokeHost(
      const std::filesystem::path& smoke_host,
      const std::filesystem::path& sentinel,
      const std::string& token)
      : sentinel_(sentinel),
        token_(token),
        sentinel_env_(
            kProofSentinelEnv,
            widen(sentinel) + L"|" + widen(token)) {
    expect(sentinel_env_.ok(), "failed to configure proof sentinel environment");

    std::error_code error;
    std::filesystem::create_directories(sentinel_.parent_path(), error);
    removeIfExists(sentinel_);

    std::wstring command_line = quoteArg(widen(smoke_host));
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    const BOOL created = CreateProcessW(
        widen(smoke_host).c_str(),
        mutable_command.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &startup,
        &process_);
    if (created != TRUE) {
      const DWORD error_code = GetLastError();
      std::ostringstream out;
      out << "failed to start controlled smoke host, GetLastError=" << error_code;
      fail(out.str());
    }
    launched_ = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
  }

  ControlledSmokeHost(const ControlledSmokeHost&) = delete;
  ControlledSmokeHost& operator=(const ControlledSmokeHost&) = delete;

  ~ControlledSmokeHost() {
    if (launched_) {
      TerminateProcess(process_.hProcess, 0);
      WaitForSingleObject(process_.hProcess, 5000);
      CloseHandle(process_.hThread);
      CloseHandle(process_.hProcess);
    }
  }

  DWORD pid() const { return process_.dwProcessId; }
  const std::string& token() const { return token_; }

 private:
  std::filesystem::path sentinel_;
  std::string token_;
  ScopedEnvironmentVariable sentinel_env_;
  PROCESS_INFORMATION process_{};
  bool launched_ = false;
};

struct ProcessRunResult {
  bool launched = false;
  bool timed_out = false;
  DWORD exit_code = 0xffffffff;
  DWORD error_code = 0;
};

ProcessRunResult runAttachLoader(
    const std::filesystem::path& attach_loader,
    DWORD pid,
    const std::filesystem::path& payload_dll,
    unsigned timeout_ms) {
  ProcessRunResult result;
  std::wostringstream pid_text;
  pid_text << pid;
  std::wstring command_line = quoteArg(widen(attach_loader)) + L" --pid " +
      pid_text.str() + L" --dll " + quoteArg(widen(payload_dll));
  std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
  mutable_command.push_back(L'\0');

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};

  const BOOL created = CreateProcessW(
      widen(attach_loader).c_str(),
      mutable_command.data(),
      nullptr,
      nullptr,
      FALSE,
      0,
      nullptr,
      nullptr,
      &startup,
      &process);
  if (created != TRUE) {
    result.error_code = GetLastError();
    return result;
  }

  result.launched = true;
  const DWORD wait = WaitForSingleObject(process.hProcess, timeout_ms);
  if (wait == WAIT_TIMEOUT) {
    result.timed_out = true;
    TerminateProcess(process.hProcess, 124);
    WaitForSingleObject(process.hProcess, 5000);
  }
  GetExitCodeProcess(process.hProcess, &result.exit_code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return result;
}

class EasyHookAttachRuntimeLoader final : public BootstrapRuntimeLoader {
 public:
  EasyHookAttachRuntimeLoader(
      Options options,
      std::filesystem::path sentinel,
      std::string token)
      : options_(std::move(options)),
        sentinel_(std::move(sentinel)),
        token_(std::move(token)) {}

  RuntimeLoadAttempt loadRuntime(
      const vrclient::injector::safety::SafetyPreflightRequest& preflight) override {
    invoked_ = true;
    removeIfExists(sentinel_);
    run_ = runAttachLoader(
        options_.attach_loader,
        preflight.target.process_id,
        options_.payload_dll,
        options_.timeout_ms);
    if (!run_.launched) {
      return {false, false, "easyhook_attach_launch_failed",
              "CreateProcessW failed for user-supplied EasyHook attach loader"};
    }
    if (run_.timed_out) {
      return {false, true, "easyhook_attach_timeout",
              "EasyHook attach loader timed out"};
    }
    if (run_.exit_code != 0) {
      return {false, true, "easyhook_attach_failed",
              "EasyHook attach loader returned a nonzero exit code"};
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
      if (std::filesystem::exists(sentinel_)) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    sentinel_fields_ = parseSentinel(sentinel_);
    std::string detail;
    sentinel_confirmed_ =
        sentinelMatches(sentinel_fields_, token_, preflight.target.process_id, &detail);
    sentinel_detail_ = detail;
    if (!sentinel_confirmed_) {
      return {false, true, "easyhook_attach_sentinel_unconfirmed", detail};
    }

    return {true, false, "easyhook_attach_loaded",
            "EasyHook-compatible attach loader loaded proof payload into running smoke host"};
  }

  bool probePoseTimingApi() override { return sentinel_confirmed_; }
  bool cleanupPartialLoad() override { return true; }

  bool invoked() const { return invoked_; }
  bool launched() const { return run_.launched; }
  bool sentinelConfirmed() const { return sentinel_confirmed_; }
  DWORD exitCode() const { return run_.exit_code; }
  const std::string& token() const { return token_; }
  const std::string& sentinelDetail() const { return sentinel_detail_; }
  const std::map<std::string, std::string>& sentinelFields() const {
    return sentinel_fields_;
  }

 private:
  bool sentinelMatches(
      const std::map<std::string, std::string>& fields,
      const std::string& token,
      DWORD expected_pid,
      std::string* detail) const {
    if (fieldValue(fields, "token") != token) {
      *detail = "sentinel token mismatch";
      return false;
    }
    if (fieldValue(fields, "module") != kProofPayloadModule) {
      *detail = "sentinel module mismatch";
      return false;
    }
    if (fieldValue(fields, "pid") != std::to_string(expected_pid)) {
      *detail = "sentinel process id mismatch";
      return false;
    }
    if (fieldValue(fields, "pose_timing_api") != "available") {
      *detail = "proof payload pose/timing marker unavailable";
      return false;
    }
    *detail = "attach sentinel confirmed";
    return true;
  }

  Options options_;
  std::filesystem::path sentinel_;
  std::string token_;
  bool invoked_ = false;
  bool sentinel_confirmed_ = false;
  ProcessRunResult run_;
  std::string sentinel_detail_;
  std::map<std::string, std::string> sentinel_fields_;
};

vrclient::injector::process::TargetDescriptor discoverAttachTarget(
    const Options& options,
    const vrclient::versioning::GameFingerprintConfig& config,
    DWORD pid,
    const std::string& process_start_token,
    vrclient::diagnostics::AsyncLogger* logger) {
  vrclient::injector::process::ProcessDiscoveryRequest request;
  request.flow = vrclient::injector::process::DiscoveryFlow::AttachRunning;
  request.game_id_hint = config.game_id;
  request.expected_process_start_token = process_start_token;
  request.expected_running_executable = options.smoke_host;
  request.expected_architecture = config.support_policy.target_architecture;
  request.required_privilege_level = "user";
  request.install_root = options.smoke_host.parent_path();
  request.running_process.process_id = pid;
  request.running_process.exists = true;
  request.running_process.executable_path = options.smoke_host;
  request.running_process.process_start_token = process_start_token;
  request.running_process.architecture = config.support_policy.target_architecture;
  request.running_process.privilege_level = "user";
  request.identity_evidence_trusted = true;
  request.identity_evidence_source = "easyhook_attach_smoke_config_model";

  for (const auto& build : config.builds) {
    if (build.supported && !build.file_hashes.empty()) {
      request.executable_sha256 = build.file_hashes.front().value;
      request.build_id_hint = build.build_id;
      if (!build.product_versions.empty()) {
        request.product_version = build.product_versions.front();
      }
      break;
    }
  }

  const auto discovered =
      vrclient::injector::process::discoverTargets(request, logger);
  expect(discovered.status == vrclient::injector::process::DiscoveryStatus::Ready,
         "attach discovery did not produce a ready descriptor: " + discovered.reason_code);
  expect(discovered.descriptors.size() == 1, "attach discovery descriptor count mismatch");
  expect(discovered.descriptors.front().flow ==
             vrclient::injector::process::DiscoveryFlow::AttachRunning,
         "attach discovery did not preserve AttachRunning flow");
  expect(discovered.descriptors.front().running,
         "attach descriptor did not mark target as running");
  expect(discovered.descriptors.front().process_id == pid,
         "attach descriptor process id mismatch");
  return discovered.descriptors.front();
}

vrclient::safety::SafetyVerdictResult evaluateSmokeSafety(
    const vrclient::versioning::VersionDetectionResult& identity,
    const vrclient::injector::safety::SafetyPreflightRequest& preflight,
    const vrclient::safety::SafetyRuleSet& rules,
    bool force_known_anti_cheat,
    vrclient::diagnostics::AsyncLogger* logger) {
  vrclient::safety::SafetyEvaluationRequest request;
  request.identity = identity;
  request.preflight = preflight;
  request.adapter_id = kAdapterId;
  request.requested_launch_mode = "offline";
  request.expected_rule_set_version = kExpectedRuleSetVersion;
  request.detection.module_enumeration_complete = true;
  request.detection.modding_posture =
      vrclient::safety::ModdingPosture::CommunitySupported;
  if (force_known_anti_cheat) {
    request.detection.observed_anti_cheat_indicators = {"EasyAntiCheat"};
  }
  return vrclient::safety::evaluateSafetyVerdict(request, rules, logger);
}

void printFields(
    const std::string& prefix,
    const std::vector<vrclient::diagnostics::LogField>& fields) {
  for (const auto& field : fields) {
    std::cout << prefix << field.key << "=" << field.value << "\n";
  }
}

int run(const Options& options) {
  removeIfExists(options.sentinel);
  removeIfExists(options.blocked_sentinel);

  const auto loaded_config =
      vrclient::versioning::loadGameFingerprintConfig(options.game_config);
  expect(loaded_config.loaded, loaded_config.message);
  const auto loaded_rules = vrclient::safety::loadSafetyRuleSet(options.safety_rules);
  expect(loaded_rules.loaded, loaded_rules.message);

  vrclient::diagnostics::AsyncLogger logger;
  vrclient::diagnostics::LoggerConfig log_config;
  log_config.log_directory = options.log_dir;
  log_config.file_prefix = "vrclient-easyhook-attach-smoke";
  log_config.start_worker = false;
  log_config.max_retained_logs = 16;

  vrclient::diagnostics::SessionMetadata metadata;
  metadata.game_id = loaded_config.config.game_id;
  metadata.build_id = "smoke-2026-06-11";
  metadata.adapter_id = kAdapterId;
  metadata.runtime_version = "0.1.0";
  metadata.launch_path = pathString(options.attach_loader);
  expect(logger.start(log_config, metadata), "diagnostics logger failed to start");
  const auto diagnostics_log = logger.activeLogPath();

  ControlledSmokeHost host(options.smoke_host, options.sentinel, makeToken("attach"));
  const std::string start_token = "easyhook-attach-smoke-" + std::to_string(host.pid());
  const auto target = discoverAttachTarget(
      options,
      loaded_config.config,
      host.pid(),
      start_token,
      &logger);

  const auto identity =
      vrclient::versioning::detectVersion(target, loaded_config.config);
  expect(identity.status == vrclient::versioning::DetectionStatus::KnownSupported,
         "attach smoke target identity was not KnownSupported");

  auto preflight = vrclient::injector::safety::makePreflightRequest(
      target,
      identity,
      loaded_config.config.controlled_smoke_target,
      true,
      identity.runtime.sha256);

  const auto allow_verdict =
      evaluateSmokeSafety(identity, preflight, loaded_rules.rule_set, false, &logger);
  expect(allow_verdict.verdict == vrclient::safety::SafetyVerdict::Allow,
         "attach smoke safety verdict did not allow");

  EasyHookAttachRuntimeLoader loader(
      options,
      options.sentinel,
      host.token());
  BootstrapSmokeRequest request;
  request.preflight = preflight;
  request.runtime_loader = &loader;
  request.adapter_placeholder = kAdapterId;
  request.openxr_state = "not_started_attach_smoke";
  request.safety_verdict = allow_verdict;

  const auto result =
      vrclient::injector::bootstrap::runBootstrapSmoke(request, &logger);
  expect(result.code == BootstrapResultCode::Loaded,
         "EasyHook attach smoke did not load: " + result.reason_code);
  expect(result.runtime_load_attempted, "runtime load was not attempted");
  expect(loader.invoked(), "EasyHook attach loader was not invoked");
  expect(loader.launched(), "EasyHook attach loader process was not launched");
  expect(loader.sentinelConfirmed(), "attach sentinel was not confirmed");

  std::cout << "attach_token=" << loader.token() << "\n";
  std::cout << "attach_pid=" << host.pid() << "\n";
  std::cout << "attach_sentinel_confirmed=true\n";
  std::cout << "attach_sentinel_detail=" << loader.sentinelDetail() << "\n";
  std::cout << "easyhook_attach_exit_code=" << loader.exitCode() << "\n";
  printFields("", result.diagnostics_fields);
  for (const auto& [key, value] : loader.sentinelFields()) {
    std::cout << "sentinel_" << key << "=" << value << "\n";
  }

  ControlledSmokeHost blocked_host(
      options.smoke_host,
      options.blocked_sentinel,
      makeToken("blocked"));
  const auto blocked_target = discoverAttachTarget(
      options,
      loaded_config.config,
      blocked_host.pid(),
      "easyhook-attach-smoke-blocked-" + std::to_string(blocked_host.pid()),
      &logger);
  const auto blocked_identity =
      vrclient::versioning::detectVersion(blocked_target, loaded_config.config);
  auto blocked_preflight = vrclient::injector::safety::makePreflightRequest(
      blocked_target,
      blocked_identity,
      loaded_config.config.controlled_smoke_target,
      true,
      blocked_identity.runtime.sha256);
  const auto block_verdict = evaluateSmokeSafety(
      blocked_identity,
      blocked_preflight,
      loaded_rules.rule_set,
      true,
      &logger);
  expect(block_verdict.verdict == vrclient::safety::SafetyVerdict::Block,
         "known anti-cheat observation did not block attach");
  expect(block_verdict.reason_code == "anti_cheat_detected",
         "blocked attach safety reason mismatch");

  EasyHookAttachRuntimeLoader blocked_loader(
      options,
      options.blocked_sentinel,
      blocked_host.token());
  BootstrapSmokeRequest blocked;
  blocked.preflight = blocked_preflight;
  blocked.runtime_loader = &blocked_loader;
  blocked.adapter_placeholder = kAdapterId;
  blocked.openxr_state = "not_started_attach_smoke";
  blocked.safety_verdict = block_verdict;

  const auto blocked_result =
      vrclient::injector::bootstrap::runBootstrapSmoke(blocked, &logger);
  expect(blocked_result.code == BootstrapResultCode::RefusedSafetyVerdict,
         "blocked safety verdict did not refuse before attach");
  expect(!blocked_result.runtime_load_attempted,
         "blocked safety verdict attempted runtime load");
  expect(!blocked_loader.invoked(),
         "blocked safety verdict invoked the EasyHook attach loader");
  expect(!std::filesystem::exists(options.blocked_sentinel),
         "blocked attach path created a sentinel despite refusing before load");

  printFields("blocked_", blocked_result.diagnostics_fields);
  std::cout << "blocked_runtime_load_attempted=false\n";
  std::cout << "blocked_loader_invoked=false\n";
  std::cout << "blocked_sentinel_exists=false\n";

  logger.stop();
  std::cout << "diagnostics_log=" << pathString(diagnostics_log) << "\n";
  std::cout << "attach_loader=" << pathString(options.attach_loader) << "\n";
  std::cout << "payload_dll=" << pathString(options.payload_dll) << "\n";
  std::cout << "sentinel_module=" << fieldValue(loader.sentinelFields(), "module")
            << "\n";
  std::cout << "pose_timing_api="
            << fieldValue(loader.sentinelFields(), "pose_timing_api") << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = parseArgs(argc, argv);
    return run(options);
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << "\n";
    return 1;
  }
}

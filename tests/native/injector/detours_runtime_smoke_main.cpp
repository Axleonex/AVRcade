#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "diagnostics/logging/diagnostic_logger.h"
#include "injector/bootstrap/bootstrap_smoke.h"
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
#include <vector>

namespace {

using vrclient::injector::bootstrap::BootstrapResultCode;
using vrclient::injector::bootstrap::BootstrapRuntimeLoader;
using vrclient::injector::bootstrap::BootstrapSmokeRequest;
using vrclient::injector::bootstrap::RuntimeLoadAttempt;

constexpr char kAdapterId[] = "phase3-smoke-adapter";
constexpr char kExpectedRuleSetVersion[] = "2026-06-13.1";
constexpr wchar_t kRuntimeSmokeSentinelEnv[] = L"VRCLIENT_RUNTIME_SMOKE_SENTINEL";
constexpr wchar_t kRuntimeSmokeProfileEnv[] = L"VRCLIENT_RUNTIME_SMOKE_PROFILE";
constexpr wchar_t kRuntimeSmokeModuleEnv[] = L"VRCLIENT_RUNTIME_SMOKE_MODULE";
constexpr wchar_t kRuntimeSmokeModuleName[] = L"vrclient_runtime_smoke_payload.dll";

struct Options {
  std::filesystem::path withdll;
  std::filesystem::path smoke_host;
  std::filesystem::path payload_dll;
  std::filesystem::path sentinel;
  std::filesystem::path blocked_sentinel;
  std::filesystem::path profile;
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

std::filesystem::path defaultRuntimeProfilePath() {
  return repoRoot() / "config" / "defaults" / "runtime-profile.json";
}

std::wstring widen(const std::filesystem::path& path) {
  return path.lexically_normal().wstring();
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

std::string narrowAscii(const std::wstring& value) {
  std::string out;
  out.reserve(value.size());
  for (wchar_t ch : value) {
    out.push_back(ch <= 0x7f ? static_cast<char>(ch) : '?');
  }
  return out;
}

std::string pathString(const std::filesystem::path& path) {
  return path.lexically_normal().generic_string();
}

Options parseArgs(int argc, char** argv) {
  Options options;
  options.game_config = defaultGameConfigPath();
  options.safety_rules = defaultSafetyRulesPath();
  options.profile = defaultRuntimeProfilePath();

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&](const char* name) -> std::string {
      if (i + 1 >= argc) {
        fail(std::string("missing value for ") + name);
      }
      return argv[++i];
    };

    if (arg == "--withdll") {
      options.withdll = next("--withdll");
    } else if (arg == "--smoke-host") {
      options.smoke_host = next("--smoke-host");
    } else if (arg == "--payload-dll") {
      options.payload_dll = next("--payload-dll");
    } else if (arg == "--sentinel") {
      options.sentinel = next("--sentinel");
    } else if (arg == "--blocked-sentinel") {
      options.blocked_sentinel = next("--blocked-sentinel");
    } else if (arg == "--profile") {
      options.profile = next("--profile");
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
    options.log_dir = std::filesystem::temp_directory_path() /
        "vrclient-detours-runtime-smoke-logs";
  }

  expect(!options.withdll.empty(), "--withdll is required");
  expect(!options.smoke_host.empty(), "--smoke-host is required");
  expect(!options.payload_dll.empty(), "--payload-dll is required");
  expect(!options.sentinel.empty(), "--sentinel is required");
  expect(std::filesystem::exists(options.withdll), "withdll.exe does not exist");
  expect(std::filesystem::exists(options.smoke_host), "smoke host does not exist");
  expect(std::filesystem::exists(options.payload_dll), "runtime smoke payload does not exist");
  expect(std::filesystem::exists(options.profile), "runtime profile does not exist");
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

bool sentinelMatches(
    const std::map<std::string, std::string>& fields,
    const std::string& token,
    std::string* detail) {
  auto get = [&](const std::string& key) -> std::string {
    const auto it = fields.find(key);
    return it == fields.end() ? std::string() : it->second;
  };

  if (get("token") != token) {
    *detail = "sentinel token mismatch";
    return false;
  }
  if (get("module") != "vrclient_runtime_smoke_payload.dll") {
    *detail = "sentinel module mismatch";
    return false;
  }
  if (get("runtime_create") != "VR_RUNTIME_OK") {
    *detail = "runtime_create did not succeed";
    return false;
  }
  if (get("runtime_state") != "stopped") {
    *detail = "runtime state was not stopped before session start";
    return false;
  }
  if (get("headset_state") != "VR_RUNTIME_OK") {
    *detail = "headset state query failed";
    return false;
  }
  if (get("frame_data") != "VR_RUNTIME_OK") {
    *detail = "frame data query failed";
    return false;
  }
  if (get("eye_count") != "2") {
    *detail = "frame data did not report two eyes";
    return false;
  }
  if (get("head_pose_orientation_valid") != "1") {
    *detail = "head pose orientation was not available";
    return false;
  }
  if (get("pose_timing_api") != "available") {
    *detail = "pose/timing API marker was unavailable";
    return false;
  }
  *detail = "runtime smoke sentinel confirmed";
  return true;
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

struct ProcessRunResult {
  bool launched = false;
  bool timed_out = false;
  DWORD exit_code = 0xffffffff;
  DWORD error_code = 0;
};

ProcessRunResult runWithDll(
    const std::filesystem::path& withdll,
    const std::filesystem::path& payload_dll,
    const std::filesystem::path& smoke_host,
    unsigned timeout_ms) {
  ProcessRunResult result;
  std::wstring command_line = quoteArg(widen(withdll)) + L" /d:" +
      quoteArg(widen(payload_dll)) + L" " + quoteArg(widen(smoke_host)) +
      L" --runtime-smoke";
  std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
  mutable_command.push_back(L'\0');

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};

  const BOOL created = CreateProcessW(
      widen(withdll).c_str(),
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

class DetoursLaunchRuntimeLoader final : public BootstrapRuntimeLoader {
 public:
  DetoursLaunchRuntimeLoader(
      Options options,
      std::filesystem::path sentinel,
      std::string token)
      : options_(std::move(options)),
        sentinel_(std::move(sentinel)),
        token_(std::move(token)) {}

  RuntimeLoadAttempt loadRuntime(
      const vrclient::injector::safety::SafetyPreflightRequest&) override {
    invoked_ = true;
    removeIfExists(sentinel_);
    std::error_code error;
    std::filesystem::create_directories(sentinel_.parent_path(), error);

    ScopedEnvironmentVariable sentinel_env(
        kRuntimeSmokeSentinelEnv,
        widen(sentinel_) + L"|" + std::wstring(token_.begin(), token_.end()));
    ScopedEnvironmentVariable profile_env(kRuntimeSmokeProfileEnv, widen(options_.profile));
    ScopedEnvironmentVariable module_env(kRuntimeSmokeModuleEnv, kRuntimeSmokeModuleName);
    if (!sentinel_env.ok() || !profile_env.ok() || !module_env.ok()) {
      return {false, false, "runtime_smoke_env_failed",
              "failed to configure runtime smoke environment"};
    }

    run_ = runWithDll(
        options_.withdll,
        options_.payload_dll,
        options_.smoke_host,
        options_.timeout_ms);
    if (!run_.launched) {
      return {false, false, "detours_withdll_launch_failed",
              "CreateProcessW failed for user-supplied Detours withdll.exe"};
    }
    if (run_.timed_out) {
      return {false, true, "detours_withdll_timeout",
              "Detours runtime smoke launch timed out"};
    }
    if (run_.exit_code != 0) {
      return {false, true, "runtime_smoke_host_failed",
              "runtime smoke host returned a nonzero exit code"};
    }

    sentinel_fields_ = parseSentinel(sentinel_);
    std::string detail;
    sentinel_confirmed_ = sentinelMatches(sentinel_fields_, token_, &detail);
    sentinel_detail_ = detail;
    if (!sentinel_confirmed_) {
      return {false, true, "runtime_smoke_sentinel_unconfirmed", detail};
    }

    return {true, false, "runtime_smoke_loaded",
            "Detours loaded runtime smoke payload and public runtime ABI probe succeeded"};
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
  Options options_;
  std::filesystem::path sentinel_;
  std::string token_;
  bool invoked_ = false;
  bool sentinel_confirmed_ = false;
  ProcessRunResult run_;
  std::string sentinel_detail_;
  std::map<std::string, std::string> sentinel_fields_;
};

vrclient::injector::process::TargetDescriptor makeSmokeTarget(
    const Options& options,
    const vrclient::versioning::GameFingerprintConfig& config) {
  vrclient::injector::process::TargetDescriptor target;
  target.flow = vrclient::injector::process::DiscoveryFlow::DirectLaunch;
  target.executable_path = options.smoke_host;
  target.install_root = options.smoke_host.parent_path();
  target.game_id_hint = config.game_id;
  target.architecture = config.support_policy.target_architecture;
  target.identity_evidence_trusted = true;
  target.identity_evidence_source = "detours_runtime_smoke_config_model";
  for (const auto& build : config.builds) {
    if (build.supported && !build.file_hashes.empty()) {
      target.executable_sha256 = build.file_hashes.front().value;
      target.build_id_hint = build.build_id;
      if (!build.product_versions.empty()) {
        target.product_version = build.product_versions.front();
      }
      break;
    }
  }
  return target;
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

std::string fieldValue(
    const std::map<std::string, std::string>& fields,
    const std::string& key) {
  const auto it = fields.find(key);
  return it == fields.end() ? std::string() : it->second;
}

int run(const Options& options) {
  removeIfExists(options.sentinel);
  removeIfExists(options.blocked_sentinel);

  const auto loaded_config =
      vrclient::versioning::loadGameFingerprintConfig(options.game_config);
  expect(loaded_config.loaded, loaded_config.message);
  const auto loaded_rules = vrclient::safety::loadSafetyRuleSet(options.safety_rules);
  expect(loaded_rules.loaded, loaded_rules.message);

  const auto target = makeSmokeTarget(options, loaded_config.config);
  const auto identity =
      vrclient::versioning::detectVersion(target, loaded_config.config);
  expect(identity.status == vrclient::versioning::DetectionStatus::KnownSupported,
         "smoke target identity was not KnownSupported");

  auto preflight = vrclient::injector::safety::makePreflightRequest(
      target,
      identity,
      loaded_config.config.controlled_smoke_target,
      true,
      identity.runtime.sha256);

  vrclient::diagnostics::AsyncLogger logger;
  vrclient::diagnostics::LoggerConfig log_config;
  log_config.log_directory = options.log_dir;
  log_config.file_prefix = "vrclient-detours-runtime-smoke";
  log_config.start_worker = false;
  log_config.max_retained_logs = 16;

  vrclient::diagnostics::SessionMetadata metadata;
  metadata.game_id = identity.game_id;
  metadata.build_id = identity.build_id;
  metadata.adapter_id = kAdapterId;
  metadata.runtime_version = "0.1.0";
  metadata.launch_path = pathString(options.withdll);
  expect(logger.start(log_config, metadata), "diagnostics logger failed to start");

  const auto diagnostics_log = logger.activeLogPath();

  const auto allow_verdict =
      evaluateSmokeSafety(identity, preflight, loaded_rules.rule_set, false, &logger);
  expect(allow_verdict.verdict == vrclient::safety::SafetyVerdict::Allow,
         "smoke safety verdict did not allow");

  DetoursLaunchRuntimeLoader loader(
      options,
      options.sentinel,
      makeToken("runtime"));
  BootstrapSmokeRequest request;
  request.preflight = preflight;
  request.runtime_loader = &loader;
  request.adapter_placeholder = kAdapterId;
  request.openxr_state = "not_started_no_headset";
  request.safety_verdict = allow_verdict;

  const auto result =
      vrclient::injector::bootstrap::runBootstrapSmoke(request, &logger);
  expect(result.code == BootstrapResultCode::Loaded,
         "runtime smoke bootstrap did not load: " + result.reason_code);
  expect(result.runtime_load_attempted, "runtime load was not attempted");
  expect(loader.invoked(), "Detours runtime loader was not invoked");
  expect(loader.launched(), "Detours withdll.exe was not launched");
  expect(loader.sentinelConfirmed(), "runtime sentinel was not confirmed");

  std::cout << "runtime_token=" << loader.token() << "\n";
  std::cout << "runtime_sentinel_confirmed=true\n";
  std::cout << "runtime_sentinel_detail=" << loader.sentinelDetail() << "\n";
  std::cout << "runtime_withdll_exit_code=" << loader.exitCode() << "\n";
  printFields("", result.diagnostics_fields);
  for (const auto& [key, value] : loader.sentinelFields()) {
    std::cout << "sentinel_" << key << "=" << value << "\n";
  }

  const auto block_verdict =
      evaluateSmokeSafety(identity, preflight, loaded_rules.rule_set, true, &logger);
  expect(block_verdict.verdict == vrclient::safety::SafetyVerdict::Block,
         "known anti-cheat observation did not block");
  expect(block_verdict.reason_code == "anti_cheat_detected",
         "blocked safety reason mismatch");

  DetoursLaunchRuntimeLoader blocked_loader(
      options,
      options.blocked_sentinel,
      makeToken("blocked"));
  BootstrapSmokeRequest blocked;
  blocked.preflight = preflight;
  blocked.runtime_loader = &blocked_loader;
  blocked.adapter_placeholder = kAdapterId;
  blocked.openxr_state = "not_started_no_headset";
  blocked.safety_verdict = block_verdict;

  const auto blocked_result =
      vrclient::injector::bootstrap::runBootstrapSmoke(blocked, &logger);
  expect(blocked_result.code == BootstrapResultCode::RefusedSafetyVerdict,
         "blocked safety verdict did not refuse before load");
  expect(!blocked_result.runtime_load_attempted,
         "blocked safety verdict attempted runtime load");
  expect(!blocked_loader.invoked(),
         "blocked safety verdict invoked the Detours loader");
  expect(!std::filesystem::exists(options.blocked_sentinel),
         "blocked path created a sentinel despite refusing before load");

  printFields("blocked_", blocked_result.diagnostics_fields);
  std::cout << "blocked_runtime_load_attempted=false\n";
  std::cout << "blocked_loader_invoked=false\n";
  std::cout << "blocked_sentinel_exists=false\n";

  logger.stop();
  std::cout << "diagnostics_log=" << pathString(diagnostics_log) << "\n";
  std::cout << "runtime_profile=" << pathString(options.profile) << "\n";
  std::cout << "payload_dll=" << pathString(options.payload_dll) << "\n";
  std::cout << "runtime_create=" << fieldValue(loader.sentinelFields(), "runtime_create")
            << "\n";
  std::cout << "frame_data=" << fieldValue(loader.sentinelFields(), "frame_data")
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

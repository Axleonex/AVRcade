#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iterator>
#include <string>
#include <thread>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace {

// Referenced by sample-game.json signatures as identity evidence only.
[[maybe_unused]] volatile const char kSmokeHostIdentity[] = "VRCLIENT_SMOKE_HOST";

std::atomic_bool g_stop_requested{false};

void handleSignal(int) {
  g_stop_requested.store(true);
}

#if defined(_WIN32)
constexpr wchar_t kRuntimeSmokeSentinelEnv[] = L"VRCLIENT_RUNTIME_SMOKE_SENTINEL";
constexpr wchar_t kRuntimeSmokeProfileEnv[] = L"VRCLIENT_RUNTIME_SMOKE_PROFILE";
constexpr wchar_t kRuntimeSmokeModuleEnv[] = L"VRCLIENT_RUNTIME_SMOKE_MODULE";
constexpr wchar_t kDefaultRuntimeSmokeModule[] = L"vrclient_runtime_smoke_payload.dll";
constexpr char kRuntimeSmokeProbeExport[] = "vrclient_runtime_smoke_probe";

using RuntimeSmokeProbe = int (WINAPI*)(const wchar_t*, const wchar_t*);

std::wstring readEnvOrDefault(const wchar_t* name, const wchar_t* fallback) {
  wchar_t value[4096]{};
  const DWORD length =
      GetEnvironmentVariableW(name, value, static_cast<DWORD>(std::size(value)));
  if (length == 0 || length >= static_cast<DWORD>(std::size(value))) {
    return fallback != nullptr ? std::wstring(fallback) : std::wstring();
  }
  return std::wstring(value, value + length);
}

int runRuntimeSmokeProbe() {
  const std::wstring sentinel = readEnvOrDefault(kRuntimeSmokeSentinelEnv, L"");
  if (sentinel.empty()) {
    return 20;
  }

  const std::wstring module_name =
      readEnvOrDefault(kRuntimeSmokeModuleEnv, kDefaultRuntimeSmokeModule);
  HMODULE module = GetModuleHandleW(module_name.c_str());
  if (module == nullptr) {
    return 21;
  }

  auto* probe = reinterpret_cast<RuntimeSmokeProbe>(
      GetProcAddress(module, kRuntimeSmokeProbeExport));
  if (probe == nullptr) {
    return 22;
  }

  const std::wstring profile = readEnvOrDefault(kRuntimeSmokeProfileEnv, L"");
  return probe(sentinel.c_str(), profile.c_str());
}
#endif

}  // namespace

int main(int argc, char** argv) {
  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--once") == 0) {
      return 0;
    }
    if (std::strcmp(argv[i], "--runtime-smoke") == 0) {
#if defined(_WIN32)
      return runRuntimeSmokeProbe();
#else
      return 23;
#endif
    }
  }

  while (!g_stop_requested.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return 0;
}

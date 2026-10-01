#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct Options {
  DWORD pid = 0;
  std::filesystem::path dll;
  std::filesystem::path evidence;
};

std::wstring widen(const std::string& value) {
  if (value.empty()) {
    return {};
  }
  const int length = MultiByteToWideChar(
      CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(
      CP_UTF8,
      0,
      value.c_str(),
      static_cast<int>(value.size()),
      out.data(),
      length);
  return out;
}

Options parse(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&](const char* name) {
      if (i + 1 >= argc) {
        throw std::runtime_error(std::string("missing value for ") + name);
      }
      return std::string(argv[++i]);
    };
    if (arg == "--pid") {
      options.pid = static_cast<DWORD>(std::stoul(next("--pid")));
    } else if (arg == "--dll") {
      options.dll = widen(next("--dll"));
    } else if (arg == "--evidence") {
      options.evidence = widen(next("--evidence"));
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }
  if (options.pid == 0 || options.dll.empty() || options.evidence.empty()) {
    throw std::runtime_error("--pid, --dll, and --evidence are required");
  }
  options.dll = std::filesystem::absolute(options.dll);
  options.evidence = std::filesystem::absolute(options.evidence);
  if (!std::filesystem::is_regular_file(options.dll)) {
    throw std::runtime_error("payload DLL does not exist");
  }
  return options;
}

std::filesystem::path sidecarPath(DWORD pid) {
  wchar_t temp[MAX_PATH]{};
  if (GetTempPathW(MAX_PATH, temp) == 0) {
    throw std::runtime_error("GetTempPathW failed");
  }
  return std::filesystem::path(temp) /
      (L"vrclient-meccha-observer-" + std::to_wstring(pid) + L".path");
}

void writeEvidenceSidecar(DWORD pid, const std::filesystem::path& evidence) {
  const auto sidecar = sidecarPath(pid);
  const std::wstring text = evidence.wstring();
  const HANDLE file = CreateFileW(
      sidecar.c_str(),
      GENERIC_WRITE,
      0,
      nullptr,
      CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    throw std::runtime_error("failed to create evidence sidecar");
  }
  DWORD written = 0;
  const DWORD bytes = static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t));
  const BOOL ok = WriteFile(file, text.c_str(), bytes, &written, nullptr);
  FlushFileBuffers(file);
  CloseHandle(file);
  if (!ok || written != bytes) {
    throw std::runtime_error("failed to write evidence sidecar");
  }
}

int inject(const Options& options) {
  writeEvidenceSidecar(options.pid, options.evidence);

  HANDLE process = OpenProcess(
      PROCESS_CREATE_THREAD | PROCESS_QUERY_LIMITED_INFORMATION |
          PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
      FALSE,
      options.pid);
  if (process == nullptr) {
    throw std::runtime_error(
        "OpenProcess failed: " + std::to_string(GetLastError()));
  }

  wchar_t image[32768]{};
  DWORD image_length = static_cast<DWORD>(std::size(image));
  if (!QueryFullProcessImageNameW(process, 0, image, &image_length)) {
    const DWORD error = GetLastError();
    CloseHandle(process);
    throw std::runtime_error(
        "QueryFullProcessImageNameW failed: " + std::to_string(error));
  }

  const std::wstring dll = options.dll.wstring();
  const SIZE_T bytes = (dll.size() + 1) * sizeof(wchar_t);
  void* remote = VirtualAllocEx(
      process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (remote == nullptr) {
    const DWORD error = GetLastError();
    CloseHandle(process);
    throw std::runtime_error(
        "VirtualAllocEx failed: " + std::to_string(error));
  }

  SIZE_T written = 0;
  if (!WriteProcessMemory(
          process, remote, dll.c_str(), bytes, &written) ||
      written != bytes) {
    const DWORD error = GetLastError();
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);
    throw std::runtime_error(
        "WriteProcessMemory failed: " + std::to_string(error));
  }

  const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
  const auto load_library = reinterpret_cast<LPTHREAD_START_ROUTINE>(
      GetProcAddress(kernel, "LoadLibraryW"));
  if (load_library == nullptr) {
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);
    throw std::runtime_error("LoadLibraryW address unavailable");
  }

  HANDLE thread = CreateRemoteThread(
      process, nullptr, 0, load_library, remote, 0, nullptr);
  if (thread == nullptr) {
    const DWORD error = GetLastError();
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);
    throw std::runtime_error(
        "CreateRemoteThread failed: " + std::to_string(error));
  }

  const DWORD wait = WaitForSingleObject(thread, 10000);
  DWORD remote_result = 0;
  GetExitCodeThread(thread, &remote_result);
  CloseHandle(thread);
  VirtualFreeEx(process, remote, 0, MEM_RELEASE);
  CloseHandle(process);

  if (wait != WAIT_OBJECT_0) {
    throw std::runtime_error("remote LoadLibraryW timed out");
  }
  if (remote_result == 0) {
    throw std::runtime_error("remote LoadLibraryW returned null");
  }

  std::wcout << L"ATTACHED: pid=" << options.pid << L" image=" << image
             << L" dll=" << options.dll.wstring() << L"\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return inject(parse(argc, argv));
  } catch (const std::exception& error) {
    std::cerr << "ATTACH_FAILED: " << error.what() << "\n";
    return 1;
  }
}

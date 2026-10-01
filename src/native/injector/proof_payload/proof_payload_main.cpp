#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

constexpr wchar_t kSentinelEnv[] = L"VRCLIENT_PROOF_SENTINEL";
constexpr char kModuleName[] = "vrclient_proof_payload.dll";
constexpr char kPoseTimingApi[] = "available";

void appendChar(char*& cursor, char* end, char value) {
  if (cursor < end) {
    *cursor++ = value;
  }
}

void appendLiteral(char*& cursor, char* end, const char* value) {
  while (*value != '\0') {
    appendChar(cursor, end, *value++);
  }
}

void appendUInt(char*& cursor, char* end, DWORD value) {
  char digits[16]{};
  int count = 0;
  do {
    digits[count++] = static_cast<char>('0' + (value % 10));
    value /= 10;
  } while (value != 0 && count < static_cast<int>(sizeof(digits)));

  while (count > 0) {
    appendChar(cursor, end, digits[--count]);
  }
}

void appendWideAscii(char*& cursor, char* end, const wchar_t* value) {
  while (*value != L'\0') {
    appendChar(cursor, end, (*value <= 0x7f) ? static_cast<char>(*value) : '?');
    ++value;
  }
}

const wchar_t* findSeparator(wchar_t* value) {
  for (wchar_t* cursor = value; *cursor != L'\0'; ++cursor) {
    if (*cursor == L'|') {
      return cursor;
    }
  }
  return nullptr;
}

void writeSentinelFile() {
  wchar_t env_value[2048]{};
  const DWORD env_len = GetEnvironmentVariableW(
      kSentinelEnv, env_value, static_cast<DWORD>(sizeof(env_value) / sizeof(env_value[0])));
  if (env_len == 0 || env_len >= static_cast<DWORD>(sizeof(env_value) / sizeof(env_value[0]))) {
    return;
  }

  const wchar_t* separator = findSeparator(env_value);
  if (separator == nullptr || separator == env_value || separator[1] == L'\0') {
    return;
  }

  wchar_t* mutable_separator = const_cast<wchar_t*>(separator);
  *mutable_separator = L'\0';
  const wchar_t* sentinel_path = env_value;
  const wchar_t* token = separator + 1;

  wchar_t exe_path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, exe_path, static_cast<DWORD>(sizeof(exe_path) / sizeof(exe_path[0])));

  char line[4096]{};
  char* cursor = line;
  char* end = line + sizeof(line) - 2;
  appendLiteral(cursor, end, "pid=");
  appendUInt(cursor, end, GetCurrentProcessId());
  appendLiteral(cursor, end, ";token=");
  appendWideAscii(cursor, end, token);
  appendLiteral(cursor, end, ";module=");
  appendLiteral(cursor, end, kModuleName);
  appendLiteral(cursor, end, ";exe=");
  appendWideAscii(cursor, end, exe_path);
  appendLiteral(cursor, end, ";pose_timing_api=");
  appendLiteral(cursor, end, kPoseTimingApi);
  appendLiteral(cursor, end, "\r\n");

  DWORD bytes_written = 0;
  const HANDLE file = CreateFileW(
      sentinel_path,
      GENERIC_WRITE,
      0,
      nullptr,
      CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }
  WriteFile(file, line, static_cast<DWORD>(cursor - line), &bytes_written, nullptr);
  FlushFileBuffers(file);
  CloseHandle(file);
}

}  // namespace

extern "C" __declspec(dllexport) void CALLBACK DetourFinishHelperProcess(
    HWND,
    HINSTANCE,
    LPSTR,
    int) {}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(module);
    writeSentinelFile();
  }
  return TRUE;
}

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "public/vr_runtime_api.h"

#include <cstddef>
#include <cwchar>
#include <string>

namespace {

constexpr char kModuleName[] = "vrclient_runtime_smoke_payload.dll";

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

void appendInt(char*& cursor, char* end, int value) {
  if (value < 0) {
    appendChar(cursor, end, '-');
    value = -value;
  }

  char digits[32]{};
  int count = 0;
  do {
    digits[count++] = static_cast<char>('0' + (value % 10));
    value /= 10;
  } while (value != 0 && count < static_cast<int>(sizeof(digits)));

  while (count > 0) {
    appendChar(cursor, end, digits[--count]);
  }
}

void appendUInt(char*& cursor, char* end, unsigned long long value) {
  char digits[32]{};
  int count = 0;
  do {
    digits[count++] = static_cast<char>('0' + (value % 10));
    value /= 10;
  } while (value != 0 && count < static_cast<int>(sizeof(digits)));

  while (count > 0) {
    appendChar(cursor, end, digits[--count]);
  }
}

void appendDouble(char*& cursor, char* end, double value) {
  if (value < 0.0) {
    appendChar(cursor, end, '-');
    value = -value;
  }
  const auto whole = static_cast<unsigned long long>(value);
  appendUInt(cursor, end, whole);
  appendChar(cursor, end, '.');
  double fraction = value - static_cast<double>(whole);
  for (int i = 0; i < 6; ++i) {
    fraction *= 10.0;
    const int digit = static_cast<int>(fraction);
    appendChar(cursor, end, static_cast<char>('0' + digit));
    fraction -= static_cast<double>(digit);
  }
}

void appendWideAscii(char*& cursor, char* end, const wchar_t* value) {
  if (value == nullptr) {
    return;
  }
  while (*value != L'\0') {
    appendChar(cursor, end, (*value <= 0x7f) ? static_cast<char>(*value) : '?');
    ++value;
  }
}

std::string wideAscii(const wchar_t* value) {
  std::string converted;
  if (value == nullptr) {
    return converted;
  }
  while (*value != L'\0') {
    converted.push_back((*value <= 0x7f) ? static_cast<char>(*value) : '?');
    ++value;
  }
  return converted;
}

wchar_t* findSeparator(wchar_t* value) {
  for (wchar_t* cursor = value; *cursor != L'\0'; ++cursor) {
    if (*cursor == L'|') {
      return cursor;
    }
  }
  return nullptr;
}

const char* runtimeStateName(VrRuntimeState state) {
  switch (state) {
    case VR_RUNTIME_STATE_STOPPED:
      return "stopped";
    case VR_RUNTIME_STATE_INITIALIZING:
      return "initializing";
    case VR_RUNTIME_STATE_READY:
      return "ready";
    case VR_RUNTIME_STATE_RUNNING:
      return "running";
    case VR_RUNTIME_STATE_DEGRADED:
      return "degraded";
    case VR_RUNTIME_STATE_LOSS_PENDING:
      return "loss_pending";
    case VR_RUNTIME_STATE_EXITING:
      return "exiting";
    case VR_RUNTIME_STATE_ERROR:
      return "error";
  }
  return "unknown";
}

bool writeSentinel(
    const wchar_t* sentinel_path,
    const wchar_t* token,
    const wchar_t* profile_path,
    VrRuntimeResult create_result,
    VrRuntimeState runtime_state,
    VrRuntimeResult headset_result,
    const VrRuntimeHeadsetState& headset,
    VrRuntimeResult frame_result,
    const VrRuntimeFrameData& frame) {
  char line[4096]{};
  char* cursor = line;
  char* end = line + sizeof(line) - 2;

  appendLiteral(cursor, end, "pid=");
  appendUInt(cursor, end, GetCurrentProcessId());
  appendLiteral(cursor, end, ";token=");
  appendWideAscii(cursor, end, token);
  appendLiteral(cursor, end, ";module=");
  appendLiteral(cursor, end, kModuleName);
  appendLiteral(cursor, end, ";profile=");
  appendWideAscii(cursor, end, profile_path);
  appendLiteral(cursor, end, ";runtime_create=");
  appendLiteral(cursor, end, vr_runtime_result_name(create_result));
  appendLiteral(cursor, end, ";runtime_state=");
  appendLiteral(cursor, end, runtimeStateName(runtime_state));
  appendLiteral(cursor, end, ";headset_state=");
  appendLiteral(cursor, end, vr_runtime_result_name(headset_result));
  appendLiteral(cursor, end, ";headset_runtime_state=");
  appendLiteral(cursor, end, runtimeStateName(headset.runtime_state));
  appendLiteral(cursor, end, ";headset_session_active=");
  appendUInt(cursor, end, headset.session_active);
  appendLiteral(cursor, end, ";frame_data=");
  appendLiteral(cursor, end, vr_runtime_result_name(frame_result));
  appendLiteral(cursor, end, ";eye_count=");
  appendUInt(cursor, end, frame.eye_count);
  appendLiteral(cursor, end, ";head_pose_orientation_valid=");
  appendUInt(cursor, end, frame.head_pose.orientation_valid);
  appendLiteral(cursor, end, ";head_pose_position_valid=");
  appendUInt(cursor, end, frame.head_pose.position_valid);
  appendLiteral(cursor, end, ";frame_index=");
  appendUInt(cursor, end, frame.timing.frame_index);
  appendLiteral(cursor, end, ";predicted_period_seconds=");
  appendDouble(cursor, end, frame.timing.predicted_display_period_seconds);
  appendLiteral(cursor, end, ";pose_timing_api=");
  appendLiteral(cursor, end,
                (create_result == VR_RUNTIME_OK &&
                 headset_result == VR_RUNTIME_OK &&
                 frame_result == VR_RUNTIME_OK &&
                 frame.eye_count == 2 &&
                 frame.head_pose.orientation_valid != 0)
                    ? "available"
                    : "unavailable");
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
    return false;
  }
  const BOOL wrote =
      WriteFile(file, line, static_cast<DWORD>(cursor - line), &bytes_written, nullptr);
  FlushFileBuffers(file);
  CloseHandle(file);
  return wrote == TRUE && bytes_written == static_cast<DWORD>(cursor - line);
}

int runRuntimeSmokeProbe(const wchar_t* sentinel_spec, const wchar_t* profile_path) {
  if (sentinel_spec == nullptr || sentinel_spec[0] == L'\0') {
    return 30;
  }

  wchar_t mutable_spec[4096]{};
  wcsncpy_s(mutable_spec, sentinel_spec, _TRUNCATE);
  wchar_t* separator = findSeparator(mutable_spec);
  if (separator == nullptr || separator == mutable_spec || separator[1] == L'\0') {
    return 31;
  }
  *separator = L'\0';
  const wchar_t* sentinel_path = mutable_spec;
  const wchar_t* token = separator + 1;

  const std::string profile = wideAscii(profile_path);

  VrRuntimeDesc desc{};
  desc.size = sizeof(VrRuntimeDesc);
  desc.application_name = "VRClient Runtime Smoke";
  desc.runtime_profile_path = profile.empty() ? nullptr : profile.c_str();
  desc.preferred_graphics_backend = VR_RUNTIME_GRAPHICS_BACKEND_VULKAN;

  VrRuntime* runtime = nullptr;
  const VrRuntimeResult create_result = vr_runtime_create(&desc, &runtime);

  VrRuntimeState runtime_state = VR_RUNTIME_STATE_ERROR;
  VrRuntimeHeadsetState headset{};
  VrRuntimeFrameData frame{};
  VrRuntimeResult headset_result = VR_RUNTIME_ERROR_STATE;
  VrRuntimeResult frame_result = VR_RUNTIME_ERROR_STATE;

  if (runtime != nullptr) {
    runtime_state = vr_runtime_get_state(runtime);
    headset_result = vr_runtime_get_headset_state(runtime, &headset);
    frame_result = vr_runtime_get_frame_data(runtime, &frame);
    vr_runtime_destroy(runtime);
  }

  if (!writeSentinel(
          sentinel_path,
          token,
          profile_path,
          create_result,
          runtime_state,
          headset_result,
          headset,
          frame_result,
          frame)) {
    return 32;
  }

  return (create_result == VR_RUNTIME_OK &&
          runtime_state == VR_RUNTIME_STATE_STOPPED &&
          headset_result == VR_RUNTIME_OK &&
          frame_result == VR_RUNTIME_OK &&
          frame.eye_count == 2 &&
          frame.head_pose.orientation_valid != 0)
      ? 0
      : 33;
}

}  // namespace

extern "C" __declspec(dllexport) void CALLBACK DetourFinishHelperProcess(
    HWND,
    HINSTANCE,
    LPSTR,
    int) {}

extern "C" __declspec(dllexport) int WINAPI vrclient_runtime_smoke_probe(
    const wchar_t* sentinel_spec,
    const wchar_t* profile_path) {
  return runRuntimeSmokeProbe(sentinel_spec, profile_path);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(module);
  }
  return TRUE;
}

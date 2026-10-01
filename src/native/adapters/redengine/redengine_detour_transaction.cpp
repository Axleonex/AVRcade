#include "adapters/redengine/redengine_detour_transaction.h"

#include <detours.h>
#include <tlhelp32.h>

#include <vector>

namespace vrclient::adapters::redengine {
namespace {

constexpr DWORD kThreadAccess =
    THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT |
    THREAD_QUERY_INFORMATION | SYNCHRONIZE;

class OwnedHandles {
 public:
  ~OwnedHandles() {
    for (HANDLE handle : handles_) {
      CloseHandle(handle);
    }
  }

  void add(HANDLE handle) { handles_.push_back(handle); }

 private:
  std::vector<HANDLE> handles_;
};

bool threadHasExited(HANDLE thread) {
  DWORD exit_code = STILL_ACTIVE;
  return GetExitCodeThread(thread, &exit_code) != FALSE &&
         exit_code != STILL_ACTIVE;
}

bool enrollThread(DWORD thread_id, OwnedHandles* handles) {
  HANDLE thread = OpenThread(kThreadAccess, FALSE, thread_id);
  if (thread == nullptr) {
    return GetLastError() == ERROR_INVALID_PARAMETER;
  }
  handles->add(thread);
  if (threadHasExited(thread)) {
    return true;
  }
  if (DetourUpdateThread(thread) == NO_ERROR) {
    return true;
  }
  return threadHasExited(thread);
}

bool enrollCurrentProcessThreads(OwnedHandles* handles) {
  if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR) {
    return false;
  }

  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return false;
  }

  THREADENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  const DWORD process_id = GetCurrentProcessId();
  const DWORD current_thread_id = GetCurrentThreadId();
  BOOL has_entry = Thread32First(snapshot, &entry);
  if (!has_entry) {
    CloseHandle(snapshot);
    return false;
  }

  for (;;) {
    if (entry.th32OwnerProcessID == process_id &&
        entry.th32ThreadID != current_thread_id &&
        !enrollThread(entry.th32ThreadID, handles)) {
      CloseHandle(snapshot);
      return false;
    }
    entry.dwSize = sizeof(entry);
    has_entry = Thread32Next(snapshot, &entry);
    if (!has_entry) {
      break;
    }
  }
  const DWORD error = GetLastError();
  CloseHandle(snapshot);
  return error == ERROR_NO_MORE_FILES;
}

}  // namespace

bool commitDetourTransaction(
    const DetourChange* changes,
    std::size_t change_count,
    DetourOperation operation) {
  if (changes == nullptr || change_count == 0) {
    return false;
  }
  for (std::size_t index = 0; index < change_count; ++index) {
    if (changes[index].target == nullptr || *changes[index].target == nullptr ||
        changes[index].detour == nullptr) {
      return false;
    }
  }

  if (DetourTransactionBegin() != NO_ERROR) {
    return false;
  }
  OwnedHandles handles;
  if (!enrollCurrentProcessThreads(&handles)) {
    DetourTransactionAbort();
    return false;
  }

  for (std::size_t index = 0; index < change_count; ++index) {
    const LONG result = operation == DetourOperation::Attach
        ? DetourAttach(changes[index].target, changes[index].detour)
        : DetourDetach(changes[index].target, changes[index].detour);
    if (result != NO_ERROR) {
      DetourTransactionAbort();
      return false;
    }
  }
  if (DetourTransactionCommit() != NO_ERROR) {
    DetourTransactionAbort();
    return false;
  }
  return true;
}

}  // namespace vrclient::adapters::redengine

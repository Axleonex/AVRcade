#include "adapters/unreal/meccha_detour_transaction.h"

#include <tlhelp32.h>
#include <detours.h>

#include <vector>

namespace vrclient::adapters::unreal {
namespace {

constexpr DWORD kDetourThreadAccess =
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
  HANDLE thread = OpenThread(kDetourThreadAccess, FALSE, thread_id);
  if (thread == nullptr) {
    // ERROR_INVALID_PARAMETER means the enumerated thread no longer exists.
    // Access and other failures for a potentially live thread fail closed.
    return GetLastError() == ERROR_INVALID_PARAMETER;
  }
  handles->add(thread);
  if (threadHasExited(thread)) {
    return true;
  }
  if (DetourUpdateThread(thread) == NO_ERROR) {
    return true;
  }
  // Exiting between the liveness check and enrollment is a clean race.
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
  const DWORD enumeration_error = GetLastError();
  CloseHandle(snapshot);
  return enumeration_error == ERROR_NO_MORE_FILES;
}

}  // namespace

bool commitMecchaDetourTransaction(
    const MecchaDetourAttachment* attachments,
    std::size_t attachment_count) {
  if (attachments == nullptr || attachment_count == 0) {
    return false;
  }
  for (std::size_t i = 0; i < attachment_count; ++i) {
    if (attachments[i].target == nullptr ||
        *attachments[i].target == nullptr ||
        attachments[i].detour == nullptr) {
      return false;
    }
  }

  if (DetourTransactionBegin() != NO_ERROR) {
    return false;
  }

  OwnedHandles thread_handles;
  if (!enrollCurrentProcessThreads(&thread_handles)) {
    DetourTransactionAbort();
    return false;
  }
  for (std::size_t i = 0; i < attachment_count; ++i) {
    if (DetourAttach(attachments[i].target, attachments[i].detour) != NO_ERROR) {
      DetourTransactionAbort();
      return false;
    }
  }
  if (DetourTransactionCommit() != NO_ERROR) {
    // Commit normally ends the transaction even on failure. This defensive
    // abort is harmless if it has already been closed and prevents leakage if
    // a Detours implementation leaves it active.
    DetourTransactionAbort();
    return false;
  }
  return true;
}

}  // namespace vrclient::adapters::unreal

#pragma once

#include <windows.h>

#include <cstddef>

namespace vrclient::adapters::redengine {

enum class DetourOperation {
  Attach,
  Detach,
};

struct DetourChange {
  PVOID* target = nullptr;
  PVOID detour = nullptr;
};

// Applies a complete hook change atomically after enrolling the process's live
// threads. Detach uses the same target storage that was used for attachment.
bool commitDetourTransaction(
    const DetourChange* changes,
    std::size_t change_count,
    DetourOperation operation);

}  // namespace vrclient::adapters::redengine

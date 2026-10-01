#pragma once

#include <windows.h>

#include <cstddef>

namespace vrclient::adapters::unreal {

struct MecchaDetourAttachment {
  PVOID* target = nullptr;
  PVOID detour = nullptr;
};

// Applies all attachments atomically after enrolling every live thread observed
// in the current process. Returns false without leaving an active transaction.
bool commitMecchaDetourTransaction(
    const MecchaDetourAttachment* attachments,
    std::size_t attachment_count);

}  // namespace vrclient::adapters::unreal

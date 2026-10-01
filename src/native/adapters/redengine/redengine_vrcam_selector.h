#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace vrclient::adapters::redengine {

struct VrcamCandidateScore {
  std::uint64_t binds = 0;
  std::uint64_t clears = 0;
  std::uint64_t shader_read_transitions = 0;
};

std::size_t selectVrcamCandidate(
    std::span<const VrcamCandidateScore> candidates);

}  // namespace vrclient::adapters::redengine

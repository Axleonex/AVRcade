#include "adapters/redengine/redengine_vrcam_selector.h"

#include <algorithm>

namespace vrclient::adapters::redengine {

std::size_t selectVrcamCandidate(
    std::span<const VrcamCandidateScore> candidates) {
  if (candidates.empty()) {
    return candidates.size();
  }
  const auto same_cadence = [](std::uint64_t left, std::uint64_t right) {
    if (left == 0 || right == 0) {
      return false;
    }
    const std::uint64_t maximum = (std::max)(left, right);
    const std::uint64_t difference = left > right
        ? left - right
        : right - left;
    return static_cast<long double>(difference) * 100.0L <=
        static_cast<long double>(maximum) * 2.0L;
  };

  // VRCAM publishes a lightly-bound texture alongside a heavily-bound render
  // surface at the same completed-frame cadence. Match that pair explicitly.
  // A zero-bind peer is only cleared/copied bookkeeping and has proven to be
  // an all-black intermediate in live Cyberpunk captures.
  std::size_t paired_selection = candidates.size();
  long double paired_bind_ratio = 0.0L;
  for (std::size_t candidate_index = 0;
       candidate_index < candidates.size(); ++candidate_index) {
    const auto& candidate = candidates[candidate_index];
    if (candidate.binds == 0 || candidate.clears == 0 ||
        candidate.shader_read_transitions == 0 ||
        candidate.binds > candidate.shader_read_transitions ||
        !same_cadence(
            candidate.clears, candidate.shader_read_transitions)) {
      continue;
    }
    bool has_render_peer = false;
    for (std::size_t peer_index = 0; peer_index < candidates.size();
         ++peer_index) {
      if (peer_index == candidate_index) {
        continue;
      }
      const auto& peer = candidates[peer_index];
      if (same_cadence(
              candidate.shader_read_transitions,
              peer.shader_read_transitions) &&
          same_cadence(candidate.clears, peer.clears) &&
          peer.binds / 8 >= candidate.binds &&
          peer.binds / 4 >= peer.shader_read_transitions) {
        has_render_peer = true;
        break;
      }
    }
    if (!has_render_peer) {
      continue;
    }
    const long double bind_ratio =
        static_cast<long double>(candidate.binds) /
        static_cast<long double>(candidate.shader_read_transitions);
    if (paired_selection == candidates.size() ||
        bind_ratio <= paired_bind_ratio) {
      paired_selection = candidate_index;
      paired_bind_ratio = bind_ratio;
    }
  }
  if (paired_selection < candidates.size()) {
    return paired_selection;
  }

  // Controlled/single-resource paths have no peer. Retain a conservative
  // fallback, but never select a zero-bind intermediate.
  std::uint64_t maximum_transitions = 0;
  for (const auto& candidate : candidates) {
    if (candidate.binds != 0) {
      maximum_transitions = (std::max)(
          maximum_transitions, candidate.shader_read_transitions);
    }
  }
  if (maximum_transitions == 0) {
    return candidates.size();
  }
  std::size_t selected = candidates.size();
  std::uint64_t selected_binds = UINT64_MAX;
  for (std::size_t index = 0; index < candidates.size(); ++index) {
    const auto& candidate = candidates[index];
    if (candidate.shader_read_transitions * 100 <
            maximum_transitions * 95 ||
        candidate.clears == 0 || candidate.binds == 0) {
      continue;
    }
    if (selected == candidates.size() || candidate.binds <= selected_binds) {
      selected = index;
      selected_binds = candidate.binds;
    }
  }
  return selected;
}

}  // namespace vrclient::adapters::redengine

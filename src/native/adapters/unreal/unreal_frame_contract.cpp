#include "adapters/unreal/unreal_frame_contract.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace vrclient::adapters::unreal {
namespace {

bool selectorMatches(
    const UnrealObjectSelector& selector,
    const UnrealObjectCandidate& candidate) {
  if (selector.require_active && !candidate.active) {
    return false;
  }
  if (!selector.object_path.empty() &&
      selector.object_path != candidate.object_path) {
    return false;
  }
  if (!selector.component_name.empty() &&
      selector.component_name != candidate.component_name) {
    return false;
  }
  return true;
}

bool finiteMatrix(const UnrealMatrix4& matrix) {
  return std::all_of(
      std::begin(matrix.values),
      std::end(matrix.values),
      [](float value) { return std::isfinite(value); });
}

}  // namespace

UnrealDiscoveryResult selectUniqueUnrealObject(
    const UnrealObjectSelector& selector,
    const std::vector<UnrealObjectCandidate>& candidates,
    std::uint64_t* object_id) {
  if (object_id == nullptr ||
      (selector.object_path.empty() && selector.component_name.empty())) {
    return UnrealDiscoveryResult::InvalidSelector;
  }

  std::uint64_t match = 0;
  std::size_t match_count = 0;
  for (const auto& candidate : candidates) {
    if (selectorMatches(selector, candidate)) {
      match = candidate.object_id;
      ++match_count;
    }
  }
  if (match_count == 0) {
    return UnrealDiscoveryResult::Missing;
  }
  if (match_count != 1) {
    return UnrealDiscoveryResult::Ambiguous;
  }
  *object_id = match;
  return UnrealDiscoveryResult::Found;
}

UnrealSubmissionResult validateUnrealFrameSubmission(
    const UnrealFrameSubmission& submission) {
  if (submission.predicted_display_time_ns <= 0) {
    return UnrealSubmissionResult::InvalidFrame;
  }
  if (submission.renderer != RendererApi::D3D12) {
    return UnrealSubmissionResult::UnsupportedRenderer;
  }
  if (submission.eyes[0].color_target == nullptr ||
      submission.eyes[1].color_target == nullptr) {
    return UnrealSubmissionResult::MissingEyeTarget;
  }
  if (submission.eyes[0].color_target == submission.eyes[1].color_target) {
    return UnrealSubmissionResult::SharedEyeTarget;
  }
  for (const auto& eye : submission.eyes) {
    if (eye.width == 0 || eye.height == 0) {
      return UnrealSubmissionResult::InvalidDimensions;
    }
    if (eye.color_format == 0) {
      return UnrealSubmissionResult::InvalidFormat;
    }
    if (!finiteMatrix(eye.view) || !finiteMatrix(eye.projection)) {
      return UnrealSubmissionResult::NonFiniteMatrix;
    }
  }
  return UnrealSubmissionResult::Ready;
}

}  // namespace vrclient::adapters::unreal

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace vrclient::adapters::unreal {

struct UnrealPeSectionView {
  std::uint32_t rva = 0;
  std::vector<std::uint8_t> bytes;
};

struct UnrealRuntimeFunctionRange {
  std::uint32_t begin_rva = 0;
  std::uint32_t end_rva = 0;
};

struct UnrealViewSeamImage {
  UnrealPeSectionView text;
  UnrealPeSectionView rdata;
  std::vector<UnrealRuntimeFunctionRange> runtime_functions;
};

enum class UnrealViewSeamLocateResult {
  Ready,
  InvalidImage,
  FingerprintMismatch,
  MissingPrimaryAnchor,
  MissingSecondaryAnchor,
  MissingCandidate,
  AmbiguousCandidate,
  PinnedRvaInvalid,
  SignatureMismatch,
  ReferenceCountMismatch,
};

struct UnrealPinnedViewSeam {
  std::uint32_t constructor_rva = 0;
  std::vector<std::uint8_t> prologue;
  std::size_t expected_direct_call_references = 0;
  std::uint32_t primary_anchor_window_bytes = 0;
};

struct UnrealViewSeamLocation {
  UnrealViewSeamLocateResult result = UnrealViewSeamLocateResult::InvalidImage;
  std::uint32_t constructor_rva = 0;
  std::size_t candidate_count = 0;
  std::size_t primary_anchor_count = 0;
  std::size_t secondary_anchor_count = 0;
  std::size_t primary_reference_count = 0;
  std::size_t secondary_reference_count = 0;
  std::size_t root_count = 0;
  std::size_t decoded_instruction_count = 0;
  std::size_t direct_call_reference_count = 0;
};

enum class UnrealInstructionFlow {
  Continue,
  Call,
  ConditionalBranch,
  UnconditionalBranch,
  Return,
};

struct UnrealDecodedInstruction {
  std::size_t length = 0;
  UnrealInstructionFlow flow = UnrealInstructionFlow::Continue;
  bool has_target = false;
  std::uint32_t target_rva = 0;
};

using UnrealInstructionDecoder = bool (*)(
    const std::uint8_t* instruction,
    std::size_t available,
    std::uint32_t instruction_rva,
    UnrealDecodedInstruction* output,
    void* context);

UnrealViewSeamLocation locateUnrealViewConstructor(
    const UnrealViewSeamImage& image,
    std::string_view expected_sha256,
    std::string_view observed_sha256);

UnrealViewSeamLocation locateUnrealViewConstructorExhaustive(
    const UnrealViewSeamImage& image,
    std::string_view expected_sha256,
    std::string_view observed_sha256,
    UnrealInstructionDecoder decoder,
    void* decoder_context = nullptr,
    std::size_t maximum_instruction_count = 1000);

UnrealViewSeamLocation validatePinnedUnrealViewConstructor(
    const UnrealViewSeamImage& image,
    std::string_view expected_sha256,
    std::string_view observed_sha256,
    const UnrealPinnedViewSeam& pinned);

}  // namespace vrclient::adapters::unreal

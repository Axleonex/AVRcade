#include "adapters/unreal/unreal_view_seam_locator.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <deque>
#include <set>

namespace vrclient::adapters::unreal {
namespace {

constexpr std::u16string_view kPrimaryAnchor = u"r.TranslucentSortPolicy";
constexpr std::u16string_view kSecondaryAnchor = u"vr.InstancedStereo";

bool validSha256(std::string_view value) {
  return value.size() == 64 &&
      std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isxdigit(character) != 0;
      });
}

bool equalAsciiInsensitive(std::string_view left, std::string_view right) {
  return left.size() == right.size() &&
      std::equal(left.begin(), left.end(), right.begin(),
                 [](unsigned char a, unsigned char b) {
                   return std::tolower(a) == std::tolower(b);
                 });
}

std::vector<std::uint32_t> findWideStrings(
    const UnrealPeSectionView& section,
    std::u16string_view value) {
  std::vector<std::uint32_t> matches;
  const std::size_t byte_count = (value.size() + 1) * sizeof(char16_t);
  if (section.rva == 0 || value.empty() || section.bytes.size() < byte_count) {
    return matches;
  }
  for (std::size_t offset = 0; offset + byte_count <= section.bytes.size(); ++offset) {
    bool matches_value = true;
    for (std::size_t index = 0; index < value.size(); ++index) {
      const char16_t observed = static_cast<char16_t>(
          section.bytes[offset + index * 2] |
          (static_cast<unsigned>(section.bytes[offset + index * 2 + 1]) << 8));
      if (observed != value[index]) {
        matches_value = false;
        break;
      }
    }
    if (matches_value && section.bytes[offset + value.size() * 2] == 0 &&
        section.bytes[offset + value.size() * 2 + 1] == 0) {
      matches.push_back(section.rva + static_cast<std::uint32_t>(offset));
    }
  }
  return matches;
}

std::vector<std::uint32_t> findRipRelativeReferences(
    const UnrealPeSectionView& text,
    const std::vector<std::uint32_t>& targets) {
  std::vector<std::uint32_t> references;
  if (text.rva == 0 || targets.empty()) {
    return references;
  }
  for (std::size_t offset = 0; offset + 6 <= text.bytes.size(); ++offset) {
    std::size_t opcode_offset = offset;
    if ((text.bytes[opcode_offset] & 0xF0U) == 0x40U) {
      ++opcode_offset;
    } else if (offset > 0 && (text.bytes[offset - 1] & 0xF0U) == 0x40U) {
      // The preceding offset was already considered as this instruction's REX
      // prefix. Do not count the same RIP-relative operand twice.
      continue;
    }
    if (opcode_offset + 6 > text.bytes.size()) {
      continue;
    }
    const std::uint8_t opcode = text.bytes[opcode_offset];
    if (opcode != 0x8D && opcode != 0x8B) {
      continue;
    }
    const std::uint8_t modrm = text.bytes[opcode_offset + 1];
    if ((modrm & 0xC7U) != 0x05U) {
      continue;
    }
    std::int32_t displacement = 0;
    std::memcpy(&displacement, text.bytes.data() + opcode_offset + 2,
                sizeof(displacement));
    const std::size_t instruction_length = opcode_offset - offset + 6;
    const std::int64_t next_rva = static_cast<std::int64_t>(text.rva) +
        static_cast<std::int64_t>(offset + instruction_length);
    const std::int64_t target = next_rva + displacement;
    if (target >= 0 && target <= UINT32_MAX &&
        std::find(targets.begin(), targets.end(),
                  static_cast<std::uint32_t>(target)) != targets.end()) {
      references.push_back(text.rva + static_cast<std::uint32_t>(offset));
    }
  }
  std::sort(references.begin(), references.end());
  references.erase(std::unique(references.begin(), references.end()), references.end());
  return references;
}

std::vector<std::uint32_t> findDirectCalls(
    const UnrealPeSectionView& text,
    std::uint32_t target_rva) {
  std::vector<std::uint32_t> references;
  if (text.rva == 0) {
    return references;
  }
  for (std::size_t offset = 0; offset + 5 <= text.bytes.size(); ++offset) {
    if (text.bytes[offset] != 0xE8) {
      continue;
    }
    std::int32_t displacement = 0;
    std::memcpy(&displacement, text.bytes.data() + offset + 1,
                sizeof(displacement));
    const std::int64_t resolved = static_cast<std::int64_t>(text.rva) +
        static_cast<std::int64_t>(offset) + 5 + displacement;
    if (resolved == target_rva) {
      references.push_back(text.rva + static_cast<std::uint32_t>(offset));
    }
  }
  return references;
}

bool containsReference(
    const UnrealRuntimeFunctionRange& function,
    const std::vector<std::uint32_t>& references) {
  return std::any_of(references.begin(), references.end(),
                     [&](std::uint32_t reference) {
                       return reference >= function.begin_rva &&
                           reference < function.end_rva;
                     });
}

}  // namespace

UnrealViewSeamLocation locateUnrealViewConstructor(
    const UnrealViewSeamImage& image,
    std::string_view expected_sha256,
    std::string_view observed_sha256) {
  UnrealViewSeamLocation result{};
  if (!validSha256(expected_sha256) || !validSha256(observed_sha256) ||
      image.text.rva == 0 || image.text.bytes.empty() || image.rdata.rva == 0 ||
      image.rdata.bytes.empty() || image.runtime_functions.empty()) {
    return result;
  }
  if (!equalAsciiInsensitive(expected_sha256, observed_sha256)) {
    result.result = UnrealViewSeamLocateResult::FingerprintMismatch;
    return result;
  }

  const auto primary_targets = findWideStrings(image.rdata, kPrimaryAnchor);
  result.primary_anchor_count = primary_targets.size();
  if (primary_targets.empty()) {
    result.result = UnrealViewSeamLocateResult::MissingPrimaryAnchor;
    return result;
  }
  const auto secondary_targets = findWideStrings(image.rdata, kSecondaryAnchor);
  result.secondary_anchor_count = secondary_targets.size();
  if (secondary_targets.empty()) {
    result.result = UnrealViewSeamLocateResult::MissingSecondaryAnchor;
    return result;
  }
  const auto primary_references =
      findRipRelativeReferences(image.text, primary_targets);
  const auto secondary_references =
      findRipRelativeReferences(image.text, secondary_targets);
  result.primary_reference_count = primary_references.size();
  result.secondary_reference_count = secondary_references.size();

  const std::uint64_t text_begin = image.text.rva;
  const std::uint64_t text_end = text_begin + image.text.bytes.size();
  std::set<std::uint32_t> candidates;
  for (const auto& function : image.runtime_functions) {
    if (function.begin_rva < text_begin || function.end_rva <= function.begin_rva ||
        function.end_rva > text_end) {
      continue;
    }
    if (containsReference(function, primary_references) &&
        containsReference(function, secondary_references)) {
      candidates.insert(function.begin_rva);
    }
  }
  result.candidate_count = candidates.size();
  if (candidates.empty()) {
    result.result = UnrealViewSeamLocateResult::MissingCandidate;
  } else if (candidates.size() != 1) {
    result.result = UnrealViewSeamLocateResult::AmbiguousCandidate;
  } else {
    result.result = UnrealViewSeamLocateResult::Ready;
    result.constructor_rva = *candidates.begin();
  }
  return result;
}

UnrealViewSeamLocation locateUnrealViewConstructorExhaustive(
    const UnrealViewSeamImage& image,
    std::string_view expected_sha256,
    std::string_view observed_sha256,
    UnrealInstructionDecoder decoder,
    void* decoder_context,
    std::size_t maximum_instruction_count) {
  const UnrealViewSeamLocation direct = locateUnrealViewConstructor(
      image, expected_sha256, observed_sha256);
  if (direct.result != UnrealViewSeamLocateResult::MissingCandidate) {
    return direct;
  }
  UnrealViewSeamLocation result{};
  result.primary_anchor_count = direct.primary_anchor_count;
  result.secondary_anchor_count = direct.secondary_anchor_count;
  result.primary_reference_count = direct.primary_reference_count;
  result.secondary_reference_count = direct.secondary_reference_count;
  if (decoder == nullptr || maximum_instruction_count == 0) {
    return result;
  }

  const auto primary_targets = findWideStrings(image.rdata, kPrimaryAnchor);
  const auto secondary_targets = findWideStrings(image.rdata, kSecondaryAnchor);
  const auto primary_references =
      findRipRelativeReferences(image.text, primary_targets);
  const auto secondary_references =
      findRipRelativeReferences(image.text, secondary_targets);
  if (primary_references.empty() || secondary_references.empty()) {
    result.result = UnrealViewSeamLocateResult::MissingCandidate;
    return result;
  }

  const std::uint64_t text_begin = image.text.rva;
  const std::uint64_t text_end = text_begin + image.text.bytes.size();
  std::set<std::uint32_t> roots;
  for (const auto& function : image.runtime_functions) {
    if (function.begin_rva >= text_begin && function.end_rva <= text_end &&
        function.begin_rva < function.end_rva &&
        containsReference(function, primary_references)) {
      roots.insert(function.begin_rva);
    }
  }
  result.root_count = roots.size();

  std::set<std::uint32_t> candidates;
  for (const std::uint32_t root : roots) {
    std::deque<std::uint32_t> blocks{root};
    std::set<std::uint32_t> visited;
    std::size_t decoded_count = 0;
    bool found_secondary = false;
    while (!blocks.empty() && decoded_count < maximum_instruction_count &&
           !found_secondary) {
      std::uint32_t cursor = blocks.front();
      blocks.pop_front();
      while (cursor >= text_begin && cursor < text_end &&
             decoded_count < maximum_instruction_count) {
        if (std::find(secondary_references.begin(), secondary_references.end(),
                      cursor) != secondary_references.end()) {
          found_secondary = true;
          break;
        }
        if (!visited.insert(cursor).second) {
          break;
        }
        const std::size_t offset = cursor - image.text.rva;
        UnrealDecodedInstruction instruction{};
        if (!decoder(image.text.bytes.data() + offset,
                     image.text.bytes.size() - offset,
                     cursor,
                     &instruction,
                     decoder_context) ||
            instruction.length == 0 ||
            instruction.length > image.text.bytes.size() - offset) {
          break;
        }
        ++decoded_count;
        ++result.decoded_instruction_count;
        if (instruction.has_target && instruction.target_rva >= text_begin &&
            instruction.target_rva < text_end) {
          blocks.push_back(instruction.target_rva);
        }
        const std::uint64_t next =
            static_cast<std::uint64_t>(cursor) + instruction.length;
        if (instruction.flow == UnrealInstructionFlow::Return ||
            instruction.flow == UnrealInstructionFlow::UnconditionalBranch ||
            next >= text_end) {
          break;
        }
        cursor = static_cast<std::uint32_t>(next);
      }
    }
    if (found_secondary) {
      candidates.insert(root);
    }
  }

  result.candidate_count = candidates.size();
  if (candidates.empty()) {
    result.result = UnrealViewSeamLocateResult::MissingCandidate;
  } else if (candidates.size() != 1) {
    result.result = UnrealViewSeamLocateResult::AmbiguousCandidate;
  } else {
    result.result = UnrealViewSeamLocateResult::Ready;
    result.constructor_rva = *candidates.begin();
  }
  return result;
}

UnrealViewSeamLocation validatePinnedUnrealViewConstructor(
    const UnrealViewSeamImage& image,
    std::string_view expected_sha256,
    std::string_view observed_sha256,
    const UnrealPinnedViewSeam& pinned) {
  UnrealViewSeamLocation result{};
  if (!validSha256(expected_sha256) || !validSha256(observed_sha256) ||
      image.text.rva == 0 || image.text.bytes.empty() || image.rdata.rva == 0 ||
      image.rdata.bytes.empty() || pinned.constructor_rva == 0 ||
      pinned.prologue.empty() || pinned.expected_direct_call_references == 0) {
    return result;
  }
  if (!equalAsciiInsensitive(expected_sha256, observed_sha256)) {
    result.result = UnrealViewSeamLocateResult::FingerprintMismatch;
    return result;
  }

  const auto primary_targets = findWideStrings(image.rdata, kPrimaryAnchor);
  const auto secondary_targets = findWideStrings(image.rdata, kSecondaryAnchor);
  result.primary_anchor_count = primary_targets.size();
  result.secondary_anchor_count = secondary_targets.size();
  if (primary_targets.empty()) {
    result.result = UnrealViewSeamLocateResult::MissingPrimaryAnchor;
    return result;
  }
  if (secondary_targets.empty()) {
    result.result = UnrealViewSeamLocateResult::MissingSecondaryAnchor;
    return result;
  }

  const auto text_begin = static_cast<std::uint64_t>(image.text.rva);
  const auto text_end = text_begin + image.text.bytes.size();
  const auto signature_end = static_cast<std::uint64_t>(pinned.constructor_rva) +
      pinned.prologue.size();
  if (pinned.constructor_rva < text_begin || signature_end > text_end) {
    result.result = UnrealViewSeamLocateResult::PinnedRvaInvalid;
    return result;
  }
  const auto signature_offset = static_cast<std::size_t>(
      pinned.constructor_rva - image.text.rva);
  if (!std::equal(pinned.prologue.begin(), pinned.prologue.end(),
                  image.text.bytes.begin() + signature_offset)) {
    result.result = UnrealViewSeamLocateResult::SignatureMismatch;
    return result;
  }

  const auto primary_references =
      findRipRelativeReferences(image.text, primary_targets);
  const auto secondary_references =
      findRipRelativeReferences(image.text, secondary_targets);
  result.primary_reference_count = primary_references.size();
  result.secondary_reference_count = secondary_references.size();
  if (primary_references.empty() || secondary_references.empty()) {
    result.result = UnrealViewSeamLocateResult::MissingCandidate;
    return result;
  }
  if (pinned.primary_anchor_window_bytes != 0) {
    const std::uint64_t window_end =
        static_cast<std::uint64_t>(pinned.constructor_rva) +
        pinned.primary_anchor_window_bytes;
    const bool primary_in_window = std::any_of(
        primary_references.begin(), primary_references.end(),
        [&](std::uint32_t reference) {
          return reference >= pinned.constructor_rva && reference < window_end;
        });
    if (!primary_in_window) {
      result.result = UnrealViewSeamLocateResult::MissingCandidate;
      return result;
    }
  }

  result.direct_call_reference_count =
      findDirectCalls(image.text, pinned.constructor_rva).size();
  if (result.direct_call_reference_count !=
      pinned.expected_direct_call_references) {
    result.result = UnrealViewSeamLocateResult::ReferenceCountMismatch;
    return result;
  }
  result.result = UnrealViewSeamLocateResult::Ready;
  result.constructor_rva = pinned.constructor_rva;
  result.candidate_count = 1;
  return result;
}

}  // namespace vrclient::adapters::unreal

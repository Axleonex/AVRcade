#include "adapters/unreal/meccha_camera_pin.h"
#include "adapters/unreal/unreal_view_seam_locator.h"
#include "adapters/unreal/unreal_view_seam_pe.h"

#include <iomanip>
#include <iostream>
#include <string>

namespace {

const char* resultName(
    vrclient::adapters::unreal::UnrealViewSeamLocateResult result) {
  using Result = vrclient::adapters::unreal::UnrealViewSeamLocateResult;
  switch (result) {
    case Result::Ready: return "ready";
    case Result::InvalidImage: return "invalid_image";
    case Result::FingerprintMismatch: return "fingerprint_mismatch";
    case Result::MissingPrimaryAnchor: return "missing_primary_anchor";
    case Result::MissingSecondaryAnchor: return "missing_secondary_anchor";
    case Result::MissingCandidate: return "missing_candidate";
    case Result::AmbiguousCandidate: return "ambiguous_candidate";
    case Result::PinnedRvaInvalid: return "pinned_rva_invalid";
    case Result::SignatureMismatch: return "signature_mismatch";
    case Result::ReferenceCountMismatch: return "reference_count_mismatch";
  }
  return "unknown";
}

bool narrowAscii(const wchar_t* value, std::string* output) {
  if (value == nullptr || output == nullptr) {
    return false;
  }
  output->clear();
  for (; *value != L'\0'; ++value) {
    if (*value < 0 || *value > 0x7F) {
      output->clear();
      return false;
    }
    output->push_back(static_cast<char>(*value));
  }
  return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  using namespace vrclient::adapters::unreal;
  if (argc != 3) {
    std::wcerr << L"usage: vrclient_unreal_view_seam_probe "
                  L"<shipping.exe> <observed-sha256>\n";
    return 2;
  }
  UnrealViewSeamImage image{};
  std::string error;
  if (loadUnrealViewSeamImageFromFile(argv[1], &image, &error) !=
      UnrealViewSeamPeResult::Ready) {
    std::cerr << "pe_error=" << error << "\n";
    return 3;
  }
  std::string observed_hash;
  if (!narrowAscii(argv[2], &observed_hash)) {
    std::cerr << "result=invalid_hash_encoding\n";
    return 4;
  }
  const auto discovered = locateUnrealViewConstructor(
      image, observed_hash, observed_hash);
  const auto location = validatePinnedUnrealViewConstructor(
      image, mecchaCameraExecutableSha256(), observed_hash,
      mecchaCameraViewSeam());
  std::cout << "discovery_result=" << resultName(discovered.result) << "\n"
            << "discovered_candidate_count=" << discovered.candidate_count << "\n"
            << "discovered_primary_anchor_count="
            << discovered.primary_anchor_count << "\n"
            << "discovered_secondary_anchor_count="
            << discovered.secondary_anchor_count << "\n"
            << "discovered_primary_reference_count="
            << discovered.primary_reference_count << "\n"
            << "discovered_secondary_reference_count="
            << discovered.secondary_reference_count << "\n"
            << "discovered_constructor_rva=0x" << std::hex << std::uppercase
            << discovered.constructor_rva << std::dec << "\n"
            << "result=" << resultName(location.result) << "\n"
            << "build_id=" << mecchaCameraBuildId() << "\n"
            << "candidate_count=" << location.candidate_count << "\n"
            << "primary_anchor_count=" << location.primary_anchor_count << "\n"
            << "secondary_anchor_count=" << location.secondary_anchor_count << "\n"
            << "primary_reference_count=" << location.primary_reference_count << "\n"
            << "secondary_reference_count=" << location.secondary_reference_count << "\n"
            << "direct_call_reference_count="
            << location.direct_call_reference_count << "\n"
            << "constructor_rva=0x" << std::hex << std::uppercase
            << location.constructor_rva << "\n";
  return location.result == UnrealViewSeamLocateResult::Ready ? 0 : 5;
}

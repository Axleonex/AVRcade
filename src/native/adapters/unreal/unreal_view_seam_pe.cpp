#include "adapters/unreal/unreal_view_seam_pe.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <vector>

namespace vrclient::adapters::unreal {
namespace {

UnrealViewSeamPeResult fail(
    UnrealViewSeamPeResult result,
    std::string_view message,
    std::string* error) {
  if (error != nullptr) {
    *error = message;
  }
  return result;
}

bool inBounds(std::size_t offset, std::size_t length, std::size_t size) {
  return offset <= size && length <= size - offset;
}

bool sectionNamed(const IMAGE_SECTION_HEADER& section, const char* name) {
  char observed[IMAGE_SIZEOF_SHORT_NAME + 1]{};
  std::memcpy(observed, section.Name, IMAGE_SIZEOF_SHORT_NAME);
  return std::strcmp(observed, name) == 0;
}

std::optional<std::size_t> rvaToFileOffset(
    std::uint32_t rva,
    const IMAGE_SECTION_HEADER* sections,
    std::size_t section_count,
    std::size_t file_size) {
  for (std::size_t index = 0; index < section_count; ++index) {
    const auto& section = sections[index];
    const std::uint64_t begin = section.VirtualAddress;
    const std::uint64_t extent =
        std::max(section.Misc.VirtualSize, section.SizeOfRawData);
    const std::uint64_t end = begin + extent;
    if (rva < begin || rva >= end) {
      continue;
    }
    const std::uint64_t offset = section.PointerToRawData + (rva - begin);
    if (offset < file_size) {
      return static_cast<std::size_t>(offset);
    }
  }
  return std::nullopt;
}

}  // namespace

UnrealViewSeamPeResult loadUnrealViewSeamImageFromFile(
    const std::filesystem::path& path,
    UnrealViewSeamImage* output,
    std::string* error) {
  if (output == nullptr) {
    return fail(UnrealViewSeamPeResult::InvalidOutput,
                "output image is null", error);
  }
  *output = {};
  if (error != nullptr) {
    error->clear();
  }

  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return fail(UnrealViewSeamPeResult::FileReadFailed,
                "could not open PE file", error);
  }
  std::vector<std::uint8_t> file{
      std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
  if (file.size() < sizeof(IMAGE_DOS_HEADER)) {
    return fail(UnrealViewSeamPeResult::InvalidDosHeader,
                "PE file is smaller than IMAGE_DOS_HEADER", error);
  }
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
      !inBounds(static_cast<std::size_t>(dos->e_lfanew),
                sizeof(IMAGE_NT_HEADERS64), file.size())) {
    return fail(UnrealViewSeamPeResult::InvalidDosHeader,
                "invalid DOS header or e_lfanew", error);
  }
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
      file.data() + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE ||
      nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
    return fail(UnrealViewSeamPeResult::InvalidNtHeaders,
                "invalid PE32+ NT headers", error);
  }
  if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) {
    return fail(UnrealViewSeamPeResult::UnsupportedArchitecture,
                "camera provider supports only x64 PE images", error);
  }

  const std::size_t sections_offset = static_cast<std::size_t>(dos->e_lfanew) +
      sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) +
      nt->FileHeader.SizeOfOptionalHeader;
  const std::size_t section_count = nt->FileHeader.NumberOfSections;
  if (!inBounds(sections_offset,
                section_count * sizeof(IMAGE_SECTION_HEADER), file.size())) {
    return fail(UnrealViewSeamPeResult::InvalidNtHeaders,
                "section table exceeds PE file", error);
  }
  const auto* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(
      file.data() + sections_offset);
  for (std::size_t index = 0; index < section_count; ++index) {
    const auto& section = sections[index];
    if (!sectionNamed(section, ".text") && !sectionNamed(section, ".rdata")) {
      continue;
    }
    const std::size_t offset = section.PointerToRawData;
    const std::size_t size = section.SizeOfRawData;
    if (!inBounds(offset, size, file.size())) {
      return fail(UnrealViewSeamPeResult::InvalidNtHeaders,
                  "section bytes exceed PE file", error);
    }
    UnrealPeSectionView* destination = sectionNamed(section, ".text")
        ? &output->text
        : &output->rdata;
    destination->rva = section.VirtualAddress;
    destination->bytes.assign(file.begin() + offset, file.begin() + offset + size);
  }
  if (output->text.bytes.empty()) {
    return fail(UnrealViewSeamPeResult::MissingTextSection,
                "PE image has no readable .text section", error);
  }
  if (output->rdata.bytes.empty()) {
    return fail(UnrealViewSeamPeResult::MissingRdataSection,
                "PE image has no readable .rdata section", error);
  }

  const auto& exception =
      nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
  const auto exception_offset =
      rvaToFileOffset(exception.VirtualAddress, sections, section_count, file.size());
  if (!exception_offset.has_value() || exception.Size < sizeof(RUNTIME_FUNCTION) ||
      !inBounds(*exception_offset, exception.Size, file.size())) {
    return fail(UnrealViewSeamPeResult::MissingRuntimeFunctions,
                "PE image has no readable x64 exception table", error);
  }
  const std::size_t function_count = exception.Size / sizeof(RUNTIME_FUNCTION);
  const auto* functions = reinterpret_cast<const RUNTIME_FUNCTION*>(
      file.data() + *exception_offset);
  output->runtime_functions.reserve(function_count);
  for (std::size_t index = 0; index < function_count; ++index) {
    if (functions[index].BeginAddress < functions[index].EndAddress) {
      output->runtime_functions.push_back(
          {functions[index].BeginAddress, functions[index].EndAddress});
    }
  }
  if (output->runtime_functions.empty()) {
    return fail(UnrealViewSeamPeResult::MissingRuntimeFunctions,
                "x64 exception table contains no function ranges", error);
  }
  return UnrealViewSeamPeResult::Ready;
}

}  // namespace vrclient::adapters::unreal

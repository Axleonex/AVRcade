#include "tooling/hookdisc/inspection.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <string>

namespace vrclient::tooling::hookdisc {
namespace {

std::string wideToUtf8(const wchar_t* value) {
  if (value == nullptr) {
    return std::string();
  }
  const int len = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
  if (len <= 1) {
    return std::string();
  }
  std::string out(static_cast<std::size_t>(len - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value, -1, out.data(), len, nullptr, nullptr);
  return out;
}

std::string toLower(std::string_view value) {
  std::string out(value);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

void logEvent(
    diagnostics::AsyncLogger* logger,
    diagnostics::Severity severity,
    std::string_view event,
    std::initializer_list<diagnostics::LogField> fields) {
  if (logger != nullptr) {
    logger->log(severity, event, fields);
  }
}

}  // namespace

ModuleEnumeration enumerateModules(
    std::uint32_t process_id,
    diagnostics::AsyncLogger* logger) {
  ModuleEnumeration result;
  const DWORD pid = (process_id == 0) ? GetCurrentProcessId() : process_id;
  result.process_id = static_cast<std::uint32_t>(pid);

  HANDLE snapshot = CreateToolhelp32Snapshot(
      TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
  if (snapshot == INVALID_HANDLE_VALUE) {
    result.message = "CreateToolhelp32Snapshot failed (error " +
                     std::to_string(GetLastError()) + ")";
    logEvent(logger, diagnostics::Severity::Warning, "hookdisc_module_enum_failed",
             {{"process_id", std::to_string(pid)}, {"error", result.message}});
    return result;
  }

  MODULEENTRY32W entry;
  std::memset(&entry, 0, sizeof(entry));
  entry.dwSize = sizeof(entry);

  if (Module32FirstW(snapshot, &entry)) {
    do {
      ModuleInfo info;
      info.name = wideToUtf8(entry.szModule);
      info.path = wideToUtf8(entry.szExePath);
      info.base = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
      info.size = static_cast<std::size_t>(entry.modBaseSize);
      result.modules.push_back(std::move(info));
    } while (Module32NextW(snapshot, &entry));
    result.ok = true;
  } else {
    result.message = "Module32FirstW failed (error " +
                     std::to_string(GetLastError()) + ")";
  }

  CloseHandle(snapshot);

  if (result.ok) {
    logEvent(logger, diagnostics::Severity::Info, "hookdisc_module_enumerated",
             {{"process_id", std::to_string(pid)},
              {"module_count", std::to_string(result.modules.size())}});
  }
  return result;
}

ModuleEnumeration enumerateCurrentProcessModules(diagnostics::AsyncLogger* logger) {
  return enumerateModules(0, logger);
}

const ModuleInfo* findModule(const ModuleEnumeration& modules, std::string_view name) {
  const std::string needle = toLower(name);
  for (const ModuleInfo& module : modules.modules) {
    if (toLower(module.name) == needle) {
      return &module;
    }
  }
  return nullptr;
}

ExportEnumeration enumerateCurrentProcessExports(
    const ModuleInfo& module,
    diagnostics::AsyncLogger* logger) {
  ExportEnumeration result;
  result.module_name = module.name;

  if (module.base == 0 || module.size == 0) {
    result.message = "module has no mapped range";
    return result;
  }

  // The OS loader does NOT validate export-table contents, and the target use
  // case is inspecting game processes full of third-party/packed DLLs, so every
  // field below is treated as untrusted. Every RVA-derived pointer and string is
  // clamped to [base, base + size); a malformed module returns an error, never
  // an access violation.
  const BYTE* const image = reinterpret_cast<const BYTE*>(module.base);
  const std::size_t image_size = module.size;

  // True iff [offset, offset+len) lies fully inside the mapped image (no overflow).
  const auto in_image = [image_size](std::size_t offset, std::size_t len) -> bool {
    return offset <= image_size && len <= (image_size - offset);
  };
  // Read a NUL-terminated ASCII string starting at an image RVA, clamped to the
  // image and to a sane maximum length. Returns false if it runs off the image
  // (i.e. no NUL was found inside the mapping).
  const auto read_image_string =
      [image, image_size](DWORD rva, std::string& out) -> bool {
    if (rva >= image_size) {
      return false;
    }
    const char* const p = reinterpret_cast<const char*>(image + rva);
    const std::size_t max_len = image_size - rva;
    constexpr std::size_t kMaxNameLen = 8192;
    std::size_t n = 0;
    while (n < max_len && n < kMaxNameLen && p[n] != '\0') {
      ++n;
    }
    if (n >= max_len) {
      return false;  // not NUL-terminated inside the mapped image
    }
    out.assign(p, n);
    return true;
  };

  if (!in_image(0, sizeof(IMAGE_DOS_HEADER))) {
    result.message = "image too small for DOS header";
    return result;
  }
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
    result.message = "not a PE image (bad DOS signature)";
    return result;
  }
  const LONG e_lfanew = dos->e_lfanew;
  if (e_lfanew < 0 ||
      !in_image(static_cast<std::size_t>(e_lfanew), sizeof(IMAGE_NT_HEADERS))) {
    result.message = "NT header offset (e_lfanew) is out of range";
    return result;
  }
  const auto* nt =
      reinterpret_cast<const IMAGE_NT_HEADERS*>(image + static_cast<std::size_t>(e_lfanew));
  if (nt->Signature != IMAGE_NT_SIGNATURE) {
    result.message = "not a PE image (bad NT signature)";
    return result;
  }

  if (nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT) {
    result.ok = true;  // no export data directory present -> no exports
    logEvent(logger, diagnostics::Severity::Info, "hookdisc_export_enumerated",
             {{"module", module.name}, {"export_count", "0"}});
    return result;
  }

  const IMAGE_DATA_DIRECTORY& export_dir_entry =
      nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
  const DWORD export_rva = export_dir_entry.VirtualAddress;
  const DWORD export_size = export_dir_entry.Size;
  if (export_rva == 0 || export_size == 0) {
    // A module with no exports is a valid, non-error outcome.
    result.ok = true;
    logEvent(logger, diagnostics::Severity::Info, "hookdisc_export_enumerated",
             {{"module", module.name}, {"export_count", "0"}});
    return result;
  }
  if (!in_image(export_rva, export_size) ||
      !in_image(export_rva, sizeof(IMAGE_EXPORT_DIRECTORY))) {
    result.message = "export directory is out of image bounds";
    return result;
  }
  const DWORD export_end = export_rva + export_size;  // no overflow: in_image checked
  const auto* exports =
      reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(image + export_rva);

  const DWORD number_of_names = exports->NumberOfNames;
  const DWORD number_of_functions = exports->NumberOfFunctions;
  // Sanity-cap the counts before they drive any allocation or indexing.
  constexpr DWORD kMaxExportEntries = 1u << 20;  // 1,048,576
  if (number_of_functions > kMaxExportEntries || number_of_names > kMaxExportEntries) {
    result.message = "export table counts are implausibly large";
    return result;
  }

  const DWORD functions_rva = exports->AddressOfFunctions;
  const DWORD names_rva = exports->AddressOfNames;
  const DWORD ordinals_rva = exports->AddressOfNameOrdinals;
  if (number_of_functions > 0 &&
      !in_image(functions_rva,
                static_cast<std::size_t>(number_of_functions) * sizeof(DWORD))) {
    result.message = "export AddressOfFunctions array is out of bounds";
    return result;
  }
  if (number_of_names > 0 &&
      (!in_image(names_rva,
                 static_cast<std::size_t>(number_of_names) * sizeof(DWORD)) ||
       !in_image(ordinals_rva,
                 static_cast<std::size_t>(number_of_names) * sizeof(WORD)))) {
    result.message = "export name/ordinal arrays are out of bounds";
    return result;
  }

  const auto* functions = reinterpret_cast<const DWORD*>(image + functions_rva);
  const auto* names = reinterpret_cast<const DWORD*>(image + names_rva);
  const auto* ordinals = reinterpret_cast<const WORD*>(image + ordinals_rva);

  result.exports.reserve(
      static_cast<std::size_t>((std::min)(number_of_names, DWORD{65536})));
  for (DWORD i = 0; i < number_of_names; ++i) {
    const WORD ordinal_index = ordinals[i];
    if (ordinal_index >= number_of_functions) {
      continue;  // ordinal index out of range -> skip this malformed entry
    }
    ExportInfo info;
    if (!read_image_string(names[i], info.name)) {
      continue;  // unreadable / non-terminated name -> skip
    }
    info.ordinal = exports->Base + ordinal_index;
    const DWORD function_rva = functions[ordinal_index];
    if (function_rva == 0) {
      continue;  // empty slot
    }
    info.address = module.base + function_rva;
    if (function_rva >= export_rva && function_rva < export_end) {
      info.forwarded = true;
      std::string forward_target;
      if (read_image_string(function_rva, forward_target)) {
        info.forward_target = std::move(forward_target);
      }
    }
    result.exports.push_back(std::move(info));
  }

  result.ok = true;
  logEvent(logger, diagnostics::Severity::Info, "hookdisc_export_enumerated",
           {{"module", module.name},
            {"export_count", std::to_string(result.exports.size())}});
  return result;
}

std::vector<SymbolSearchHit> searchExportSymbols(
    const ExportEnumeration& exports,
    const std::vector<std::string>& candidates) {
  std::vector<SymbolSearchHit> hits;
  for (const std::string& candidate : candidates) {
    if (candidate.empty()) {
      continue;
    }
    for (const ExportInfo& exp : exports.exports) {
      const bool exact = (exp.name == candidate);
      const bool substring = !exact && exp.name.find(candidate) != std::string::npos;
      if (exact || substring) {
        SymbolSearchHit hit;
        hit.module = exports.module_name;
        hit.symbol = exp.name;
        hit.address = exp.address;
        hit.exact = exact;
        hits.push_back(std::move(hit));
      }
    }
  }
  return hits;
}

RttiScanResult scanCurrentProcessRtti(
    const ModuleInfo& module,
    const std::vector<std::string>& candidate_substrings,
    diagnostics::AsyncLogger* logger) {
  RttiScanResult result;
  result.module_name = module.name;

  if (module.base == 0 || module.size == 0) {
    result.message = "module has no mapped range to scan";
    return result;
  }

  // MSVC RTTI type-descriptor decorated-name prefixes: ".?AV" (class) and
  // ".?AU" (struct). We scan only committed, readable regions, identified by
  // VirtualQuery, so the scan can never fault on guard/no-access pages.
  static constexpr std::array<const char*, 2> kPatterns = {".?AV", ".?AU"};

  const std::uintptr_t begin = module.base;
  const std::uintptr_t end = module.base + module.size;
  std::uintptr_t cursor = begin;

  while (cursor < end) {
    MEMORY_BASIC_INFORMATION mbi;
    std::memset(&mbi, 0, sizeof(mbi));
    if (VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &mbi, sizeof(mbi)) == 0) {
      break;
    }
    const std::uintptr_t region_base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
    const std::uintptr_t region_end = region_base + mbi.RegionSize;
    const std::uintptr_t scan_begin = (std::max)(region_base, cursor);
    const std::uintptr_t scan_end = (std::min)(region_end, end);

    const bool readable =
        mbi.State == MEM_COMMIT &&
        (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) == 0 &&
        (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ |
                        PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY |
                        PAGE_EXECUTE_WRITECOPY)) != 0;

    if (readable && scan_end > scan_begin) {
      const BYTE* data = reinterpret_cast<const BYTE*>(scan_begin);
      const std::size_t length = static_cast<std::size_t>(scan_end - scan_begin);
      result.bytes_scanned += length;

      for (std::size_t i = 0; i + 4 <= length; ++i) {
        for (const char* pattern : kPatterns) {
          if (std::memcmp(data + i, pattern, 4) == 0) {
            // Extract the printable, null-terminated decorated name.
            std::string descriptor;
            std::size_t j = i;
            while (j < length) {
              const char ch = static_cast<char>(data[j]);
              if (ch == '\0') {
                break;
              }
              if (ch < 0x20 || ch > 0x7E) {
                descriptor.clear();
                break;
              }
              descriptor.push_back(ch);
              if (descriptor.size() > 512) {
                break;
              }
              ++j;
            }
            if (!descriptor.empty()) {
              bool keep = candidate_substrings.empty();
              for (const std::string& sub : candidate_substrings) {
                if (!sub.empty() && descriptor.find(sub) != std::string::npos) {
                  keep = true;
                  break;
                }
              }
              if (keep) {
                result.type_descriptors.push_back(descriptor);
              }
            }
            break;
          }
        }
      }
    }

    if (region_end <= cursor) {
      break;  // defensive: VirtualQuery did not advance
    }
    cursor = region_end;
  }

  // De-duplicate while preserving first-seen order.
  std::vector<std::string> unique;
  unique.reserve(result.type_descriptors.size());
  for (const std::string& descriptor : result.type_descriptors) {
    if (std::find(unique.begin(), unique.end(), descriptor) == unique.end()) {
      unique.push_back(descriptor);
    }
  }
  result.type_descriptors = std::move(unique);

  result.ok = true;
  logEvent(logger, diagnostics::Severity::Info, "hookdisc_rtti_scanned",
           {{"module", module.name},
            {"bytes_scanned", std::to_string(result.bytes_scanned)},
            {"descriptor_count", std::to_string(result.type_descriptors.size())}});
  return result;
}

}  // namespace vrclient::tooling::hookdisc

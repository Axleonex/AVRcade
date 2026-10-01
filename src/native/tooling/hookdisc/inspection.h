#pragma once

// Bolt-on Phase B2 (RE-01): non-invasive inspection harness.
//
// READ-ONLY observation only. This module enumerates loaded modules, parses PE
// export tables, searches for candidate symbol names, and scans for MSVC RTTI
// type descriptors (the ".?AV"/".?AU" pattern). It never writes target memory,
// never allocates remote memory, never creates remote threads, and never patches
// code bytes.
//
// v1 scope / honest limitation:
//   * Module enumeration works against any process id via the Toolhelp snapshot
//     API (read-only).
//   * Export-table parsing and RTTI scanning are implemented for the CURRENT
//     process only — they read the live, already-mapped image directly (no
//     ReadProcessMemory of another process). Remote export/RTTI parsing is
//     deferred to a later phase that performs it under the live safety gate;
//     see docs/tooling/hookdisc-design.md.

#include "diagnostics/logging/diagnostic_logger.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vrclient::tooling::hookdisc {

struct ModuleInfo {
  std::string name;  // file name, e.g. "kernel32.dll"
  std::string path;  // full path when available
  std::uintptr_t base = 0;
  std::size_t size = 0;
};

struct ModuleEnumeration {
  bool ok = false;
  std::string message;
  std::uint32_t process_id = 0;
  std::vector<ModuleInfo> modules;
};

struct ExportInfo {
  std::string name;
  std::uintptr_t address = 0;  // absolute VA inside the inspected (current) process
  std::uint32_t ordinal = 0;
  bool forwarded = false;       // export forwards to another module
  std::string forward_target;   // "OTHERDLL.SomeExport" when forwarded
};

struct ExportEnumeration {
  bool ok = false;
  std::string message;
  std::string module_name;
  std::vector<ExportInfo> exports;
};

struct SymbolSearchHit {
  std::string module;
  std::string symbol;
  std::uintptr_t address = 0;
  bool exact = false;  // true => exact match, false => substring match
};

struct RttiScanResult {
  bool ok = false;
  std::string message;
  std::string module_name;
  std::size_t bytes_scanned = 0;
  std::vector<std::string> type_descriptors;  // raw ".?AV...@@" decorated names
};

// Enumerate loaded modules of a process. process_id == 0 => current process.
ModuleEnumeration enumerateModules(
    std::uint32_t process_id,
    diagnostics::AsyncLogger* logger = nullptr);

ModuleEnumeration enumerateCurrentProcessModules(
    diagnostics::AsyncLogger* logger = nullptr);

// Case-insensitive lookup by module file name. Returns nullptr when not found.
const ModuleInfo* findModule(const ModuleEnumeration& modules, std::string_view name);

// Parse the PE export directory of a module mapped in the CURRENT process.
ExportEnumeration enumerateCurrentProcessExports(
    const ModuleInfo& module,
    diagnostics::AsyncLogger* logger = nullptr);

// Match candidate symbol names against an export enumeration. A candidate hits
// "exact" when an export name equals it, otherwise "substring" when an export
// name contains it.
std::vector<SymbolSearchHit> searchExportSymbols(
    const ExportEnumeration& exports,
    const std::vector<std::string>& candidates);

// Scan readable bytes of a CURRENT-process module image for MSVC RTTI type
// descriptors (".?AV"/".?AU"). When candidate_substrings is non-empty, only
// descriptors containing one of those substrings are retained.
RttiScanResult scanCurrentProcessRtti(
    const ModuleInfo& module,
    const std::vector<std::string>& candidate_substrings,
    diagnostics::AsyncLogger* logger = nullptr);

}  // namespace vrclient::tooling::hookdisc

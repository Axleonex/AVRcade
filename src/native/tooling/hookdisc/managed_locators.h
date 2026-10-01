#pragma once

// M6 Half A (AAA-01/03): READ-ONLY managed-metadata enumeration.
//
// Given a Mono/.NET assembly (an `Assembly-CSharp`-style PE/CLI image), enumerate
// its user-defined methods as `mono_method` locator candidates by walking the
// ECMA-335 metadata tables (TypeDef + MethodDef, names via the #Strings heap).
//
// READ-ONLY BY DECREE (M6D4): this parses a file's bytes. It does NOT load the
// assembly, execute it, patch, inject, hook, or touch any process — no remote-
// thread, cross-process memory-write, module-injection, or byte-patching APIs
// are used here. Enumeration only.

#include <string>
#include <vector>

namespace vrclient::tooling::hookdisc {

struct ManagedMethodCandidate {
  std::string locator_kind;  // "mono_method"
  std::string type_name;     // declaring type (namespace-qualified when present)
  std::string method_name;   // method identifier
};

// Enumerate user-defined methods from the assembly at `assemblyPath`. Returns
// true on success (candidates in `out`), false with `error` set on a malformed
// or non-CLI image. Compiler-generated / special names (leading '<' or '.ctor'
// style) are included; the caller filters. Read-only; never mutates the file.
bool EnumerateMonoMethods(const std::string& assemblyPath,
                          std::vector<ManagedMethodCandidate>& out,
                          std::string& error);

// Enumerate methods from an IL2CPP `global-metadata.dat` (the file at
// `<Game>_Data/il2cpp_data/Metadata/global-metadata.dat`). Reads the header's
// method-definition table (nameIndex is the first field of each entry) and the
// string heap. VERSION-SCOPED (AAA-02): the Il2CppMethodDefinition struct stride
// differs across IL2CPP metadata versions, so this walks the version(s) whose
// layout it knows and returns a clear error for an unknown version — never a
// silent misparse. type_name is left as "(il2cpp)" at this increment (declaring-
// type association needs the far-header typeDefinitions layout — a later step).
// Read-only; never mutates the file.
bool EnumerateIl2CppMethods(const std::string& metadataPath,
                            std::vector<ManagedMethodCandidate>& out,
                            std::string& error);

}  // namespace vrclient::tooling::hookdisc

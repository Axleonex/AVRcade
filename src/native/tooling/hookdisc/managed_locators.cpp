#include "tooling/hookdisc/managed_locators.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>

// READ-ONLY ECMA-335 (CLI) metadata reader. See the header for the safety decree.
// Scope: enough of the format to walk TypeDef + MethodDef and resolve names from
// the #Strings heap for a controlled fixture and typical csc/Mono output.

namespace vrclient::tooling::hookdisc {
namespace {

std::uint16_t Rd16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
std::uint32_t Rd32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) |
                                    (static_cast<std::uint32_t>(p[3]) << 24));
}

struct Image {
  std::vector<std::uint8_t> bytes;
  bool Ok(std::size_t off, std::size_t len) const { return off + len <= bytes.size(); }
};

// PE section for RVA translation.
struct Section {
  std::uint32_t virt_addr = 0;
  std::uint32_t virt_size = 0;
  std::uint32_t raw_ptr = 0;
  std::uint32_t raw_size = 0;
};

bool RvaToOffset(const std::vector<Section>& sections, std::uint32_t rva, std::size_t& out) {
  for (const auto& s : sections) {
    std::uint32_t span = s.virt_size ? s.virt_size : s.raw_size;
    if (rva >= s.virt_addr && rva < s.virt_addr + span) {
      out = static_cast<std::size_t>(rva - s.virt_addr) + s.raw_ptr;
      return true;
    }
  }
  return false;
}

}  // namespace

bool EnumerateMonoMethods(const std::string& assemblyPath,
                          std::vector<ManagedMethodCandidate>& out,
                          std::string& error) {
  Image img;
  {
    std::ifstream f(assemblyPath, std::ios::binary);
    if (!f) { error = "cannot open assembly: " + assemblyPath; return false; }
    std::ostringstream ss; ss << f.rdbuf();
    const std::string data = ss.str();
    img.bytes.assign(data.begin(), data.end());
  }
  const std::uint8_t* b = img.bytes.data();

  // --- PE headers ---
  if (!img.Ok(0x40, 0) || img.bytes.size() < 0x40 || b[0] != 'M' || b[1] != 'Z') {
    error = "not a PE image"; return false;
  }
  const std::uint32_t peOff = Rd32(b + 0x3C);
  if (!img.Ok(peOff, 24) || std::memcmp(b + peOff, "PE\0\0", 4) != 0) {
    error = "bad PE signature"; return false;
  }
  const std::uint16_t numSections = Rd16(b + peOff + 6);
  const std::uint16_t optSize = Rd16(b + peOff + 20);
  const std::size_t optOff = peOff + 24;
  if (!img.Ok(optOff, optSize)) { error = "truncated optional header"; return false; }
  const std::uint16_t magic = Rd16(b + optOff);
  const std::size_t dirOff = optOff + (magic == 0x20B ? 112 : 96);  // PE32+ vs PE32
  // COM descriptor (CLI header) = data directory index 14.
  if (!img.Ok(dirOff + 14 * 8, 8)) { error = "no data directories"; return false; }
  const std::uint32_t comRva = Rd32(b + dirOff + 14 * 8);
  if (comRva == 0) { error = "not a CLI/.NET assembly (no COR20 header)"; return false; }

  std::vector<Section> sections;
  std::size_t secOff = optOff + optSize;
  for (std::uint16_t i = 0; i < numSections; ++i) {
    if (!img.Ok(secOff, 40)) { error = "truncated section headers"; return false; }
    Section s;
    s.virt_size = Rd32(b + secOff + 8);
    s.virt_addr = Rd32(b + secOff + 12);
    s.raw_size = Rd32(b + secOff + 16);
    s.raw_ptr = Rd32(b + secOff + 20);
    sections.push_back(s);
    secOff += 40;
  }

  // --- CLI header -> metadata root ---
  std::size_t comOff;
  if (!RvaToOffset(sections, comRva, comOff) || !img.Ok(comOff, 16)) {
    error = "bad COR20 header rva"; return false;
  }
  const std::uint32_t mdRva = Rd32(b + comOff + 8);
  std::size_t mdOff;
  if (!RvaToOffset(sections, mdRva, mdOff) || !img.Ok(mdOff, 20)) {
    error = "bad metadata rva"; return false;
  }
  if (Rd32(b + mdOff) != 0x424A5342) { error = "bad metadata signature (BSJB)"; return false; }
  const std::uint32_t verLen = Rd32(b + mdOff + 12);
  std::size_t p = mdOff + 16 + ((verLen + 3) & ~3u);  // skip version string (4-aligned)
  if (!img.Ok(p, 4)) { error = "truncated metadata streams header"; return false; }
  const std::uint16_t streamCount = Rd16(b + p + 2);
  p += 4;

  std::size_t stringsOff = 0, stringsSize = 0, tablesOff = 0, tablesSize = 0;
  for (std::uint16_t i = 0; i < streamCount; ++i) {
    if (!img.Ok(p, 8)) { error = "truncated stream header"; return false; }
    const std::uint32_t off = Rd32(b + p);
    const std::uint32_t size = Rd32(b + p + 4);
    const char* name = reinterpret_cast<const char*>(b + p + 8);
    std::size_t nameLen = 0;
    while (p + 8 + nameLen < img.bytes.size() && name[nameLen] != '\0') ++nameLen;
    if (std::strcmp(name, "#Strings") == 0) { stringsOff = mdOff + off; stringsSize = size; }
    else if (std::strcmp(name, "#~") == 0 || std::strcmp(name, "#-") == 0) { tablesOff = mdOff + off; tablesSize = size; }
    p += 8 + ((nameLen + 1 + 3) & ~3u);
  }
  if (stringsOff == 0 || tablesOff == 0) { error = "missing #Strings or #~ stream"; return false; }

  auto ReadString = [&](std::uint32_t idx) -> std::string {
    std::size_t s = stringsOff + idx;
    if (s >= img.bytes.size()) return std::string();
    std::string out2;
    while (s < img.bytes.size() && b[s] != '\0') out2.push_back(static_cast<char>(b[s++]));
    return out2;
  };

  // --- #~ table stream header ---
  if (!img.Ok(tablesOff, 24)) { error = "truncated table stream"; return false; }
  const std::uint8_t heapSizes = b[tablesOff + 6];
  const std::uint64_t valid = static_cast<std::uint64_t>(Rd32(b + tablesOff + 8)) |
                              (static_cast<std::uint64_t>(Rd32(b + tablesOff + 12)) << 32);
  std::size_t rc = tablesOff + 24;  // row-count array
  std::uint32_t rows[64] = {0};
  for (int t = 0; t < 64; ++t) {
    if (valid & (1ull << t)) {
      if (!img.Ok(rc, 4)) { error = "truncated row counts"; return false; }
      rows[t] = Rd32(b + rc);
      rc += 4;
    }
  }
  const std::size_t rowsStart = rc;
  const int strIdx = (heapSizes & 0x01) ? 4 : 2;
  const int guidIdx = (heapSizes & 0x02) ? 4 : 2;
  const int blobIdx = (heapSizes & 0x04) ? 4 : 2;
  auto simple = [&](int table) { return rows[table] < (1u << 16) ? 2 : 4; };
  auto coded = [&](int bits, std::initializer_list<int> set) {
    std::uint32_t mx = 0; for (int t : set) if (rows[t] > mx) mx = rows[t];
    return (mx < (1u << (16 - bits))) ? 2 : 4;
  };

  // Row sizes for tables 0..6 (only those needed to reach + read TypeDef/MethodDef).
  const int rsTypeDefOrRef = coded(2, {0x02, 0x01, 0x1B});           // TypeDef, TypeRef, TypeSpec
  const int rsResolutionScope = coded(2, {0x00, 0x1A, 0x23, 0x01});  // Module, ModuleRef, AssemblyRef, TypeRef
  int size[7] = {0};
  size[0] = 2 + strIdx + 3 * guidIdx;                                        // Module
  size[1] = rsResolutionScope + 2 * strIdx;                                  // TypeRef
  size[2] = 4 + 2 * strIdx + rsTypeDefOrRef + simple(0x04) + simple(0x06);   // TypeDef: ..Field, ..Method
  size[3] = simple(0x04);                                                    // FieldPtr (rare)
  size[4] = 2 + strIdx + blobIdx;                                            // Field
  size[5] = simple(0x06);                                                    // MethodPtr (rare)
  size[6] = 4 + 2 + 2 + strIdx + blobIdx + simple(0x08);                     // MethodDef

  auto rowStart = [&](int table) -> std::size_t {
    std::size_t o = rowsStart;
    for (int t = 0; t < table; ++t) o += static_cast<std::size_t>(rows[t]) * size[t];
    return o;
  };
  auto readIdx = [&](std::size_t at, int w) -> std::uint32_t {
    return w == 2 ? Rd16(b + at) : Rd32(b + at);
  };

  const std::size_t typeDefBase = rowStart(0x02);
  const std::size_t methodBase = rowStart(0x06);
  if (!img.Ok(methodBase, static_cast<std::size_t>(rows[0x06]) * size[6])) {
    error = "truncated MethodDef table"; return false;
  }

  // MethodDef names (Name index at row offset 4+2+2 = 8).
  std::vector<std::string> methodNames(rows[0x06] + 1);
  for (std::uint32_t m = 0; m < rows[0x06]; ++m) {
    const std::size_t at = methodBase + static_cast<std::size_t>(m) * size[6] + 8;
    methodNames[m + 1] = ReadString(readIdx(at, strIdx));  // 1-based table rows
  }

  // TypeDef: Name (off 4), Namespace (off 4+strIdx), MethodList (last column).
  const int tdMethodListOff = 4 + 2 * strIdx + rsTypeDefOrRef + simple(0x04);
  for (std::uint32_t td = 0; td < rows[0x02]; ++td) {
    const std::size_t base = typeDefBase + static_cast<std::size_t>(td) * size[2];
    const std::string typeName = ReadString(readIdx(base + 4, strIdx));
    const std::string ns = ReadString(readIdx(base + 4 + strIdx, strIdx));
    const std::uint32_t methodStart = readIdx(base + tdMethodListOff, simple(0x06));
    std::uint32_t methodEnd = rows[0x06] + 1;
    if (td + 1 < rows[0x02]) {
      const std::size_t nextBase = typeDefBase + static_cast<std::size_t>(td + 1) * size[2];
      methodEnd = readIdx(nextBase + tdMethodListOff, simple(0x06));
    }
    const std::string fullType = ns.empty() ? typeName : ns + "." + typeName;
    for (std::uint32_t m = methodStart; m < methodEnd && m <= rows[0x06]; ++m) {
      if (m == 0 || m >= methodNames.size()) continue;
      out.push_back(ManagedMethodCandidate{"mono_method", fullType, methodNames[m]});
    }
  }
  return true;
}

bool EnumerateIl2CppMethods(const std::string& metadataPath,
                            std::vector<ManagedMethodCandidate>& out,
                            std::string& error) {
  std::vector<std::uint8_t> bytes;
  {
    std::ifstream f(metadataPath, std::ios::binary);
    if (!f) { error = "cannot open global-metadata.dat: " + metadataPath; return false; }
    std::ostringstream ss; ss << f.rdbuf();
    const std::string data = ss.str();
    bytes.assign(data.begin(), data.end());
  }
  const std::uint8_t* b = bytes.data();
  auto ok = [&](std::size_t off, std::size_t len) { return off + len <= bytes.size(); };

  // IL2CPP global-metadata header (positions stable across the supported versions):
  //   sanity@0, version@4, stringOffset@24, stringSize@28, methodsOffset@48, methodsSize@52.
  if (!ok(0, 56)) { error = "file too small for an IL2CPP metadata header"; return false; }
  if (Rd32(b) != 0xFAB11BAF) { error = "not an IL2CPP global-metadata.dat (bad sanity)"; return false; }
  const std::uint32_t version = Rd32(b + 4);

  // Il2CppMethodDefinition stride per metadata version (nameIndex is always the
  // first int32). Version-scoped by decree (AAA-02); unknown -> clear refusal.
  std::size_t methodDefSize = 0;
  if (version == 24) methodDefSize = 32;       // v24: ..token(4)+flags/iflags/slot/paramCount(2*4)
  else if (version == 29) methodDefSize = 36;  // v29: adds returnParameterToken(4)
  if (methodDefSize == 0) {
    std::ostringstream e; e << "unsupported IL2CPP metadata version " << version
                            << " (known: 24, 29); real per-version layouts are the next increment";
    error = e.str();
    return false;
  }

  const std::uint32_t stringOffset = Rd32(b + 24);
  const std::uint32_t stringSize = Rd32(b + 28);
  const std::uint32_t methodsOffset = Rd32(b + 48);
  const std::uint32_t methodsSize = Rd32(b + 52);
  if (!ok(stringOffset, stringSize) || !ok(methodsOffset, methodsSize)) {
    error = "IL2CPP header offsets out of range"; return false;
  }

  auto ReadHeapString = [&](std::uint32_t idx) -> std::string {
    std::size_t s = static_cast<std::size_t>(stringOffset) + idx;
    if (idx >= stringSize || s >= bytes.size()) return std::string();
    std::string out2;
    while (s < bytes.size() && b[s] != '\0') out2.push_back(static_cast<char>(b[s++]));
    return out2;
  };

  const std::size_t methodCount = methodsSize / methodDefSize;
  for (std::size_t i = 0; i < methodCount; ++i) {
    const std::size_t at = static_cast<std::size_t>(methodsOffset) + i * methodDefSize;
    if (!ok(at, 4)) break;
    const std::uint32_t nameIndex = Rd32(b + at);  // first field of Il2CppMethodDefinition
    const std::string name = ReadHeapString(nameIndex);
    if (!name.empty())
      out.push_back(ManagedMethodCandidate{"il2cpp_method", "(il2cpp)", name});
  }
  return true;
}

}  // namespace vrclient::tooling::hookdisc

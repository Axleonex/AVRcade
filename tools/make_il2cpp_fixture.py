#!/usr/bin/env python3
"""Generate a minimal, SPEC-CONFORMANT IL2CPP global-metadata.dat fixture (v24)
for the M6 managed-locator CTest (AAA-02). It is a CONTROLLED TEST FIXTURE — never
a real game. Header field positions (sanity@0, version@4, stringOffset@24,
stringSize@28, methodsOffset@48, methodsSize@52) and the v24 Il2CppMethodDefinition
stride (32 bytes, nameIndex first) match the documented IL2CPP metadata format, so
the same reader also parses a real v24 global-metadata.dat.
"""
import struct
import sys

METHOD_DEF_SIZE = 32  # v24 Il2CppMethodDefinition stride
HEADER_SIZE = 64
METHODS_OFFSET = HEADER_SIZE

# String heap: index 0 is the empty string.
names = ["Il2CppFixtureMethod", "AnotherIl2CppMethod"]
heap = b"\x00"
name_index = {}
for n in names:
    name_index[n] = len(heap)
    heap += n.encode("ascii") + b"\x00"

methods = b"".join(
    struct.pack("<i", name_index[n]) + b"\x00" * (METHOD_DEF_SIZE - 4) for n in names
)
strings_offset = METHODS_OFFSET + len(methods)

header = bytearray(HEADER_SIZE)
struct.pack_into("<I", header, 0, 0xFAB11BAF)          # sanity
struct.pack_into("<I", header, 4, 24)                  # version
struct.pack_into("<I", header, 24, strings_offset)     # stringOffset
struct.pack_into("<I", header, 28, len(heap))          # stringSize
struct.pack_into("<I", header, 48, METHODS_OFFSET)     # methodsOffset
struct.pack_into("<I", header, 52, len(methods))       # methodsSize

blob = bytes(header) + methods + heap
out = sys.argv[1] if len(sys.argv) > 1 else "global-metadata.fixture.dat"
with open(out, "wb") as f:
    f.write(blob)
print(f"wrote {out}: {len(blob)} bytes, {len(names)} methods, v24")

#pragma once

#include <cstdint>

// ABI passed by a FOSE/bootstrap layer after it has selected a reviewed
// Fallout3.exe build profile. All addresses are RVAs from the main image so
// ASLR is supported. The executable hash and entry bytes are both mandatory.
struct Fallout3HookProfile {
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;
    char expectedExecutableSha256[65];
    std::uint8_t hashPadding[3];
    std::uint32_t renderHookRva;
    std::uint32_t renderContextPointerRva;
    std::uint32_t sceneGraphPointerRva;
    std::uint32_t rendererPointerRva;
    std::uint32_t cameraOffset;
    std::uint32_t deviceOffset;
    std::uint32_t renderEntryByteCount;
    std::uint8_t expectedRenderEntry[16];
    float unitsPerMeter;
    std::uint8_t standingOrigin;
    std::uint8_t reserved[3];
};

inline constexpr std::uint32_t Fallout3HookInterfaceVersion = 3;
static_assert(sizeof(Fallout3HookProfile) == 128);

extern "C" __declspec(dllexport) bool Fallout3VrConfigure(const Fallout3HookProfile* profile);
extern "C" __declspec(dllexport) void Fallout3VrStop();

#pragma once
#include "vr_runtime_api.h"
#include <memory>

struct IDirect3DDevice9;
struct IDirect3DSurface9;
namespace vrclient::runtime {
// Standalone 32-bit legacy bridge, not a new selector in the stable runtime ABI.
// All calls/destruction belong to one render thread. No OpenXR handles escape.
class LegacyStereo {
public:
    LegacyStereo();
    ~LegacyStereo();
    LegacyStereo(const LegacyStereo&) = delete;
    LegacyStereo& operator=(const LegacyStereo&) = delete;
    bool initialize(const wchar_t* absoluteLoaderPath, unsigned width, unsigned height);
    bool begin(VrRuntimeEyeView (&eyes)[2]);
    bool copyEye(IDirect3DDevice9* device, IDirect3DSurface9* surface, unsigned eye);
    bool end();
    const char* error() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

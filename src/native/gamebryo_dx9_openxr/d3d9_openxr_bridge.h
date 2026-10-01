#pragma once

#include "../runtime/public/legacy_stereo.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

struct IDirect3DDevice9;
struct IDirect3DSurface9;

namespace vrclient::gamebryo {

enum class BridgeState : std::uint8_t {
    Cold,
    Ready,
    FrameBegun,
    DeviceLost,
    RuntimeLost,
    Stopped
};

enum class TrackingOrigin : std::uint8_t { Seated, Standing };

struct BridgeConfig {
    std::wstring openxrLoaderPath;
    std::uint32_t width{};
    std::uint32_t height{};
    TrackingOrigin origin{TrackingOrigin::Seated};
};

struct EyeRenderRequest {
    std::uint32_t eye{};
    VrRuntimeEyeView view{};
};

struct BridgeDiagnostics {
    BridgeState state{BridgeState::Cold};
    std::uint64_t framesBegun{};
    std::uint64_t framesSubmitted{};
    std::uint64_t rejectedFrames{};
    std::uint64_t deviceLosses{};
    std::uint64_t resets{};
    bool independentEyeViewsObserved{};
    std::string lastMessage;
};

// Pure lifecycle model is kept separate so loss/reset/fallback behavior is
// deterministic and testable without a game, D3D device, OpenXR runtime or HMD.
class BridgeLifecycle final {
public:
    bool initialized();
    bool begin();
    bool submitEye(std::uint32_t eye, std::uint64_t projectionSignature);
    bool end();
    void deviceLost();
    void reset();
    void runtimeLost();
    void stop();
    BridgeState state() const noexcept { return state_; }
    bool independentEyes() const noexcept { return independentEyes_; }
private:
    BridgeState state_{BridgeState::Cold};
    std::uint8_t submittedMask_{};
    std::array<std::uint64_t, 2> projectionSignatures_{};
    bool independentEyes_{};
};

// Reusable 32-bit Gamebryo bridge. It owns no game addresses and performs no
// executable patching. A title adapter must render each eye independently,
// pass the matching eye surface, and provide a signature of the actual view /
// projection pair used for that render. Copying one desktop frame twice is
// rejected and never promoted to native stereo evidence.
class D3d9OpenXrBridge final {
public:
    using DiagnosticSink = std::function<void(const BridgeDiagnostics&)>;
    D3d9OpenXrBridge();
    ~D3d9OpenXrBridge();
    D3d9OpenXrBridge(const D3d9OpenXrBridge&) = delete;
    D3d9OpenXrBridge& operator=(const D3d9OpenXrBridge&) = delete;

    bool configure(BridgeConfig config, DiagnosticSink sink = {});
    bool beginFrame(EyeRenderRequest (&eyes)[2]);
    bool submitEye(IDirect3DDevice9* device, IDirect3DSurface9* surface,
                   std::uint32_t eye, std::uint64_t projectionSignature);
    bool endFrame();
    void onDeviceLost();
    void onResetComplete(std::uint32_t width, std::uint32_t height);
    void stop();
    const BridgeDiagnostics& diagnostics() const noexcept { return diagnostics_; }

private:
    bool initializeRuntime();
    void reject(std::string message);
    void publish();
    BridgeConfig config_{};
    DiagnosticSink sink_{};
    BridgeLifecycle lifecycle_{};
    BridgeDiagnostics diagnostics_{};
    std::unique_ptr<runtime::LegacyStereo> runtime_{};
    std::array<VrRuntimeEyeView, 2> views_{};
};

} // namespace vrclient::gamebryo

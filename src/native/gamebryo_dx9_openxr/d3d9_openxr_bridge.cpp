#include "d3d9_openxr_bridge.h"
#include <utility>

namespace vrclient::gamebryo {

bool BridgeLifecycle::initialized() {
    if (state_ != BridgeState::Cold && state_ != BridgeState::DeviceLost) return false;
    state_ = BridgeState::Ready;
    submittedMask_ = 0;
    projectionSignatures_ = {};
    return true;
}

bool BridgeLifecycle::begin() {
    if (state_ != BridgeState::Ready) return false;
    state_ = BridgeState::FrameBegun;
    submittedMask_ = 0;
    projectionSignatures_ = {};
    independentEyes_ = false;
    return true;
}

bool BridgeLifecycle::submitEye(std::uint32_t eye, std::uint64_t signature) {
    if (state_ != BridgeState::FrameBegun || eye > 1 || signature == 0 || (submittedMask_ & (1u << eye)))
        return false;
    projectionSignatures_[eye] = signature;
    submittedMask_ |= static_cast<std::uint8_t>(1u << eye);
    if (submittedMask_ == 3)
        independentEyes_ = projectionSignatures_[0] != projectionSignatures_[1];
    return true;
}

bool BridgeLifecycle::end() {
    if (state_ != BridgeState::FrameBegun) return false;
    const bool complete = submittedMask_ == 3 && independentEyes_;
    state_ = BridgeState::Ready;
    submittedMask_ = 0;
    return complete;
}

void BridgeLifecycle::deviceLost() {
    if (state_ != BridgeState::Stopped && state_ != BridgeState::RuntimeLost)
        state_ = BridgeState::DeviceLost;
    submittedMask_ = 0;
}
void BridgeLifecycle::reset() { if (state_ == BridgeState::DeviceLost) state_ = BridgeState::Cold; }
void BridgeLifecycle::runtimeLost() { state_ = BridgeState::RuntimeLost; submittedMask_ = 0; }
void BridgeLifecycle::stop() { state_ = BridgeState::Stopped; submittedMask_ = 0; }

D3d9OpenXrBridge::D3d9OpenXrBridge() = default;
D3d9OpenXrBridge::~D3d9OpenXrBridge() { stop(); }

bool D3d9OpenXrBridge::configure(BridgeConfig config, DiagnosticSink sink) {
    if (config.openxrLoaderPath.empty() || !config.width || !config.height ||
        config.width > 8192 || config.height > 8192 || lifecycle_.state() == BridgeState::Stopped)
        return false;
    config_ = std::move(config);
    sink_ = std::move(sink);
    return initializeRuntime();
}

bool D3d9OpenXrBridge::initializeRuntime() {
    runtime_ = std::make_unique<runtime::LegacyStereo>();
    if (!runtime_->initialize(config_.openxrLoaderPath.c_str(), config_.width, config_.height)) {
        reject(runtime_->error());
        runtime_.reset();
        return false;
    }
    if (!lifecycle_.initialized()) {
        reject("bridge lifecycle refused runtime initialization");
        runtime_.reset();
        return false;
    }
    diagnostics_.state = lifecycle_.state();
    diagnostics_.lastMessage = config_.origin == TrackingOrigin::Standing
        ? "OpenXR ready (standing origin requested; local-space fallback may be used)"
        : "OpenXR ready (seated/local origin)";
    publish();
    return true;
}

bool D3d9OpenXrBridge::beginFrame(EyeRenderRequest (&eyes)[2]) {
    if (lifecycle_.state() == BridgeState::Cold && !initializeRuntime()) return false;
    if (!runtime_ || !lifecycle_.begin()) return false;
    VrRuntimeEyeView runtimeEyes[2]{};
    if (!runtime_->begin(runtimeEyes)) {
        lifecycle_.runtimeLost();
        diagnostics_.state = lifecycle_.state();
        reject(runtime_->error()[0] ? runtime_->error() : "OpenXR frame begin failed");
        runtime_.reset();
        return false;
    }
    for (std::uint32_t eye = 0; eye < 2; ++eye) {
        views_[eye] = runtimeEyes[eye];
        eyes[eye] = {eye, runtimeEyes[eye]};
    }
    ++diagnostics_.framesBegun;
    diagnostics_.state = lifecycle_.state();
    publish();
    return true;
}

bool D3d9OpenXrBridge::submitEye(IDirect3DDevice9* device, IDirect3DSurface9* surface,
                                 std::uint32_t eye, std::uint64_t signature) {
    if (!runtime_ || !lifecycle_.submitEye(eye, signature)) {
        reject("eye submission refused: duplicate, invalid, or identical-projection frame");
        return false;
    }
    if (!runtime_->copyEye(device, surface, eye)) {
        reject(runtime_->error());
        return false;
    }
    return true;
}

bool D3d9OpenXrBridge::endFrame() {
    if (!runtime_) return false;
    const bool independent = lifecycle_.independentEyes();
    const bool complete = lifecycle_.end();
    const bool submitted = runtime_->end();
    diagnostics_.state = lifecycle_.state();
    diagnostics_.independentEyeViewsObserved = independent;
    if (complete && submitted) {
        ++diagnostics_.framesSubmitted;
        diagnostics_.lastMessage = "independent per-eye frame submitted";
        publish();
        return true;
    }
    reject(independent ? runtime_->error() : "native frame rejected: both eyes did not use independent projections");
    return false;
}

void D3d9OpenXrBridge::onDeviceLost() {
    runtime_.reset();
    lifecycle_.deviceLost();
    ++diagnostics_.deviceLosses;
    diagnostics_.state = lifecycle_.state();
    diagnostics_.lastMessage = "D3D9 device lost; OpenXR resources released and desktop fallback required";
    publish();
}

void D3d9OpenXrBridge::onResetComplete(std::uint32_t width, std::uint32_t height) {
    if (!width || !height || width > 8192 || height > 8192) { reject("invalid D3D9 reset dimensions"); return; }
    config_.width = width; config_.height = height;
    lifecycle_.reset();
    ++diagnostics_.resets;
    diagnostics_.state = lifecycle_.state();
    diagnostics_.lastMessage = "D3D9 reset complete; runtime will reinitialize on the render thread";
    publish();
}

void D3d9OpenXrBridge::stop() {
    runtime_.reset();
    lifecycle_.stop();
    diagnostics_.state = lifecycle_.state();
    diagnostics_.lastMessage = "bridge stopped";
    publish();
}

void D3d9OpenXrBridge::reject(std::string message) {
    ++diagnostics_.rejectedFrames;
    diagnostics_.state = lifecycle_.state();
    diagnostics_.lastMessage = std::move(message);
    publish();
}
void D3d9OpenXrBridge::publish() { if (sink_) sink_(diagnostics_); }

} // namespace vrclient::gamebryo

#if defined(VRCLIENT_BRIDGE_SELF_TEST_MAIN)
#include <cstdlib>
int main() {
    using namespace vrclient::gamebryo;
    BridgeLifecycle lifecycle;
    if (!lifecycle.initialized() || !lifecycle.begin()) return EXIT_FAILURE;
    if (!lifecycle.submitEye(0, 101) || !lifecycle.submitEye(1, 202) || !lifecycle.end()) return EXIT_FAILURE;
    lifecycle.deviceLost();
    if (lifecycle.state() != BridgeState::DeviceLost) return EXIT_FAILURE;
    lifecycle.reset();
    if (lifecycle.state() != BridgeState::Cold || !lifecycle.initialized()) return EXIT_FAILURE;
    if (!lifecycle.begin() || !lifecycle.submitEye(0, 303) || !lifecycle.submitEye(1, 303) || lifecycle.end())
        return EXIT_FAILURE; // identical projections must never prove native stereo
    lifecycle.runtimeLost();
    if (lifecycle.state() != BridgeState::RuntimeLost) return EXIT_FAILURE;
    lifecycle.stop();
    return lifecycle.state() == BridgeState::Stopped ? EXIT_SUCCESS : EXIT_FAILURE;
}
#endif

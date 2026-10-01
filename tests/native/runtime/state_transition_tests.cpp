// Hardened against NDEBUG: shares the vr_runtime_unit_tests binary with
// runtime_profile_tests.cpp. assert() compiles out under any Release/NDEBUG
// build, so the state-machine checks now use a throwing expect() helper that
// survives both Debug and Release.

#include "frame/frame_loop.h"

#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

}  // namespace

int runStateTransitionTests() {
  vrclient::runtime::frame::LifecycleStateMachine lifecycle;
  expect(lifecycle.state() == VR_RUNTIME_STATE_STOPPED,
         "initial state must be STOPPED");
  expect(!lifecycle.sessionActive(), "no session active at start");

  expect(lifecycle.apply(vrclient::runtime::frame::SessionSignal::Ready) ==
             VR_RUNTIME_STATE_READY,
         "Ready signal -> READY");
  expect(!lifecycle.sessionActive(), "READY is not session-active");

  expect(lifecycle.apply(vrclient::runtime::frame::SessionSignal::Focused) ==
             VR_RUNTIME_STATE_RUNNING,
         "Focused signal -> RUNNING");
  expect(lifecycle.sessionActive(), "RUNNING is session-active");

  expect(lifecycle.apply(vrclient::runtime::frame::SessionSignal::LossPending) ==
             VR_RUNTIME_STATE_LOSS_PENDING,
         "LossPending signal -> LOSS_PENDING");
  expect(!lifecycle.sessionActive(), "LOSS_PENDING is not session-active");

  expect(lifecycle.apply(vrclient::runtime::frame::SessionSignal::Exiting) ==
             VR_RUNTIME_STATE_EXITING,
         "Exiting signal -> EXITING");
  expect(!lifecycle.sessionActive(), "EXITING is not session-active");

  lifecycle.reset();
  expect(lifecycle.state() == VR_RUNTIME_STATE_STOPPED,
         "reset returns to STOPPED");
  return 0;
}

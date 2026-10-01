#include "frame/frame_loop.h"

namespace vrclient::runtime::frame {

VrRuntimeState LifecycleStateMachine::apply(SessionSignal signal) {
  switch (signal) {
    case SessionSignal::Idle:
      session_active_ = false;
      state_ = VR_RUNTIME_STATE_STOPPED;
      break;
    case SessionSignal::Ready:
      session_active_ = false;
      state_ = VR_RUNTIME_STATE_READY;
      break;
    case SessionSignal::Synchronized:
    case SessionSignal::Visible:
      session_active_ = false;
      state_ = VR_RUNTIME_STATE_READY;
      break;
    case SessionSignal::Focused:
      session_active_ = true;
      state_ = VR_RUNTIME_STATE_RUNNING;
      break;
    case SessionSignal::Stopping:
      session_active_ = false;
      state_ = VR_RUNTIME_STATE_STOPPED;
      break;
    case SessionSignal::LossPending:
    case SessionSignal::RuntimeRestarted:
    case SessionSignal::HeadsetDisconnected:
      session_active_ = false;
      state_ = VR_RUNTIME_STATE_LOSS_PENDING;
      break;
    case SessionSignal::Exiting:
      session_active_ = false;
      state_ = VR_RUNTIME_STATE_EXITING;
      break;
    case SessionSignal::Failure:
      session_active_ = false;
      state_ = VR_RUNTIME_STATE_ERROR;
      break;
  }

  return state_;
}

void LifecycleStateMachine::reset() {
  state_ = VR_RUNTIME_STATE_STOPPED;
  session_active_ = false;
}

}  // namespace vrclient::runtime::frame

#pragma once

#include "public/vr_runtime_api.h"

namespace vrclient::runtime::frame {

enum class SessionSignal {
  Idle,
  Ready,
  Synchronized,
  Visible,
  Focused,
  Stopping,
  LossPending,
  Exiting,
  RuntimeRestarted,
  HeadsetDisconnected,
  Failure
};

class LifecycleStateMachine {
 public:
  VrRuntimeState state() const { return state_; }
  bool sessionActive() const { return session_active_; }

  VrRuntimeState apply(SessionSignal signal);
  void reset();

 private:
  VrRuntimeState state_ = VR_RUNTIME_STATE_STOPPED;
  bool session_active_ = false;
};

}  // namespace vrclient::runtime::frame

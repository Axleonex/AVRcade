#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>

namespace gtasa_vr {
struct Vec3 { float x{}, y{}, z{}; };
struct Quaternion { float x{}, y{}, z{}, w{1}; };
struct GtaPadState {
  int16_t leftStickX{},leftStickY{},rightStickX{},rightStickY{};
  int16_t leftShoulder1{},leftShoulder2{},rightShoulder1{},rightShoulder2{};
  int16_t dPadUp{},dPadDown{},dPadLeft{},dPadRight{};
  int16_t start{},select{};
  int16_t buttonSquare{},buttonTriangle{},buttonCross{},buttonCircle{};
  int16_t shockButtonL{},shockButtonR{},chatIndicated{},pedWalk{},vehicleMouseLook{},radioTrackSkip{};
};
static_assert(sizeof(GtaPadState)==0x30);
inline int16_t GtaAxis(float value) {
  if (!std::isfinite(value)) return 0;
  return static_cast<int16_t>(std::lround(std::clamp(value,-1.0f,1.0f)*128.0f));
}
inline int16_t GtaButton(bool pressed) { return pressed ? 255 : 0; }
inline bool BuildGtaPadState(Vec3 moveAndUnused,Vec3 lookAndUnused,
                             bool jump,bool enterVehicle,bool sprint,bool fire,
                             float aim,float triggerFire,float gripSprint,
                             GtaPadState& out) {
  if (!std::isfinite(moveAndUnused.x)||!std::isfinite(moveAndUnused.y)||
      !std::isfinite(moveAndUnused.z)||!std::isfinite(lookAndUnused.x)||
      !std::isfinite(lookAndUnused.y)||!std::isfinite(lookAndUnused.z)||!std::isfinite(aim)||
      !std::isfinite(triggerFire)||!std::isfinite(gripSprint)) return false;
  out={};
  out.leftStickX=GtaAxis(moveAndUnused.x);
  out.leftStickY=GtaAxis(-moveAndUnused.y);
  out.rightStickX=GtaAxis(lookAndUnused.x);
  out.rightStickY=GtaAxis(-lookAndUnused.y);
  out.buttonSquare=GtaButton(jump);
  out.buttonTriangle=GtaButton(enterVehicle);
  out.buttonCross=GtaButton(sprint||gripSprint>=0.55f);
  out.buttonCircle=GtaButton(fire||triggerFire>=0.55f);
  out.rightShoulder1=GtaButton(aim>=0.55f);
  return true;
}
inline bool BuildGtaMenuPadState(Vec3 move,Vec3 look,bool select,bool back,
                                 float selectTrigger,float backTrigger,GtaPadState& out) {
  if (!std::isfinite(move.x)||!std::isfinite(move.y)||!std::isfinite(look.x)||
      !std::isfinite(look.y)||!std::isfinite(selectTrigger)||
      !std::isfinite(backTrigger)) return false;
  out={};
  out.leftStickX=GtaAxis(move.x);
  out.leftStickY=GtaAxis(-move.y);
  out.rightStickX=GtaAxis(look.x);
  out.rightStickY=GtaAxis(-look.y);
  out.buttonCross=GtaButton(select||selectTrigger>=0.55f);
  out.buttonTriangle=GtaButton(back||backTrigger>=0.55f);
  return true;
}
inline void MergeGtaPadState(GtaPadState& destination,const GtaPadState& vr) {
  auto mergeAxis=[](int16_t& current,int16_t incoming) {
    if (incoming==0) return;
    if (current==0||(current>0)==(incoming>0)) {
      if (std::abs(static_cast<int>(incoming))>std::abs(static_cast<int>(current)))
        current=incoming;
    } else {
      current=0;
    }
  };
  mergeAxis(destination.leftStickX,vr.leftStickX);
  mergeAxis(destination.leftStickY,vr.leftStickY);
  mergeAxis(destination.rightStickX,vr.rightStickX);
  mergeAxis(destination.rightStickY,vr.rightStickY);
  auto* destinationButtons=&destination.leftShoulder1;
  const auto* vrButtons=&vr.leftShoulder1;
  constexpr size_t buttonCount=(sizeof(GtaPadState)-offsetof(GtaPadState,leftShoulder1))/sizeof(int16_t);
  for (size_t index=0;index<buttonCount;++index)
    if (vrButtons[index]!=0) destinationButtons[index]=std::max(destinationButtons[index],vrButtons[index]);
}
inline Quaternion Conjugate(Quaternion q) { return {-q.x,-q.y,-q.z,q.w}; }
inline Quaternion Multiply(Quaternion a, Quaternion b) {
  return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
          a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
          a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
          a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
inline Vec3 Rotate(Quaternion q, Vec3 v) {
  auto r=Multiply(Multiply(q,{v.x,v.y,v.z,0}),Conjugate(q));
  return {r.x,r.y,r.z};
}
inline Vec3 Add(Vec3 a,Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec3 Subtract(Vec3 a,Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec3 Scale(Vec3 v,float s) { return {v.x*s,v.y*s,v.z*s}; }
inline float Dot(Vec3 a,Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec3 Cross(Vec3 a,Vec3 b) {
  return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
inline float Length(Vec3 v) { return std::sqrt(Dot(v,v)); }
inline bool Finite(Vec3 v) {
  return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
}
inline bool Normalize(Vec3 v,Vec3& out) {
  const float length=Length(v);
  if (!Finite(v)||!std::isfinite(length)||length<0.00001f) return false;
  out=Scale(v,1.0f/length);
  return true;
}
inline bool Normalize(Quaternion q,Quaternion& out) {
  const float length=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
  if (!std::isfinite(length)||length<0.00001f) return false;
  out={q.x/length,q.y/length,q.z/length,q.w/length};
  return true;
}
inline Quaternion RotationBetween(Vec3 from,Vec3 to) {
  Vec3 a{},b{};
  if (!Normalize(from,a)||!Normalize(to,b)) return {};
  const float cosine=std::clamp(Dot(a,b),-1.0f,1.0f);
  if (cosine>0.9999f) return {};
  if (cosine<-0.9999f) {
    Vec3 axis=Cross(a,{0,0,1});
    if (!Normalize(axis,axis)) Normalize(Cross(a,{0,1,0}),axis);
    return {axis.x,axis.y,axis.z,0};
  }
  const Vec3 axis=Cross(a,b);
  Quaternion result{axis.x,axis.y,axis.z,1.0f+cosine},normalized{};
  return Normalize(result,normalized) ? normalized : Quaternion{};
}
// GTA CCamera CMatrix is right, forward, up; it is NOT an RwMatrix.
inline Vec3 ToGame(Vec3 right,Vec3 forward,Vec3 up,Vec3 v) {
  return Add(Add(Scale(right,v.x),Scale(up,v.y)),Scale(forward,-v.z));
}
inline Quaternion CorrectHorizontalTracking(Quaternion q) {
  // Preserve pitch while reflecting the headset's observed horizontal frame.
  // Mirroring the quaternion's axial vector this way still produces a proper
  // rotation; negating GTA's right basis would instead create a left-handed
  // camera and reverse stereo translation as well as yaw.
  return {q.x,-q.y,-q.z,q.w};
}
struct Basis { Vec3 right,forward,up,position; };
enum class TurnMode { Snap, Smooth };
struct SnapTurnState {
  float yawRadians{};
  float pendingBodyRadians{};
  float cameraCatchupRadians{};
  float previousCameraHeadingRadians{};
  bool cameraHeadingKnown{};
  bool armed{true};
};
constexpr float kFullCircleRadians=6.283185307f;
inline bool AdvanceSnapTurn(SnapTurnState& state,float stickX,bool gameplayAllowed,
                            float snapDegrees=30.0f) {
  if (!gameplayAllowed || !std::isfinite(stickX) ||
      !std::isfinite(snapDegrees) || snapDegrees<15.0f || snapDegrees>90.0f) {
    state.armed=false;
    return false;
  }
  constexpr float rearmThreshold=0.35f;
  constexpr float turnThreshold=0.75f;
  const float snapRadians=snapDegrees*0.01745329252f;
  if (std::abs(stickX)<=rearmThreshold) {
    state.armed=true;
    return false;
  }
  if (!state.armed || std::abs(stickX)<turnThreshold) return false;
  state.yawRadians=std::remainder(
      state.yawRadians+(stickX>0.0f ? snapRadians : -snapRadians),kFullCircleRadians);
  state.pendingBodyRadians=std::remainder(
      state.pendingBodyRadians+(stickX>0.0f ? snapRadians : -snapRadians),kFullCircleRadians);
  state.armed=false;
  return true;
}
inline bool AdvanceSmoothTurn(SnapTurnState& state,float stickX,float elapsedSeconds,
                              float degreesPerSecond,bool gameplayAllowed) {
  if (!gameplayAllowed) {
    state.armed=false;
    return false;
  }
  if (!std::isfinite(stickX)||!std::isfinite(elapsedSeconds)||
      !std::isfinite(degreesPerSecond)||elapsedSeconds<=0.0f||
      elapsedSeconds>0.10f||degreesPerSecond<30.0f||degreesPerSecond>180.0f)
    return false;
  constexpr float deadZone=0.20f;
  const float magnitude=std::clamp(std::abs(stickX),0.0f,1.0f);
  if (magnitude<=deadZone) return false;
  const float scaled=(magnitude-deadZone)/(1.0f-deadZone);
  const float radians=std::copysign(
      scaled*degreesPerSecond*0.01745329252f*std::min(elapsedSeconds,0.05f),stickX);
  state.yawRadians=std::remainder(state.yawRadians+radians,kFullCircleRadians);
  state.pendingBodyRadians=std::remainder(
      state.pendingBodyRadians+radians,kFullCircleRadians);
  return true;
}
inline void CancelPendingBodyTurn(SnapTurnState& state) {
  state.yawRadians=std::remainder(
      state.yawRadians-state.pendingBodyRadians,kFullCircleRadians);
  state.pendingBodyRadians=0.0f;
  state.armed=false;
}
inline void CommitBodyTurn(SnapTurnState& state) {
  state.cameraCatchupRadians=std::remainder(
      state.cameraCatchupRadians+state.pendingBodyRadians,kFullCircleRadians);
  state.pendingBodyRadians=0.0f;
}
inline bool ReconcileCameraTurn(SnapTurnState& state,Vec3 cameraForward) {
  if (!Finite(cameraForward) ||
      cameraForward.x*cameraForward.x+cameraForward.y*cameraForward.y<0.01f)
    return false;
  const float heading=std::atan2(cameraForward.x,cameraForward.y);
  if (state.cameraHeadingKnown) {
    const float delta=std::remainder(
        heading-state.previousCameraHeadingRadians,kFullCircleRadians);
    // A camera jump (cutscene, teleport, mod camera) is not body catch-up.
    if (std::abs(delta)<0.785398163f &&
        delta*state.cameraCatchupRadians>0.0f) {
      const float consumed=std::copysign(
          std::min(std::abs(delta),std::abs(state.cameraCatchupRadians)),delta);
      state.yawRadians=std::remainder(state.yawRadians-consumed,kFullCircleRadians);
      state.cameraCatchupRadians-=consumed;
    }
  }
  state.previousCameraHeadingRadians=heading;
  state.cameraHeadingKnown=true;
  return true;
}
inline Basis YawBasis(Basis base,float clockwiseRadians) {
  if (!std::isfinite(clockwiseRadians)) return base;
  const float cosine=std::cos(clockwiseRadians),sine=std::sin(clockwiseRadians);
  const auto rotate=[&](Vec3 v) {
    return Vec3{cosine*v.x+sine*v.y,-sine*v.x+cosine*v.y,v.z};
  };
  base.right=rotate(base.right);
  base.forward=rotate(base.forward);
  base.up=rotate(base.up);
  return base;
}
inline bool HeadRelativeMove(Vec3 stick,const Basis& gameCamera,
                             const Basis& renderedHead,Vec3& out) {
  if (!Finite(stick)) return false;
  Vec3 gameForward{},headForward{};
  if (!Normalize({gameCamera.forward.x,gameCamera.forward.y,0},gameForward) ||
      !Normalize({renderedHead.forward.x,renderedHead.forward.y,0},headForward))
    return false;
  // The on-foot game interprets CPad axes in its own camera frame. Only the
  // horizontal difference between that frame and the rendered VR head belongs
  // in the stick; pitch, roll and eye translation must not affect walking.
  const Vec3 gameRight{gameForward.y,-gameForward.x,0};
  const Vec3 headRight{headForward.y,-headForward.x,0};
  const Vec3 desired=Add(Scale(headRight,stick.x),Scale(headForward,stick.y));
  const float magnitude=Length(desired);
  if (!std::isfinite(magnitude)) return false;
  const Vec3 bounded=magnitude>1.0f ? Scale(desired,1.0f/magnitude) : desired;
  out={Dot(bounded,gameRight),Dot(bounded,gameForward),0};
  return Finite(out);
}
inline bool RigidlyRebase(const Basis& sourceRoot,const Basis& targetRoot,
                          const Basis& sourceChild,Basis& out) {
  Vec3 sourceRight{},sourceForward{},sourceUp{};
  Vec3 targetRight{},targetForward{},targetUp{};
  if (!Finite(sourceRoot.position)||!Finite(targetRoot.position)||
      !Finite(sourceChild.position)||
      !Normalize(sourceRoot.right,sourceRight)||
      !Normalize(sourceRoot.forward,sourceForward)||
      !Normalize(sourceRoot.up,sourceUp)||
      !Normalize(targetRoot.right,targetRight)||
      !Normalize(targetRoot.forward,targetForward)||
      !Normalize(targetRoot.up,targetUp)) return false;
  constexpr float minimumHandedness=0.90f;
  // GTA's left-hand skin bones can carry an orthonormal reflected basis.  A
  // negative determinant is still a valid rigid frame; rejecting it cancels
  // the complete two-arm update after the IK solve has already succeeded.
  if (std::abs(Dot(Cross(sourceRight,sourceForward),sourceUp))<minimumHandedness||
      std::abs(Dot(Cross(targetRight,targetForward),targetUp))<minimumHandedness) return false;
  auto rebaseDirection=[&](Vec3 direction) {
    return Add(Add(Scale(targetRight,Dot(direction,sourceRight)),
                   Scale(targetForward,Dot(direction,sourceForward))),
               Scale(targetUp,Dot(direction,sourceUp)));
  };
  out.right=rebaseDirection(sourceChild.right);
  out.forward=rebaseDirection(sourceChild.forward);
  out.up=rebaseDirection(sourceChild.up);
  out.position=Add(targetRoot.position,
      rebaseDirection(Subtract(sourceChild.position,sourceRoot.position)));
  return Finite(out.right)&&Finite(out.forward)&&Finite(out.up)&&Finite(out.position);
}
inline bool ApplyControllerOrientation(const Basis& referenceController,
                                       const Basis& referenceHand,
                                       const Basis& currentController,
                                       Basis& out) {
  Basis handAtController=referenceHand;
  handAtController.position=referenceController.position;
  if (!RigidlyRebase(referenceController,currentController,handAtController,out)) return false;
  out.position=currentController.position;
  return Finite(out.position);
}
inline bool AlignHandToController(const Basis& nativeHand,Vec3 nativeFingerPosition,
                                  const Basis& controller,Basis& out) {
  Vec3 fingerForward{};
  if (!Normalize(Subtract(nativeFingerPosition,nativeHand.position),fingerForward)) return false;
  auto projectedUp=[&](Vec3 candidate,Vec3& result) {
    return Normalize(Subtract(candidate,Scale(fingerForward,Dot(candidate,fingerForward))),result);
  };
  Vec3 palmUp{};
  if (!projectedUp(nativeHand.up,palmUp)&&
      !projectedUp(nativeHand.forward,palmUp)&&
      !projectedUp(nativeHand.right,palmUp)) return false;
  Vec3 palmRight{};
  if (!Normalize(Cross(fingerForward,palmUp),palmRight) ||
      !Normalize(Cross(palmRight,fingerForward),palmUp)) return false;
  const Basis anatomicalBind{palmRight,fingerForward,palmUp,nativeHand.position};
  return ApplyControllerOrientation(anatomicalBind,nativeHand,controller,out);
}
inline bool MoveHandPreservingOrientation(const Basis& nativeHand,Vec3 target,Basis& out) {
  if (!Finite(nativeHand.right)||!Finite(nativeHand.forward)||!Finite(nativeHand.up)||
      !Finite(nativeHand.position)||!Finite(target)) return false;
  out=nativeHand;
  out.position=target;
  return true;
}
inline bool AlignHandForwardWithControllerRoll(
    const Basis& nativeHand,Vec3 nativeFingerPosition,Vec3 stableForward,Vec3 stableUp,
    Vec3 controllerUp,Vec3 target,Basis& out) {
  Vec3 fingerForward{};
  if (!Normalize(Subtract(nativeFingerPosition,nativeHand.position),fingerForward)) return false;
  auto projected=[&](Vec3 candidate,Vec3 axis,Vec3& result) {
    return Normalize(Subtract(candidate,Scale(axis,Dot(candidate,axis))),result);
  };
  Vec3 nativePalmUp{};
  if (!projected(nativeHand.up,fingerForward,nativePalmUp)&&
      !projected(nativeHand.forward,fingerForward,nativePalmUp)&&
      !projected(nativeHand.right,fingerForward,nativePalmUp)) return false;
  Vec3 nativePalmRight{};
  if (!Normalize(Cross(fingerForward,nativePalmUp),nativePalmRight)||
      !Normalize(Cross(nativePalmRight,fingerForward),nativePalmUp)) return false;

  Vec3 targetForward{},targetPalmUp{};
  if (!Normalize(stableForward,targetForward)||
      (!projected(controllerUp,targetForward,targetPalmUp)&&
       !projected(stableUp,targetForward,targetPalmUp))) return false;
  Vec3 targetPalmRight{};
  if (!Normalize(Cross(targetForward,targetPalmUp),targetPalmRight)||
      !Normalize(Cross(targetPalmRight,targetForward),targetPalmUp)) return false;

  const Basis nativeFrame{nativePalmRight,fingerForward,nativePalmUp,nativeHand.position};
  const Basis targetFrame{targetPalmRight,targetForward,targetPalmUp,target};
  if (!RigidlyRebase(nativeFrame,targetFrame,nativeHand,out)) return false;
  out.position=target;
  return Finite(out.position);
}
inline bool HandReachForward(Vec3 shoulder,Vec3 wrist,Vec3 bodyForward,Vec3 bodyUp,
                             Vec3& out) {
  Vec3 up{};
  if (!Finite(shoulder)||!Finite(wrist)||!Normalize(bodyUp,up)) return false;
  const Vec3 reach=Subtract(wrist,shoulder);
  const Vec3 horizontal=Subtract(reach,Scale(up,Dot(reach,up)));
  if (Normalize(horizontal,out)) return true;
  const Vec3 fallback=Subtract(bodyForward,Scale(up,Dot(bodyForward,up)));
  return Normalize(fallback,out);
}
inline bool GameplayHandForward(Vec3 shoulder,Vec3 wrist,Vec3 bodyForward,Vec3 bodyRight,
                                Vec3 bodyUp,bool leftHand,Vec3& out) {
  Vec3 up{},forward{},right{},reachForward{};
  if (!Normalize(bodyUp,up)||
      !Normalize(Subtract(bodyForward,Scale(up,Dot(bodyForward,up))),forward)||
      !Normalize(Subtract(bodyRight,Scale(up,Dot(bodyRight,up))),right)||
      !HandReachForward(shoulder,wrist,forward,up,reachForward)) return false;
  const float lateral=Dot(Subtract(wrist,shoulder),right);
  const float outward=leftHand ? -lateral : lateral;
  constexpr float outwardBlendStart=0.25f;
  constexpr float outwardBlendEnd=0.40f;
  if (outward<=outwardBlendStart) {
    out=forward;
    return true;
  }
  const float blend=std::clamp(
      (outward-outwardBlendStart)/(outwardBlendEnd-outwardBlendStart),0.0f,1.0f);
  return Normalize(Add(Scale(forward,1.0f-blend),Scale(reachForward,blend)),out);
}
struct FilteredTrackingPose { Vec3 position{}; Quaternion orientation{}; };
inline bool SmoothTrackedPose(const FilteredTrackingPose& previous,
                              const FilteredTrackingPose& current,
                              float alpha,float positionDeadband,
                              FilteredTrackingPose& out) {
  if (!Finite(previous.position)||!Finite(current.position)||
      !std::isfinite(alpha)||alpha<=0.0f||alpha>1.0f||
      !std::isfinite(positionDeadband)||positionDeadband<0.0f) return false;
  Quaternion prior{},next{};
  if (!Normalize(previous.orientation,prior)||!Normalize(current.orientation,next)) return false;
  const Vec3 delta=Subtract(current.position,previous.position);
  out.position=Length(delta)<=positionDeadband
      ? previous.position : Add(previous.position,Scale(delta,alpha));
  const float quaternionDot=prior.x*next.x+prior.y*next.y+prior.z*next.z+prior.w*next.w;
  if (quaternionDot<0.0f) next={-next.x,-next.y,-next.z,-next.w};
  const Quaternion blended{
      prior.x+(next.x-prior.x)*alpha,prior.y+(next.y-prior.y)*alpha,
      prior.z+(next.z-prior.z)*alpha,prior.w+(next.w-prior.w)*alpha};
  return Normalize(blended,out.orientation)&&Finite(out.position);
}
inline bool HeadAnchoredBasis(Basis base,Vec3 head,float forwardOffset,Basis& out) {
  if (!Finite(head)||!std::isfinite(forwardOffset)||forwardOffset<0.0f||forwardOffset>1.0f)
    return false;
  out=base;
  out.position=Add(head,Scale(base.forward,forwardOffset));
  return Finite(out.position);
}
inline Basis TrackedPose(Basis base,Quaternion reference,Vec3 referencePosition,
                         Quaternion orientation,Vec3 position,
                         bool correctHorizontalOrientation,
                         bool correctHorizontalPosition) {
  auto inverse=Conjugate(reference);
  auto delta=Multiply(inverse,orientation);
  if (correctHorizontalOrientation) delta=CorrectHorizontalTracking(delta);
  auto local=Rotate(inverse,Add(position,Scale(referencePosition,-1)));
  if (correctHorizontalPosition) local.x=-local.x;
  auto map=[&](Vec3 v) { return ToGame(base.right,base.forward,base.up,v); };
  return {map(Rotate(delta,{1,0,0})),map(Rotate(delta,{0,0,-1})),
          map(Rotate(delta,{0,1,0})),Add(base.position,map(local))};
}
inline Basis EyePose(Basis base,Quaternion reference,Vec3 referencePosition,
                     Quaternion orientation,Vec3 position,
                     bool correctHorizontalTracking=false) {
  return TrackedPose(base,reference,referencePosition,orientation,position,
                     correctHorizontalTracking,correctHorizontalTracking);
}
inline Basis ControllerPose(Basis base,Quaternion reference,Vec3 referencePosition,
                            Quaternion orientation,Vec3 position,
                            bool correctHorizontalTracking=false) {
  return TrackedPose(base,reference,referencePosition,orientation,position,
                     correctHorizontalTracking,correctHorizontalTracking);
}
inline bool TrackedWorldPosition(Basis base,Quaternion reference,Vec3 referencePosition,
                                 Vec3 trackedPosition,Vec3& out) {
  if (!Finite(base.position)||!Finite(trackedPosition)||!Finite(referencePosition)) return false;
  Quaternion inverse{};
  if (!Normalize(Conjugate(reference),inverse)) return false;
  const Vec3 local=Rotate(inverse,Subtract(trackedPosition,referencePosition));
  out=Add(base.position,ToGame(base.right,base.forward,base.up,local));
  return Finite(out);
}
struct TwoBoneSolution { Vec3 elbow{},wrist{}; bool clamped{}; };
inline bool SolveTwoBone(Vec3 shoulder,Vec3 animatedElbow,Vec3 target,
                         float upperLength,float forearmLength,TwoBoneSolution& out) {
  if (!Finite(shoulder)||!Finite(animatedElbow)||!Finite(target)||
      !std::isfinite(upperLength)||!std::isfinite(forearmLength)||
      upperLength<0.01f||forearmLength<0.01f) return false;
  Vec3 direction{};
  const Vec3 toTarget=Subtract(target,shoulder);
  const float requestedDistance=Length(toTarget);
  if (!Normalize(toTarget,direction)) return false;
  constexpr float epsilon=0.001f;
  const float minimum=std::abs(upperLength-forearmLength)+epsilon;
  const float maximum=upperLength+forearmLength-epsilon;
  if (minimum>=maximum) return false;
  const float distance=std::clamp(requestedDistance,minimum,maximum);
  Vec3 pole=Subtract(animatedElbow,shoulder);
  pole=Subtract(pole,Scale(direction,Dot(pole,direction)));
  if (!Normalize(pole,pole)) {
    pole=Cross({0,0,1},direction);
    if (!Normalize(pole,pole)) {
      pole=Cross({0,1,0},direction);
      if (!Normalize(pole,pole)) return false;
    }
  }
  const float along=(upperLength*upperLength+distance*distance-
                     forearmLength*forearmLength)/(2.0f*distance);
  const float height=std::sqrt(std::max(0.0f,upperLength*upperLength-along*along));
  out.elbow=Add(shoulder,Add(Scale(direction,along),Scale(pole,height)));
  out.wrist=Add(shoulder,Scale(direction,distance));
  out.clamped=std::abs(distance-requestedDistance)>epsilon;
  return Finite(out.elbow)&&Finite(out.wrist);
}
struct Frustum {
  float horizontal{},vertical{},u{},v{},width{},height{};
  float viewWindowX{},viewWindowY{},viewOffsetX{},viewOffsetY{};
};
inline bool MakeFrustum(float left,float right,float up,float down,Frustum& out) {
  constexpr float limit=1.5607963f;
  for (float angle : {left,right,up,down})
    if (!std::isfinite(angle)||std::abs(angle)>=limit) return false;
  float l=std::tan(left),r=std::tan(right),u=std::tan(up),d=std::tan(down);
  if (l>=0||r<=0||d>=0||u<=0) return false;
  out.horizontal=std::max(-l,r); out.vertical=std::max(-d,u);
  out.u=(l+out.horizontal)/(2*out.horizontal);
  out.v=(out.vertical-u)/(2*out.vertical);
  out.width=(r-l)/(2*out.horizontal); out.height=(u-d)/(2*out.vertical);
  // RenderWare can express the OpenXR asymmetric frustum directly.  Keeping
  // the full viewport prevents GTA's 2D HUD from being cropped to the
  // asymmetric sub-rectangle after it has already been drawn.
  out.viewWindowX=(r-l)*0.5f; out.viewWindowY=(u-d)*0.5f;
  out.viewOffsetX=(r+l)*0.5f; out.viewOffsetY=(u+d)*0.5f;
  return true;
}
inline bool MakeTrackedFrustum(float left,float right,float up,float down,
                               bool correctHorizontalTracking,Frustum& out) {
  if (correctHorizontalTracking)
    return MakeFrustum(-right,-left,up,down,out);
  return MakeFrustum(left,right,up,down,out);
}
inline bool UseCinematicMode(bool frontEndMenu,bool fullyFaded,
                             bool cutsceneRunning,bool cutsceneProcessing) {
  return frontEndMenu||fullyFaded||cutsceneRunning||cutsceneProcessing;
}
struct SourceRect { float u{},v{},width{},height{}; };
inline SourceRect FullEyeSourceRect(unsigned eye) {
  return {static_cast<float>(eye)*0.5f,0.0f,0.5f,1.0f};
}
inline unsigned SubmittedArrayIndex(unsigned eye,bool swapEyes) {
  return swapEyes&&eye<2 ? 1u-eye : eye;
}
enum class HudCluster { None, Radar, VitalStats, PlayerStatus };
struct HudPoint { float x{},y{}; };
inline HudPoint VrHudPoint(HudCluster cluster,float width,float height,float x,float y) {
  if (cluster==HudCluster::Radar) {
    constexpr float scale=0.55f;
    return {width*0.27f+x*scale,height*0.04f+(y-height*0.65f)*scale};
  }
  if (cluster==HudCluster::VitalStats) {
    constexpr float scale=0.55f;
    return {width*0.27f+x*scale,height*0.04f+y*scale};
  }
  if (cluster==HudCluster::PlayerStatus) {
    constexpr float scale=0.75f;
    return {width*0.73f+(x-width)*scale,height*0.04f+y*scale};
  }
  return {x,y};
}
inline bool CanSendInput(bool running,bool focused,bool foreground,bool synced) {
  return running&&focused&&foreground&&synced;
}
}

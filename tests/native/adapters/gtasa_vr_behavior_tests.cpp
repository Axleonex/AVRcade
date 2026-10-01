#include "gtasa_vr_math.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace gtasa_vr;
static int failures=0;
static void Check(bool value,const char* name) {
  if (!value) { std::cerr << "FAIL: " << name << '\n'; ++failures; }
}
static bool Near(float a,float b) { return std::abs(a-b)<0.001f; }
static bool Near(Vec3 a,Vec3 b) { return Near(a.x,b.x)&&Near(a.y,b.y)&&Near(a.z,b.z); }
int main() {
  const Basis base{{1,0,0},{0,1,0},{0,0,1},{100,200,300}};
  auto eye=EyePose(base,{}, {}, {}, {0,1,0});
  Check(Near(eye.position,{100,200,301}),"head up is world up, not forward");
  Quaternion yaw{0,0.258819045f,0,0.965925826f};
  eye=EyePose(base,{}, {}, yaw, {});
  Check(Near(eye.up,{0,0,1}),"head yaw must not roll the camera");
  Check(Near(eye.forward,{-0.5f,0.8660254f,0}),"head yaw turns game forward axis");
  eye=EyePose(base,{}, {}, yaw, {}, true);
  Check(Near(eye.forward,{0.5f,0.8660254f,0}),
        "headset horizontal compatibility correction reverses yaw without mirroring the camera");
  Check(Near(eye.right,{0.8660254f,-0.5f,0})&&Near(eye.up,{0,0,1}),
        "horizontal correction keeps a right-handed orthonormal camera basis");
  eye=EyePose(base,yaw,{},yaw,Rotate(yaw,{0,0,-1}));
  Check(Near(eye.position,{100,201,300}),"translation shares reference orientation frame");
  Check(Near(eye.forward,base.forward),"reference orientation cancels at recenter");
  auto left=EyePose(base,yaw,{},yaw,Rotate(yaw,{-0.032f,0,0}));
  auto right=EyePose(base,yaw,{},yaw,Rotate(yaw,{0.032f,0,0}));
  Check(Near(right.position.x-left.position.x,0.064f),"IPD preserved after recenter");
  const Basis mirroredCamera{{0,1,0},{1,0,0},{0,0,1},{100,200,300}};
  const auto leftController=ControllerPose(
      mirroredCamera,{}, {},{}, {-0.20f,0,0},true);
  const auto rightController=ControllerPose(
      mirroredCamera,{}, {},{}, {0.20f,0,0},true);
  Check(Near(leftController.position,{100,200.20f,300})&&
        Near(rightController.position,{100,199.80f,300}),
        "controller horizontal correction keeps anatomical left and right from crossing");
  const auto correctedLeftEye=EyePose(base,{}, {},{}, {-0.032f,0,0},true);
  const auto correctedRightEye=EyePose(base,{}, {},{}, {0.032f,0,0},true);
  Check(Near(correctedLeftEye.position,{100.032f,200,300})&&
        Near(correctedRightEye.position,{99.968f,200,300}),
        "horizontal correction transforms eye translation in the same frame as eye orientation");
  Check(Near(correctedLeftEye.position.x-correctedRightEye.position.x,0.064f),
        "corrected left and right eye poses keep a coherent non-crossed IPD baseline");
  Basis firstPerson{};
  Check(HeadAnchoredBasis(base,{11,22,33},0.12f,firstPerson),
        "finite player head produces a first-person basis");
  Check(Near(firstPerson.position,{11,22.12f,33}),
        "first-person basis is anchored at the head and moved beyond the face");
  Check(Near(firstPerson.right,base.right)&&Near(firstPerson.forward,base.forward)&&
        Near(firstPerson.up,base.up),"head anchoring preserves the game camera orientation");
  SnapTurnState turn{};
  Check(!AdvanceSnapTurn(turn,0.50f,true),"partial stick deflection does not snap turn");
  Check(AdvanceSnapTurn(turn,0.90f,true),"right stick crosses snap threshold once");
  Check(Near(turn.yawRadians,0.523598776f),"right turn is thirty degrees");
  Check(!AdvanceSnapTurn(turn,0.90f,true),"held stick cannot repeat a snap turn");
  Check(!AdvanceSnapTurn(turn,0.0f,true)&&turn.armed,
        "stick returning to center rearms snap turn");
  Check(AdvanceSnapTurn(turn,-0.90f,true)&&Near(turn.yawRadians,0.0f),
        "left snap reverses the previous right snap");
  SnapTurnState variableSnap{};
  for (const float angle : {15.0f,30.0f,45.0f,60.0f,90.0f}) {
    Check(AdvanceSnapTurn(variableSnap,1.0f,true,angle)&&
          Near(variableSnap.yawRadians,angle*0.01745329252f)&&
          Near(variableSnap.pendingBodyRadians,angle*0.01745329252f),
          "selected snap angle rotates the visual rig and player body equally");
    AdvanceSnapTurn(variableSnap,0.0f,true,angle);
    Check(AdvanceSnapTurn(variableSnap,-1.0f,true,angle)&&
          Near(variableSnap.yawRadians,0.0f)&&
          Near(variableSnap.pendingBodyRadians,0.0f),
          "opposite snap cancels the selected angle");
    AdvanceSnapTurn(variableSnap,0.0f,true,angle);
  }
  Check(!AdvanceSnapTurn(variableSnap,1.0f,true,10.0f)&&
        Near(variableSnap.yawRadians,0.0f),
        "out-of-range snap angles cannot move the rig");
  Check(!AdvanceSnapTurn(turn,1.0f,false)&&!turn.armed,
        "menu or tracking loss cannot turn or arm the rig");
  Check(!AdvanceSnapTurn(turn,1.0f,true),
        "returning to gameplay with a held stick cannot cause a surprise turn");
  SnapTurnState smooth{};
  Check(!AdvanceSmoothTurn(smooth,1.0f,1.0f/90.0f,90.0f,false)&&
        Near(smooth.yawRadians,0.0f),
        "smooth turning is gated outside on-foot stereo gameplay");
  Check(AdvanceSmoothTurn(smooth,1.0f,1.0f/90.0f,90.0f,true)&&
        Near(smooth.yawRadians,0.017453293f)&&
        Near(smooth.pendingBodyRadians,smooth.yawRadians),
        "a held right stick smoothly turns the view and queues the same body angle");
  Check(!AdvanceSmoothTurn(smooth,0.10f,1.0f/90.0f,90.0f,true)&&
        Near(smooth.yawRadians,0.017453293f),
        "smooth-turn dead zone suppresses stick drift");
  Check(!AdvanceSmoothTurn(smooth,1.0f,0.50f,90.0f,true)&&
        Near(smooth.yawRadians,0.017453293f),
        "a stalled frame cannot produce a surprise large rotation");
  Check(AdvanceSmoothTurn(smooth,-1.0f,1.0f/90.0f,90.0f,true)&&
        Near(smooth.yawRadians,0.0f)&&Near(smooth.pendingBodyRadians,0.0f),
        "smooth left and right turns are symmetric");
  AdvanceSnapTurn(turn,0.0f,true);
  Check(AdvanceSnapTurn(turn,1.0f,true),"released stick can turn after gameplay resumes");
  Check(Near(turn.pendingBodyRadians,0.523598776f),
        "one visual snap queues exactly one matching player-body turn");
  const Basis turned=YawBasis(firstPerson,turn.yawRadians);
  Check(Near(turned.position,firstPerson.position)&&
        Near(turned.forward,{0.5f,0.8660254f,0})&&
        Near(turned.right,{0.8660254f,-0.5f,0})&&Near(turned.up,{0,0,1}),
        "rig yaw rotates the whole head-anchored basis without moving its pivot");
  Vec3 alignedMove{};
  Check(HeadRelativeMove({0,1,0},base,base,alignedMove)&&Near(alignedMove,{0,1,0}),
        "neutral head preserves forward locomotion");
  Check(HeadRelativeMove({0,1,0},base,turned,alignedMove)&&
        Near(alignedMove,{0.5f,0.8660254f,0}),
        "forward locomotion follows a rightward rig snap");
  const auto physicallyTurned=EyePose(base,{}, {},
      {0,0.70710678f,0,0.70710678f},{},true);
  Check(HeadRelativeMove({0,1,0},base,physicallyTurned,alignedMove)&&
        Near(alignedMove,{1,0,0}),
        "forward locomotion follows a ninety-degree physical head turn");
  Check(HeadRelativeMove({1,0,0},base,physicallyTurned,alignedMove)&&
        Near(alignedMove,{0,-1,0}),
        "strafing remains rightward relative to a physically turned head");
  const auto pitched=EyePose(base,{}, {},
      {0.5f,0,0,0.8660254f},{},true);
  Check(HeadRelativeMove({0,1,0},base,pitched,alignedMove)&&
        Near(alignedMove,{0,1,0}),
        "looking downward does not slow or reverse horizontal locomotion");
  const auto turnedLeftEye=EyePose(turned,{}, {},{}, {-0.032f,0,0},true);
  const auto turnedRightEye=EyePose(turned,{}, {},{}, {0.032f,0,0},true);
  const auto turnedHand=ControllerPose(turned,{}, {},{}, {-0.20f,0,-0.30f},true);
  Check(Near(Length(Subtract(turnedLeftEye.position,turnedRightEye.position)),0.064f)&&
        Near(Subtract(turnedHand.position,turned.position),
             Add(Scale(turned.right,0.20f),Scale(turned.forward,0.30f))),
        "stereo separation and controller position share exactly the same turned rig");
  CommitBodyTurn(turn);
  Check(Near(turn.pendingBodyRadians,0.0f)&&
        Near(turn.cameraCatchupRadians,0.523598776f),
        "body turn commits once and awaits only GTA camera follow-through");
  Check(ReconcileCameraTurn(turn,{0,1,0})&&
        ReconcileCameraTurn(turn,{0.5f,0.8660254f,0})&&
        Near(turn.yawRadians,0.0f)&&Near(turn.cameraCatchupRadians,0.0f),
        "native camera catch-up replaces rather than doubles the visual snap");
  AdvanceSnapTurn(turn,0.0f,true);
  Check(AdvanceSnapTurn(turn,-1.0f,true),"opposite turn can be queued");
  CancelPendingBodyTurn(turn);
  Check(Near(turn.pendingBodyRadians,0.0f)&&Near(turn.yawRadians,0.0f),
        "lost focus before simulation cancels the unsynchronized visual turn");
  Check(!HeadAnchoredBasis(base,{std::numeric_limits<float>::quiet_NaN(),22,33},0.12f,
                           firstPerson),"invalid head coordinates are rejected");
  Frustum f;
  Check(MakeFrustum(-0.9f,0.7f,0.8f,-0.6f,f),"asymmetric FOV accepted");
  Check(Near((2*f.u-1)*f.horizontal,std::tan(-0.9f)),"left ray matches submitted FOV");
  Check(Near((2*(f.u+f.width)-1)*f.horizontal,std::tan(0.7f)),"right ray matches submitted FOV");
  Check(Near((1-2*f.v)*f.vertical,std::tan(0.8f)),"top ray matches submitted FOV");
  Check(Near((1-2*(f.v+f.height))*f.vertical,std::tan(-0.6f)),"bottom ray matches submitted FOV");
  Check(Near(f.viewWindowX,(std::tan(0.7f)-std::tan(-0.9f))*0.5f)&&
        Near(f.viewOffsetX,(std::tan(0.7f)+std::tan(-0.9f))*0.5f),
        "native asymmetric horizontal projection preserves the complete viewport");
  Check(Near(f.viewWindowY,(std::tan(0.8f)-std::tan(-0.6f))*0.5f)&&
        Near(f.viewOffsetY,(std::tan(0.8f)+std::tan(-0.6f))*0.5f),
        "native asymmetric vertical projection preserves the complete viewport");
  Frustum correctedLeft{},correctedRight{};
  Check(MakeTrackedFrustum(-0.9f,0.7f,0.8f,-0.6f,true,correctedLeft)&&
        MakeTrackedFrustum(-0.7f,0.9f,0.8f,-0.6f,true,correctedRight),
        "horizontal tracking correction also produces valid reflected eye frustums");
  Check(correctedLeft.viewOffsetX>0.0f&&correctedRight.viewOffsetX<0.0f&&
        Near(correctedLeft.viewWindowX,correctedRight.viewWindowX),
        "corrected eye positions and asymmetric projection centers share one reflected frame");
  Check(!MakeFrustum(0.1f,0.7f,0.8f,-0.6f,f),"invalid frustum rejected");
  Check(!MakeFrustum(-0.9f,0.7f,std::numeric_limits<float>::quiet_NaN(),-0.6f,f),"NaN rejected");
  Check(!UseCinematicMode(false,false,false,false),"gameplay stays in stereo mode");
  Check(UseCinematicMode(true,false,false,false),"frontend uses cinematic mode");
  Check(UseCinematicMode(false,true,false,false),"full fade uses cinematic mode");
  Check(UseCinematicMode(false,false,true,false),"running cutscene uses cinematic mode");
  Check(UseCinematicMode(false,false,false,true),"cutscene transition uses cinematic mode");
  const auto leftSource=FullEyeSourceRect(0),rightSource=FullEyeSourceRect(1);
  Check(Near(leftSource.u,0)&&Near(leftSource.width,0.5f)&&
        Near(rightSource.u,0.5f)&&Near(rightSource.width,0.5f),
        "complete eye viewports are copied without HUD-edge cropping");
  Check(SubmittedArrayIndex(0,true)==1&&SubmittedArrayIndex(1,true)==0,
        "headset eye correction swaps only the final array mapping");
  Check(SubmittedArrayIndex(0,false)==0&&SubmittedArrayIndex(1,false)==1,
        "canonical OpenXR eye mapping remains available");
  const auto radarTopLeft=VrHudPoint(HudCluster::Radar,1920,1080,0,702);
  const auto radarBottomRight=VrHudPoint(HudCluster::Radar,1920,1080,576,1080);
  Check(Near(radarTopLeft.x,518.4f)&&Near(radarTopLeft.y,43.2f)&&
        Near(radarBottomRight.x,835.2f)&&Near(radarBottomRight.y,251.1f),
        "radar and vital stats occupy the compact upper-left half of the VR safe area");
  const auto vitalTopLeft=VrHudPoint(HudCluster::VitalStats,1920,1080,0,0);
  const auto vitalBottomRight=VrHudPoint(HudCluster::VitalStats,1920,1080,576,540);
  Check(Near(vitalTopLeft.x,518.4f)&&Near(vitalTopLeft.y,43.2f)&&
        Near(vitalBottomRight.x,835.2f)&&Near(vitalBottomRight.y,340.2f),
        "vital-stat panels use a top-origin transform instead of the radar bottom-origin transform");
  const auto statusTopLeft=VrHudPoint(HudCluster::PlayerStatus,1920,1080,1536,0);
  const auto statusBottomRight=VrHudPoint(HudCluster::PlayerStatus,1920,1080,1920,324);
  Check(Near(statusTopLeft.x,1113.6f)&&Near(statusTopLeft.y,43.2f)&&
        Near(statusBottomRight.x,1401.6f)&&Near(statusBottomRight.y,286.2f),
        "player status and wanted level occupy the compact upper-right VR safe area");
  const auto untouched=VrHudPoint(HudCluster::None,1920,1080,960,540);
  Check(Near(untouched.x,960)&&Near(untouched.y,540),
        "unclassified UI such as crosshairs and subtitles is not relocated");
  TwoBoneSolution arm{};
  Check(SolveTwoBone({0,0,0},{0.35f,0.10f,0},{0.60f,0,0},0.40f,0.35f,arm),
        "reachable controller target produces a two-bone solution");
  Check(Near(Length(Subtract(arm.elbow,{0,0,0})),0.40f)&&
        Near(Length(Subtract(arm.wrist,arm.elbow)),0.35f),
        "two-bone solver preserves upper-arm and forearm lengths");
  Check(arm.elbow.y>0.0f,"animated elbow supplies a stable positive bend pole");
  Check(SolveTwoBone({0,0,0},{0,0.2f,0},{5,0,0},0.40f,0.35f,arm)&&arm.clamped,
        "unreachable controller target clamps instead of stretching the skeleton");
  Check(Length(arm.wrist)<0.751f,"clamped wrist remains inside total arm reach");
  Check(!SolveTwoBone({0,0,0},{0,0.2f,0},
        {std::numeric_limits<float>::quiet_NaN(),0,0},0.4f,0.35f,arm),
        "invalid controller target fails closed");
  const Basis nativePalm{{1,0,0},{0,1,0},{0,0,1},{1,2,3}};
  const Basis nativeFinger{{1,0,0},{0,1,0},{0,0,1},{1.08f,2.16f,3.04f}};
  const Basis trackedPalm{{0,1,0},{-1,0,0},{0,0,1},{10,20,30}};
  Basis trackedFinger{};
  Check(RigidlyRebase(nativePalm,trackedPalm,nativeFinger,trackedFinger),
        "finger matrix can be rigidly rebased from the animated palm to the tracked palm");
  Check(Near(trackedFinger.position,{9.84f,20.08f,30.04f}),
        "finger keeps its palm-local offset when the tracked palm moves and rotates");
  Check(Near(trackedFinger.right,trackedPalm.right)&&
        Near(trackedFinger.forward,trackedPalm.forward)&&Near(trackedFinger.up,trackedPalm.up),
        "finger basis follows the tracked palm without shearing or collapsing");
  const Basis mirroredNativePalm{{-1,0,0},{0,1,0},{0,0,1},{1,2,3}};
  const Basis mirroredNativeFinger{{-1,0,0},{0,1,0},{0,0,1},{0.92f,2.16f,3.04f}};
  Basis mirroredTrackedFinger{};
  Check(RigidlyRebase(mirroredNativePalm,trackedPalm,mirroredNativeFinger,
                      mirroredTrackedFinger),
        "mirrored left-hand bone bases remain valid rigid transforms");
  Check(Near(mirroredTrackedFinger.position,{9.84f,20.08f,30.04f})&&
        Near(mirroredTrackedFinger.right,trackedPalm.right),
        "mirrored finger descendants preserve their palm-local offset without cancelling arm IK");
  const Basis referenceController{{1,0,0},{0,1,0},{0,0,1},{0,0,0}};
  const Basis referenceLeftHand{{-1,0,0},{0,1,0},{0,0,1},{4,5,6}};
  const Basis turnedController{{0,1,0},{-1,0,0},{0,0,1},{7,8,9}};
  Basis alignedLeftHand{};
  Check(ApplyControllerOrientation(referenceController,referenceLeftHand,
                                   turnedController,alignedLeftHand),
        "controller rotation can be applied through the GTA hand bind offset");
  Check(Near(alignedLeftHand.right,{0,-1,0})&&
        Near(alignedLeftHand.forward,{-1,0,0})&&
        Near(alignedLeftHand.up,{0,0,1})&&
        Near(alignedLeftHand.position,turnedController.position),
        "left wrist keeps its mirrored anatomical basis while following the controller");
  const Basis nativeAnimatedHand{{-1,0,0},{0,1,0},{0,0,1},{0,0,0}};
  Basis movedHand{};
  Check(MoveHandPreservingOrientation(nativeAnimatedHand,{7,8,9},movedHand),
        "controller target can move a hand without replacing its animated wrist frame");
  Check(Near(movedHand.right,nativeAnimatedHand.right)&&
        Near(movedHand.forward,nativeAnimatedHand.forward)&&
        Near(movedHand.up,nativeAnimatedHand.up)&&Near(movedHand.position,{7,8,9}),
        "wrist facing remains consistent with standard GTA animation while the hand moves");
  const Basis rolledController{{0,0,-1},{0,1,0},{1,0,0},{7,8,9}};
  Basis forwardRolledHand{};
  Check(AlignHandForwardWithControllerRoll(
            nativeAnimatedHand,{0,1,0},{0,1,0},{0,0,1},rolledController.up,
            rolledController.position,forwardRolledHand),
        "a stable forward wrist frame accepts controller roll");
  Check(Near(forwardRolledHand.forward,{0,1,0})&&
        Near(forwardRolledHand.up,{1,0,0})&&
        Near(forwardRolledHand.position,rolledController.position),
        "hand points forward while controller roll rotates the palm around that axis");
  Vec3 reachForward{};
  Check(HandReachForward({0,0,0},{0,1,0},{0,1,0},{0,0,1},reachForward)&&
        Near(reachForward,{0,1,0}),
        "an arm stretched ahead keeps the wrist pointing ahead");
  Check(HandReachForward({0,0,0},{-1,0,0},{0,1,0},{0,0,1},reachForward)&&
        Near(reachForward,{-1,0,0}),
        "an arm stretched sideways can point outward in a T-pose");
  Check(HandReachForward({0,0,0},{0,0,1},{0,1,0},{0,0,1},reachForward)&&
        Near(reachForward,{0,1,0}),
        "raising a hand vertically does not force the wrist to point upward or downward");
  Check(GameplayHandForward({0,0,0},{-0.18f,0.35f,-0.20f},
                            {0,1,0},{1,0,0},{0,0,1},true,reachForward)&&
        Near(reachForward,{0,1,0}),
        "a normally held left hand points gameplay-forward instead of inward");
  Check(GameplayHandForward({0,0,0},{-0.60f,0,0},
                            {0,1,0},{1,0,0},{0,0,1},true,reachForward)&&
        Near(reachForward,{-1,0,0}),
        "a genuinely extended left arm turns outward for a T-pose");
  Check(GameplayHandForward({0,0,0},{0.60f,0,0},
                            {0,1,0},{1,0,0},{0,0,1},false,reachForward)&&
        Near(reachForward,{1,0,0}),
        "a genuinely extended right arm turns outward for a T-pose");
  FilteredTrackingPose previous{{0,0,0},{0,0,0,1}}, filtered{};
  Check(SmoothTrackedPose(previous,{{0.001f,0,0},{0,0,0,1}},0.35f,0.002f,filtered)&&
        Near(filtered.position,{0,0,0}),
        "sub-two-millimetre controller noise is rejected");
  Check(SmoothTrackedPose(previous,{{0.10f,0,0},{0,0,0,1}},0.45f,0.002f,filtered)&&
        Near(filtered.position,{0.045f,0,0}),
        "intentional controller movement is smoothed without locking the arm");
  Vec3 tracked{};
  Check(TrackedWorldPosition(base,{}, {},{0.25f,-0.10f,-0.40f},tracked),
        "finite OpenXR grip position maps into GTA world space");
  Check(Near(tracked,{100.25f,200.40f,299.90f}),
        "controller mapping shares the camera's right/up/forward convention");
  Check(!TrackedWorldPosition(base,{}, {},
        {std::numeric_limits<float>::infinity(),0,0},tracked),
        "non-finite grip positions are rejected");
  GtaPadState vrPad{};
  Check(BuildGtaPadState({1.0f,1.0f},{-0.5f,0.75f},true,true,true,true,
                         1.0f,1.0f,1.0f,vrPad),
        "active OpenXR controls produce a native GTA pad sample");
  Check(vrPad.leftStickX==128&&vrPad.leftStickY==-128&&
        vrPad.rightStickX==-64&&vrPad.rightStickY==-96,
        "both OpenXR sticks retain analog range and GTA axis direction");
  Check(vrPad.buttonSquare==255&&vrPad.buttonTriangle==255&&
        vrPad.buttonCross==255&&vrPad.buttonCircle==255&&
        vrPad.rightShoulder1==255,
        "jump, enter, sprint, fire and aim use GTA native button fields");
  GtaPadState existing{}; existing.leftStickX=30; existing.dPadUp=255;
  MergeGtaPadState(existing,vrPad);
  Check(existing.leftStickX==128&&existing.dPadUp==255&&existing.buttonSquare==255,
        "VR input merges after GInput without discarding another mod's pad state");
  for (unsigned mask=0;mask<16;++mask)
    Check(CanSendInput(mask&1,mask&2,mask&4,mask&8)==(mask==15),"input safety truth table");
  if (failures) return EXIT_FAILURE;
  std::cout << "GTA camera, recenter, IPD, projection and input policy tests passed\n";
}

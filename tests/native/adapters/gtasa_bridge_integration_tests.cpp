// Compiles the production bridge into an isolated executable. No game is
// launched or patched; only this test process owns the emulated game addresses.
#include <windows.h>
#include <vector>
#include <iostream>
static std::vector<INPUT> sentInputs;
static HWND testWindow = nullptr;
static UINT WINAPI TestSendInput(UINT count, LPINPUT inputs, int) {
  sentInputs.insert(sentInputs.end(), inputs, inputs + count);
  return count;
}
static HWND WINAPI TestForegroundWindow() { return testWindow; }
#define SendInput TestSendInput
#define GetForegroundWindow TestForegroundWindow
#define VRCLIENT_GTASA_TESTS
#define DllMain UnusedBridgeDllMain
#include "../../../adapters/gta_sa/vrclient_gtasa_theater.cpp"
#undef DllMain
#undef SendInput
#undef GetForegroundWindow

static int failures = 0, tailCalls = 0, desktopPresents = 0, endedFrames = 0;
static uint32_t lastImage = 0, lastLayers = 0;
static uint32_t lastArrayIndices[2]{};
static XrStructureType lastLayerType = XR_TYPE_UNKNOWN;
static PFN_xrEndFrame realEndFrame;
static PFN_xrAcquireSwapchainImage realAcquire;
static PFN_xrWaitFrame realWait;
static PFN_xrLocateViews realLocate;
static bool shouldRender = true, validPosition = true, failSync = false;
static bool validHandTracking = true, playerInVehicle = false;
static int handLocateCalls = 0;
static XrTime lastHandLocateTime = 0;
static XrAction pressedBooleanAction = XR_NULL_HANDLE;
static XrAction movedVectorAction = XR_NULL_HANDLE;
static XrVector2f movedVectorState{};
static XrQuaternionf headViewOrientation{0,0,0,1};
static XrAction movedFloatAction = XR_NULL_HANDLE;
static float movedFloatState = 0.0f;
static XrVector2f lastWindow{};
static GtaCameraMatrix eyeMatrices[2]{};
static gtasa_vr::GtaPadState playerPadFixture{};
static XrVector2f eyeViewWindows[2]{},eyeViewOffsets[2]{};
static float lastHudVertices[8]{};
static float lastFontPosition[2]{};
static void Check(bool ok, const char* label) {
  if (!ok) { std::cerr << "FAIL: " << label << '\n'; ++failures; }
}
static bool Near(float a,float b) { return std::abs(a-b) < 0.0001f; }
static XrResult XRAPI_CALL EndFrameSpy(XrSession session, const XrFrameEndInfo* info) {
  ++endedFrames; lastLayers=info->layerCount;
  lastLayerType=lastLayers ? info->layers[0]->type : XR_TYPE_UNKNOWN;
  if (lastLayerType==XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
    const auto* projection=reinterpret_cast<const XrCompositionLayerProjection*>(info->layers[0]);
    if (projection->viewCount==2) {
      lastArrayIndices[0]=projection->views[0].subImage.imageArrayIndex;
      lastArrayIndices[1]=projection->views[1].subImage.imageArrayIndex;
    }
  }
  return realEndFrame(session,info);
}
static XrResult XRAPI_CALL AcquireSpy(XrSwapchain swapchain,
    const XrSwapchainImageAcquireInfo* info,uint32_t* index) {
  auto result=realAcquire(swapchain,info,index); lastImage=*index; return result;
}
static XrResult XRAPI_CALL WaitSpy(XrSession session,const XrFrameWaitInfo* info,XrFrameState* state) {
  auto result=realWait(session,info,state);
  state->shouldRender=shouldRender ? XR_TRUE : XR_FALSE;
  return result;
}
static XrResult XRAPI_CALL LocateSpy(XrSession session,const XrViewLocateInfo* info,
    XrViewState* state,uint32_t capacity,uint32_t* count,XrView* views) {
  auto result=realLocate(session,info,state,capacity,count,views);
  if (!validPosition) state->viewStateFlags &= ~XR_VIEW_STATE_POSITION_VALID_BIT;
  views[0].pose.orientation=headViewOrientation;
  views[1].pose.orientation=headViewOrientation;
  views[0].fov={-0.9f,0.7f,0.8f,-0.6f};
  views[1].fov={-0.7f,0.9f,0.8f,-0.6f};
  return result;
}
static XrResult XRAPI_CALL SyncStub(XrSession,const XrActionsSyncInfo*) {
  return failSync ? XR_SESSION_NOT_FOCUSED : XR_SUCCESS;
}
static XrResult XRAPI_CALL BooleanActionStub(
    XrSession,const XrActionStateGetInfo* info,XrActionStateBoolean* state) {
  state->isActive=XR_TRUE;
  state->currentState=info->action==pressedBooleanAction ? XR_TRUE : XR_FALSE;
  state->changedSinceLastSync=XR_TRUE;
  return XR_SUCCESS;
}
static XrResult XRAPI_CALL PoseActionStub(
    XrSession,const XrActionStateGetInfo*,XrActionStatePose* state) {
  state->isActive=XR_TRUE;
  return XR_SUCCESS;
}
static XrResult XRAPI_CALL VectorActionStub(
    XrSession,const XrActionStateGetInfo* info,XrActionStateVector2f* state) {
  state->isActive=XR_TRUE;
  state->currentState=info->action==movedVectorAction ? movedVectorState : XrVector2f{};
  return XR_SUCCESS;
}
static XrResult XRAPI_CALL FloatActionStub(
    XrSession,const XrActionStateGetInfo* info,XrActionStateFloat* state) {
  state->isActive=XR_TRUE;
  state->currentState=info->action==movedFloatAction ? movedFloatState : 0.0f;
  return XR_SUCCESS;
}
static XrResult XRAPI_CALL LocateHandStub(
    XrSpace space,XrSpace,XrTime time,XrSpaceLocation* location) {
  ++handLocateCalls; lastHandLocateTime=time;
  location->locationFlags=validHandTracking
      ? XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT : 0;
  location->pose.orientation.w=1.0f;
  location->pose.position=space==g_theater.leftHandSpace_
      ? XrVector3f{-0.25f,-0.20f,-0.35f} : XrVector3f{0.25f,-0.20f,-0.35f};
  return XR_SUCCESS;
}
static void* __cdecl SetWindowStub(void* camera,const XrVector2f* value) {
  lastWindow=*value; std::memcpy(static_cast<uint8_t*>(camera)+0x68,value,sizeof(*value)); return camera;
}
static void* __cdecl SetOffsetStub(void* camera,const XrVector2f* value) {
  std::memcpy(static_cast<uint8_t*>(camera)+0x78,value,sizeof(*value)); return camera;
}
static void __fastcall CopyCameraStub(void*,void*,bool) {}
alignas(4) static uint8_t playerPedFixture[0x79C]{};
static int setHeadingCalls=0;
static float lastSetHeading=0.0f;
static void __fastcall SetHeadingStub(void* ped,void*,float heading) {
  Check(ped==playerPedFixture,"body turn targets only the local player ped");
  ++setHeadingCalls;
  lastSetHeading=heading;
}
static uint8_t playerClumpFixture = 0;
static RwMatrixNative playerBoneMatrices[10]{};
struct FakeHierarchy { int flags; int nodeCount; RwMatrixNative* matrices; };
static FakeHierarchy playerHierarchy{0,10,playerBoneMatrices};
static unsigned int requestedBone = 0;
static bool requestedBoneUpdate = false;
static void* __cdecl FindPlayerPedStub(int player) {
  return player == 0 ? playerPedFixture : nullptr;
}
static void* __cdecl FindPlayerVehicleStub(int player,bool) {
  return player==0 && playerInVehicle ? playerPedFixture : nullptr;
}
static void* __cdecl GetHierarchyStub(void* clump) {
  return clump==&playerClumpFixture ? &playerHierarchy : nullptr;
}
static int __cdecl BoneIndexStub(void*,int bone) {
  switch (bone) {
    case 32:return 0; case 33:return 1; case 34:return 2;
    case 35:return 3; case 36:return 4;
    case 22:return 5; case 23:return 6; case 24:return 7;
    case 25:return 8; case 26:return 9;
    default:return -1;
  }
}
static RwMatrixNative* __cdecl MatrixArrayStub(void*) { return playerBoneMatrices; }
static void __fastcall GetBonePositionStub(
    void*, void*, Vec3& position, unsigned int bone, bool updateSkinBones) {
  position = {11,22,33};
  requestedBone = bone;
  requestedBoneUpdate = updateSkinBones;
}
static HRESULT STDMETHODCALLTYPE PresentStub(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*) {
  ++desktopPresents; return D3D_OK;
}
static HRESULT STDMETHODCALLTYPE DrawPrimitiveUpStub(
    IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,const void* vertices,UINT stride) {
  for (unsigned index=0;index<4;++index) {
    const auto* vertex=static_cast<const uint8_t*>(vertices)+index*stride;
    std::memcpy(&lastHudVertices[index*2],vertex,sizeof(float)*2);
  }
  return D3D_OK;
}
static void __cdecl FontPrintStringStub(float x,float y,const void*) {
  lastFontPosition[0]=x; lastFontPosition[1]=y;
}
static void __cdecl DrawTail() {
  const unsigned eye=g_theater.currentEye_;
  ++tailCalls;
  eyeMatrices[eye]=*reinterpret_cast<GtaCameraMatrix*>(g_config.cameraAddress+0x974);
  auto* rwCamera=*reinterpret_cast<uint8_t**>(g_config.cameraAddress+g_config.rwCameraOffset);
  std::memcpy(&eyeViewWindows[eye],rwCamera+0x68,sizeof(XrVector2f));
  std::memcpy(&eyeViewOffsets[eye],rwCamera+0x78,sizeof(XrVector2f));
  const D3DCOLOR color=eye == 0 ? D3DCOLOR_XRGB(255,0,0) : D3DCOLOR_XRGB(0,255,0);
  Check(SUCCEEDED(g_d3d9Device->Clear(0,nullptr,D3DCLEAR_TARGET,color,1,0)),"D3D9 clear eye");
  HookedPresent(g_d3d9Device,nullptr,nullptr,nullptr,nullptr);
}
static void WriteJump(uintptr_t address,void* destination) {
  auto* code=reinterpret_cast<uint8_t*>(address);
  code[0]=0xE9;
  const uint32_t displacement=reinterpret_cast<uintptr_t>(destination)-(address+5);
  std::memcpy(code+1,&displacement,4);
  FlushInstructionCache(GetCurrentProcess(),code,5);
}
static bool CheckEyePixel(unsigned eye,unsigned channel) {
  D3D11_TEXTURE2D_DESC desc{};
  auto* image=g_theater.images_[lastImage].texture;
  image->GetDesc(&desc);
  desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0;
  desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ; desc.MiscFlags=0;
  ID3D11Texture2D* staging=nullptr;
  if (FAILED(g_theater.d3d11Device_->CreateTexture2D(&desc,nullptr,&staging))) return false;
  g_theater.d3d11Context_->CopyResource(staging,image);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  bool correct=false;
  if (SUCCEEDED(g_theater.d3d11Context_->Map(staging,eye,D3D11_MAP_READ,0,&mapped))) {
    auto* pixel=static_cast<uint8_t*>(mapped.pData)+(desc.Height/2)*mapped.RowPitch+(desc.Width/2)*4;
    correct=pixel[channel]>240 && pixel[1-channel]<10;
    g_theater.d3d11Context_->Unmap(staging,eye);
  }
  staging->Release(); return correct;
}
int main(int argc,char** argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  AddVectoredExceptionHandler(1, [](EXCEPTION_POINTERS* exception) -> LONG {
    if (exception->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
      return EXCEPTION_CONTINUE_SEARCH;
    HMODULE module=nullptr; char path[MAX_PATH]{};
    auto* address=exception->ExceptionRecord->ExceptionAddress;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<LPCSTR>(address), &module);
    GetModuleFileNameA(module,path,MAX_PATH);
    std::fprintf(stderr,"AV at %p module=%s offset=%Ix target=%Ix\n",address,path,
        reinterpret_cast<uintptr_t>(address)-reinterpret_cast<uintptr_t>(module),
        exception->ExceptionRecord->ExceptionInformation[1]);
    void* frames[32]{};
    auto count=CaptureStackBackTrace(0,32,frames,nullptr);
    for (USHORT i=0;i<count;++i) std::fprintf(stderr,"frame[%u]=%p\n",i,frames[i]);
    return EXCEPTION_CONTINUE_SEARCH;
  });
  if (argc != 3) { std::cerr << "usage: test <x86-loader.dll> <fake-runtime.json>\n"; return 2; }
  SetEnvironmentVariableA("VRCLIENT_GTASA_SNAP_TURN_DEGREES","45");
  ConfigureTurnMode();
  Check(Near(g_snapTurnDegrees,45.0f),
        "bridge reads the selected snap angle from the launch environment");
  SetEnvironmentVariableA("VRCLIENT_GTASA_SNAP_TURN_DEGREES","200");
  ConfigureTurnMode();
  Check(Near(g_snapTurnDegrees,30.0f),
        "invalid snap angle falls back to the original thirty-degree default");
  SetEnvironmentVariableA("VRCLIENT_GTASA_SNAP_TURN_DEGREES",nullptr);
  ConfigureTurnMode();
  // Explicit addresses are a test fixture, not the installed game image.
  std::vector<void*> fixtureRegions;
  for (uintptr_t originalBase : {0x500000u,0x510000u,0x7E0000u,0x8D0000u,
                         0xB50000u,0xB60000u,0xB80000u,0xBA0000u,0xC10000u,0xC30000u}) {
    const uintptr_t base=NativeAddress(originalBase);
    auto* region=VirtualAlloc(reinterpret_cast<void*>(base),0x10000,
                              MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
    if (region != reinterpret_cast<void*>(base)) {
      std::cerr << "fixture allocation failed at " << std::hex << base << '\n'; return 3;
    }
    fixtureRegions.push_back(region);
  }
  auto* derive=reinterpret_cast<uint8_t*>(NativeAddress(0x5150E0));
  derive[0]=0xC2; derive[1]=8; derive[2]=0; // thiscall: pop two bool arguments
  auto* fade=reinterpret_cast<uint8_t*>(NativeAddress(0x50AE20));
  fade[0]=0x33; fade[1]=0xC0; fade[2]=0xC3;
  WriteJump(NativeAddress(0x7EE410),reinterpret_cast<void*>(&SetWindowStub));
  WriteJump(NativeAddress(0x7EE1A0),reinterpret_cast<void*>(&SetOffsetStub));
  WNDCLASSA cls{}; cls.lpfnWndProc=DefWindowProcA;
  cls.hInstance=GetModuleHandleA(nullptr); cls.lpszClassName="GtaVrTest";
  RegisterClassA(&cls);
  testWindow=CreateWindowA(cls.lpszClassName,"GTA bridge test",WS_OVERLAPPEDWINDOW,
                          0,0,640,480,nullptr,nullptr,cls.hInstance,nullptr);
  g_realDirect3DCreate9=&Direct3DCreate9;
  auto* d3d=HookedDirect3DCreate9(D3D_SDK_VERSION);
  D3DPRESENT_PARAMETERS pp{};
  pp.Windowed=TRUE; pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
  pp.hDeviceWindow=testWindow; pp.BackBufferWidth=640; pp.BackBufferHeight=480;
  pp.BackBufferFormat=D3DFMT_X8R8G8B8; pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
  if (!d3d || FAILED(d3d->CreateDevice(0,D3DDEVTYPE_HAL,testWindow,
      D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&g_d3d9Device))) return 4;
  Check(g_vrHudDrawHooksInstalled,"D3D9 immediate drawing is intercepted for targeted VR HUD layout");
  struct HudVertex { float x,y,z,rhw; D3DCOLOR color; float u,v; };
  HudVertex hudVertices[4]={{0,312,0,1,0,0,0},{192,312,0,1,0,0,0},
                            {0,480,0,1,0,0,0},{192,480,0,1,0,0,0}};
  g_d3d9Device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1);
  const auto savedDrawPrimitiveUp=g_realDrawPrimitiveUp;
  g_realDrawPrimitiveUp=&DrawPrimitiveUpStub;
  g_vrHudCluster=gtasa_vr::HudCluster::Radar;
  Check(SUCCEEDED(HookedDrawPrimitiveUp(g_d3d9Device,D3DPT_TRIANGLESTRIP,2,
                                        hudVertices,sizeof(HudVertex))),
        "targeted radar draw reaches the D3D9 delegate");
  Check(Near(lastHudVertices[0],172.8f)&&Near(lastHudVertices[1],19.2f)&&
        Near(lastHudVertices[6],278.4f)&&Near(lastHudVertices[7],111.6f),
        "production D3D9 seam moves and scales radar into the upper-center safe area");
  g_realDrawPrimitiveUp=savedDrawPrimitiveUp;
  g_vrHudCluster=gtasa_vr::HudCluster::PlayerStatus;
  g_fontPrintStringTrampoline=reinterpret_cast<void*>(&FontPrintStringStub);
  HookedFontPrintString(512,0,nullptr);
  Check(Near(lastFontPosition[0],371.2f)&&Near(lastFontPosition[1],19.2f),
        "buffered HUD text anchor moves with the player-status cluster");
  g_fontPrintStringTrampoline=nullptr;
  g_vrHudCluster=gtasa_vr::HudCluster::None;
  const uintptr_t hudHooks[]={NativeAddress(0x500100),NativeAddress(0x500120),
      NativeAddress(0x500140),NativeAddress(0x500160),NativeAddress(0x500180)};
  const std::vector<uint8_t> hudHookExpected{0x83,0xEC,0x04,0x83,0xC4,0x04};
  for (const auto address:hudHooks) {
    std::memcpy(reinterpret_cast<void*>(address),hudHookExpected.data(),hudHookExpected.size());
    *reinterpret_cast<uint8_t*>(address+hudHookExpected.size())=0xC3;
  }
  g_config.hudPlayerInfo=hudHooks[0]; g_config.hudPlayerInfoExpected=hudHookExpected;
  g_config.hudWanted=hudHooks[1]; g_config.hudWantedExpected=hudHookExpected;
  g_config.hudRadar=hudHooks[2]; g_config.hudRadarExpected=hudHookExpected;
  g_config.hudVitalStats=hudHooks[3]; g_config.hudVitalStatsExpected=hudHookExpected;
  g_config.fontPrintString=hudHooks[4]; g_config.fontPrintStringExpected=hudHookExpected;
  Check(InstallVrHudHooks(),"all targeted VR HUD function hooks install atomically");
  reinterpret_cast<VoidFn>(g_config.hudPlayerInfo)();
  reinterpret_cast<VoidFn>(g_config.hudWanted)();
  reinterpret_cast<VoidFn>(g_config.hudRadar)();
  reinterpret_cast<VoidFn>(g_config.hudVitalStats)();
  reinterpret_cast<void(__cdecl*)(float,float,const void*)>(g_config.fontPrintString)(1,2,nullptr);
  Check(g_hudPlayerInfoTrampoline&&g_hudWantedTrampoline&&g_hudRadarTrampoline&&
        g_hudVitalStatsTrampoline&&g_fontPrintStringTrampoline,
        "targeted HUD wrappers preserve callable original trampolines");
  const uintptr_t padTarget=NativeAddress(0x500200);
  *reinterpret_cast<uint8_t*>(padTarget)=0xC3;
  auto fixturePadCall=[&](uintptr_t address) {
    std::vector<uint8_t> bytes{0xE8,0,0,0,0};
    const int32_t delta=static_cast<int32_t>(padTarget-(address+5));
    std::memcpy(bytes.data()+1,&delta,sizeof(delta));
    std::memcpy(reinterpret_cast<void*>(address),bytes.data(),bytes.size());
    *reinterpret_cast<uint8_t*>(address+5)=0xC3;
    return bytes;
  };
  g_config.padUpdateCall=NativeAddress(0x500220);
  g_config.padUpdateCallExpected=fixturePadCall(g_config.padUpdateCall);
  g_config.frontendPadUpdateCall=NativeAddress(0x500240);
  g_config.frontendPadUpdateCallExpected=fixturePadCall(g_config.frontendPadUpdateCall);
  Check(InstallPadUpdateHook() && g_realPadUpdate==reinterpret_cast<VoidFn>(padTarget),
        "gameplay and frontend loops both hook the same native pad updater");
  reinterpret_cast<VoidFn>(g_config.padUpdateCall)();
  reinterpret_cast<VoidFn>(g_config.frontendPadUpdateCall)();
  SetEnvironmentVariableA("VRCLIENT_GTASA_OPENXR_LOADER",argv[1]);
  SetEnvironmentVariableA("XR_RUNTIME_JSON",argv[2]);
  // The fake manifest names its DLL without a path; scope its search directory
  // to this isolated test process (including UNC workspaces).
  std::string runtimeDirectory=argv[2];
  runtimeDirectory.resize(runtimeDirectory.find_last_of(std::string("/") + char(92)));
  SetDllDirectoryA(runtimeDirectory.c_str());
  g_stereoRequested=true; g_inputRequested=true; g_stereoHooksInstalled=true;
  g_bridgeEnabled.store(true);
  g_config.cameraAddress=NativeAddress(0xB6F028); g_config.cameraMatrixOffset=0x974;
  g_config.rwCameraOffset=0x954;
  g_config.fovAddress=NativeAddress(0x8D5038); g_config.aspectRatioAddress=NativeAddress(0xC3EFA4);
  g_config.setRwViewWindow=NativeAddress(0x7EE410);
  g_config.setRwViewOffset=NativeAddress(0x7EE1A0);
  g_config.screenDimensions=NativeAddress(0xC17044);
  g_config.deriveCamera=NativeAddress(0x5150E0);
  g_config.frontEndMenuActive=NativeAddress(0xBA67A4);
  g_config.fadeStatus=NativeAddress(0x50AE20);
  g_config.cutsceneRunning=NativeAddress(0xB5F851);
  g_config.cutsceneProcessing=NativeAddress(0xB5F852);
  g_config.findPlayerPed=reinterpret_cast<uintptr_t>(&FindPlayerPedStub);
  g_config.playerPad=reinterpret_cast<uintptr_t>(&playerPadFixture);
  g_config.setPlayerHeading=reinterpret_cast<uintptr_t>(&SetHeadingStub);
  g_config.findPlayerVehicle=reinterpret_cast<uintptr_t>(&FindPlayerVehicleStub);
  g_config.getBonePosition=reinterpret_cast<uintptr_t>(&GetBonePositionStub);
  g_config.getAnimHierarchyFromClump=reinterpret_cast<uintptr_t>(&GetHierarchyStub);
  g_config.rpHAnimIdGetIndex=reinterpret_cast<uintptr_t>(&BoneIndexStub);
  g_config.rpHAnimGetMatrixArray=reinterpret_cast<uintptr_t>(&MatrixArrayStub);
  *reinterpret_cast<void**>(playerPedFixture+kEntityRwObjectOffset)=&playerClumpFixture;
  // Deliberately start the body ninety degrees away from the GTA camera.
  // The previous regression replaced the camera basis with the ped basis.
  *reinterpret_cast<float*>(playerPedFixture+0x558)=1.570796327f;
  *reinterpret_cast<float*>(playerPedFixture+0x55C)=1.570796327f;
  g_firstPersonRequested=true;
  auto* camera=reinterpret_cast<GtaCameraMatrix*>(NativeAddress(0xB6F028)+0x974);
  camera->right={1,0,0}; camera->forward={0,1,0}; camera->up={0,0,1};
  camera->position={10,20,30}; const auto base=*camera;
  *reinterpret_cast<float*>(NativeAddress(0x8D5038))=70;
  *reinterpret_cast<float*>(NativeAddress(0xC3EFA4))=4.0f/3.0f;
  *reinterpret_cast<void**>(NativeAddress(0xB6F028)+0x954)=reinterpret_cast<void*>(NativeAddress(0xB80000));
  g_copyCameraMatrixToRwCam=reinterpret_cast<CopyCameraMatrixFn>(&CopyCameraStub);
  g_realPresent=&PresentStub;
  // The startup movies own D3D Present before GTA enters its RenderWare tail.
  // Opening an OpenXR session from that path races game/input initialization.
  HookedPresent(g_d3d9Device,nullptr,nullptr,nullptr,nullptr);
  Check(!g_theater.initialized_,"startup Present does not initialize OpenXR before GTA render tail");
  Check(desktopPresents==1,"startup Present remains an ordinary desktop swap");
  *reinterpret_cast<uint8_t*>(g_config.frontEndMenuActive)=1;
  HookedPresent(g_d3d9Device,nullptr,nullptr,nullptr,nullptr);
  Check(g_theater.initialized_,"initial frontend menu initializes OpenXR before gameplay tail");
  Check(desktopPresents==2,"initial frontend menu still presents the desktop frame once");
  *reinterpret_cast<uint8_t*>(g_config.frontEndMenuActive)=0;
  desktopPresents=0;
  g_renderTailObserved.store(true);
  if (!g_theater.EnsureInitialized(g_d3d9Device)) return 5;
  realEndFrame=g_theater.endFrame_; g_theater.endFrame_=&EndFrameSpy;
  realAcquire=g_theater.acquireSwapchainImage_; g_theater.acquireSwapchainImage_=&AcquireSpy;
  realWait=g_theater.waitFrame_; g_theater.waitFrame_=&WaitSpy;
  realLocate=g_theater.locateViews_; g_theater.locateViews_=&LocateSpy;
  Check(g_theater.RenderCompleteFrame(g_d3d9Device,&DrawTail),"full stereo frame completed");
  Check(tailCalls==2 && desktopPresents==0,"two complete eye draws without desktop swaps");
  Check(endedFrames==1 && lastLayers==1 && lastLayerType==XR_TYPE_COMPOSITION_LAYER_PROJECTION,
        "one balanced OpenXR stereo frame");
  Check(CheckEyePixel(0,0) && CheckEyePixel(1,1),"different D3D9 eyes reach correct D3D11 array slices");
  Check(lastArrayIndices[0]==0 && lastArrayIndices[1]==1,
        "OpenXR projection views use their canonical matching texture-array slices");
  Check(eyeViewOffsets[0].x>0.0f && eyeViewOffsets[1].x<0.0f &&
        eyeViewWindows[0].x>0.0f && eyeViewWindows[1].x>0.0f,
        "RenderWare receives horizontally corrected asymmetric projection without HUD cropping");
  Check(std::memcmp(camera,&base,sizeof(base))==0,"game camera restored byte-for-byte");
  Check(Near(eyeMatrices[0].position.x-eyeMatrices[1].position.x,0.064f),
        "production camera applies IPD in the same reflected frame as corrected head yaw");
  Check(requestedBone==kPlayerHeadBone && requestedBoneUpdate,
        "production camera requests the updated player head bone");
  const Vec3 eyeMidpoint{
      (eyeMatrices[0].position.x+eyeMatrices[1].position.x)*0.5f,
      (eyeMatrices[0].position.y+eyeMatrices[1].position.y)*0.5f,
      (eyeMatrices[0].position.z+eyeMatrices[1].position.z)*0.5f};
  Check(Near(eyeMidpoint.x,11)&&Near(eyeMidpoint.y,22.12f)&&Near(eyeMidpoint.z,33),
        "stereo eye midpoint is attached to the player head instead of the chase camera");
  Check(Near(*reinterpret_cast<float*>(NativeAddress(0x8D5038)),70),"game FOV restored");
  Check(Near(*reinterpret_cast<float*>(NativeAddress(0xC3EFA4)),4.0f/3.0f),"game aspect restored");
  *reinterpret_cast<uint8_t*>(g_config.frontEndMenuActive)=1;
  Check(!g_theater.RenderCompleteFrame(g_d3d9Device,&DrawTail),"menu bypasses stereo replay");
  Check(tailCalls==3 && desktopPresents==1,"menu draws and presents exactly once");
  Check(lastLayerType==XR_TYPE_COMPOSITION_LAYER_QUAD && lastLayers==1,"menu visible in both eyes");
  *reinterpret_cast<uint8_t*>(g_config.frontEndMenuActive)=0;
  *reinterpret_cast<uint8_t*>(g_config.cutsceneRunning)=1;
  const int beforeCutsceneTail=tailCalls, beforeCutscenePresent=desktopPresents;
  Check(!g_theater.RenderCompleteFrame(g_d3d9Device,&DrawTail),
        "running cutscene bypasses first-person stereo replay");
  Check(tailCalls==beforeCutsceneTail+1 && desktopPresents==beforeCutscenePresent+1,
        "cutscene renders its intact frame exactly once");
  Check(lastLayerType==XR_TYPE_COMPOSITION_LAYER_QUAD && lastLayers==1,
        "cutscene is presented as a head-locked cinematic quad");
  *reinterpret_cast<uint8_t*>(g_config.cutsceneRunning)=0;
  *reinterpret_cast<uint8_t*>(g_config.cutsceneProcessing)=1;
  Check(!g_theater.RenderCompleteFrame(g_d3d9Device,&DrawTail),
        "cutscene transition processing remains in cinematic mode");
  Check(lastLayerType==XR_TYPE_COMPOSITION_LAYER_QUAD && lastLayers==1,
        "cutscene transition keeps the compositor background black");
  *reinterpret_cast<uint8_t*>(g_config.cutsceneProcessing)=0;
  shouldRender=false;
  const int beforeNoRender=endedFrames;
  g_theater.RenderCompleteFrame(g_d3d9Device,&DrawTail);
  Check(endedFrames==beforeNoRender+1 && lastLayers==0,"shouldRender false ends exactly one empty frame");
  shouldRender=true; validPosition=false;
  const int beforeInvalid=endedFrames;
  g_theater.RenderCompleteFrame(g_d3d9Device,&DrawTail);
  Check(endedFrames==beforeInvalid+1 && lastLayers==0,"invalid positional tracking never renders stereo");
  validPosition=true;
  g_theater.inputEnabled_=true; g_theater.inputAttached_=true;
  g_theater.sessionState_=XR_SESSION_STATE_FOCUSED; g_theater.syncActions_=&SyncStub;
  g_theater.getActionStatePose_=&PoseActionStub;
  g_theater.locateSpace_=&LocateHandStub;
  g_theater.frameState_.predictedDisplayTime=123456;
  handLocateCalls=0; validHandTracking=true;
  g_theater.PollInput();
  Check(handLocateCalls==2 && lastHandLocateTime==123456,
        "both grip spaces are sampled once at predicted display time");
  Check(g_theater.trackedHands_[0].valid&&g_theater.trackedHands_[1].valid,
        "valid OpenXR grip locations produce a paired hand snapshot input");
  g_theater.BuildArmFrameSnapshot();
  Check(g_theater.armFrame_.valid&&g_theater.armFrame_.displayTime==123456,
        "controller poses become one immutable arm snapshot for the stereo frame");
  validHandTracking=false;
  g_theater.PollInput();
  g_theater.BuildArmFrameSnapshot();
  Check(!g_theater.armFrame_.valid,"tracking loss clears arm IK instead of freezing hands");

  RwMatrixNative layoutProbe{};
  const gtasa_vr::Basis gameBasis{{1,0,0},{0,1,0},{0,0,1},{}};
  SetRwBasis(layoutProbe,gameBasis);
  Check(Near(layoutProbe.up.x,gameBasis.forward.x)&&
        Near(layoutProbe.up.y,gameBasis.forward.y)&&
        Near(layoutProbe.up.z,gameBasis.forward.z)&&
        Near(layoutProbe.at.x,gameBasis.up.x)&&
        Near(layoutProbe.at.y,gameBasis.up.y)&&Near(layoutProbe.at.z,gameBasis.up.z),
        "RenderWare up/at storage maps to GTA forward/up without swapping axes");

  auto setBone=[&](unsigned index,Vec3 position) {
    playerBoneMatrices[index].right={1,0,0};
    playerBoneMatrices[index].up={0,1,0};
    playerBoneMatrices[index].at={0,0,1};
    playerBoneMatrices[index].position=position;
  };
  setBone(0,{-0.15f,0,1.40f}); setBone(1,{-0.35f,0,1.35f});
  setBone(2,{-0.55f,0,1.30f}); setBone(3,{-0.60f,0,1.28f});
  setBone(4,{-0.65f,0,1.26f}); setBone(5,{0.15f,0,1.40f});
  setBone(6,{0.35f,0,1.35f}); setBone(7,{0.55f,0,1.30f});
  setBone(8,{0.60f,0,1.28f}); setBone(9,{0.65f,0,1.26f});
  RwMatrixNative nativeBones[10]{};
  std::memcpy(nativeBones,playerBoneMatrices,sizeof(nativeBones));
  g_theater.armFrame_.valid=true;
  g_theater.armFrame_.leftHand={{0,0,-1},{0,1,0},{1,0,0},{-0.50f,0.20f,1.35f}};
  g_theater.armFrame_.rightHand={{1,0,0},{0,1,0},{0,0,1},{0.50f,0.20f,1.35f}};
  g_theater.stereoFrameActive_=true; playerInVehicle=false;
  g_theater.AfterPedPreRender(playerPedFixture);
  Check(g_theater.armOverrideActive_&&
        std::memcmp(nativeBones,playerBoneMatrices,sizeof(nativeBones))!=0,
        "local player arm matrices receive the controller-driven IK result");
  Check(Near(playerBoneMatrices[2].position.x,g_theater.armFrame_.leftHand.position.x)&&
        Near(playerBoneMatrices[2].position.y,g_theater.armFrame_.leftHand.position.y)&&
        Near(playerBoneMatrices[2].position.z,g_theater.armFrame_.leftHand.position.z)&&
        Near(playerBoneMatrices[7].position.x,g_theater.armFrame_.rightHand.position.x)&&
        Near(playerBoneMatrices[7].position.y,g_theater.armFrame_.rightHand.position.y)&&
        Near(playerBoneMatrices[7].position.z,g_theater.armFrame_.rightHand.position.z),
        "character palms coincide with reachable OpenXR grip targets");
  const auto leftFingerOffset=gtasa_vr::Subtract(
      playerBoneMatrices[3].position,playerBoneMatrices[2].position);
  const auto rightFingerOffset=gtasa_vr::Subtract(
      playerBoneMatrices[8].position,playerBoneMatrices[7].position);
  Vec3 leftFingerDirection{},rightFingerDirection{},leftPalmRoll{},expectedLeftPalmUp{};
  Vec3 expectedLeftForward{},expectedRightForward{};
  gtasa_vr::Normalize(leftFingerOffset,leftFingerDirection);
  gtasa_vr::Normalize(rightFingerOffset,rightFingerDirection);
  gtasa_vr::GameplayHandForward(playerBoneMatrices[0].position,playerBoneMatrices[2].position,
      g_theater.vrCameraBasis_.forward,g_theater.vrCameraBasis_.right,
      g_theater.vrCameraBasis_.up,true,expectedLeftForward);
  gtasa_vr::GameplayHandForward(playerBoneMatrices[5].position,playerBoneMatrices[7].position,
      g_theater.vrCameraBasis_.forward,g_theater.vrCameraBasis_.right,
      g_theater.vrCameraBasis_.up,false,expectedRightForward);
  gtasa_vr::Normalize(gtasa_vr::Subtract(
      playerBoneMatrices[2].at,gtasa_vr::Scale(expectedLeftForward,
      gtasa_vr::Dot(playerBoneMatrices[2].at,expectedLeftForward))),leftPalmRoll);
  gtasa_vr::Normalize(gtasa_vr::Subtract(
      g_theater.armFrame_.leftHand.up,gtasa_vr::Scale(expectedLeftForward,
      gtasa_vr::Dot(g_theater.armFrame_.leftHand.up,expectedLeftForward))),expectedLeftPalmUp);
  Check(Near(gtasa_vr::Length(leftFingerOffset),gtasa_vr::Length(
             gtasa_vr::Subtract(nativeBones[3].position,nativeBones[2].position)))&&
        Near(gtasa_vr::Length(gtasa_vr::Subtract(
             playerBoneMatrices[4].position,playerBoneMatrices[2].position)),
             gtasa_vr::Length(gtasa_vr::Subtract(
             nativeBones[4].position,nativeBones[2].position)))&&
        Near(gtasa_vr::Length(rightFingerOffset),gtasa_vr::Length(
             gtasa_vr::Subtract(nativeBones[8].position,nativeBones[7].position)))&&
        Near(gtasa_vr::Length(gtasa_vr::Subtract(
             playerBoneMatrices[9].position,playerBoneMatrices[7].position)),
              gtasa_vr::Length(gtasa_vr::Subtract(
             nativeBones[9].position,nativeBones[7].position)))&&
        gtasa_vr::Dot(leftFingerDirection,expectedLeftForward)>0.99f&&
        gtasa_vr::Dot(rightFingerDirection,expectedRightForward)>0.99f&&
        gtasa_vr::Dot(leftPalmRoll,expectedLeftPalmUp)>0.99f,
        "hands point along each arm reach while controller roll rotates the palm around that axis");
  g_theater.RestoreArmOverride();
  Check(std::memcmp(nativeBones,playerBoneMatrices,sizeof(nativeBones))==0,
        "native arm matrices are restored byte-for-byte after an eye render");
  playerInVehicle=true;
  g_theater.AfterPedPreRender(playerPedFixture);
  Check(!g_theater.armOverrideActive_,"vehicle state keeps GTA native arm animation");
  playerInVehicle=false; g_theater.stereoFrameActive_=false;

  failSync=true; g_theater.keyW_=true; g_theater.mouseFire_=true;
  g_theater.PollInput();
  Check(!g_theater.keyW_ && !g_theater.mouseFire_ && sentInputs.size()==2,
        "XR_SESSION_NOT_FOCUSED releases held keyboard and mouse input");
  Check((sentInputs[0].ki.dwFlags&KEYEVENTF_KEYUP)!=0 &&
        (sentInputs[1].mi.dwFlags&MOUSEEVENTF_LEFTUP)!=0,"actual key-up and mouse-up events generated");
  sentInputs.clear(); failSync=false;
  g_theater.getActionStateVector2f_=&VectorActionStub;
  g_theater.getActionStateBoolean_=&BooleanActionStub;
  g_theater.getActionStateFloat_=&FloatActionStub;
  g_theater.PollInput(); // Centered stick rearms after the simulated XR focus loss.
  movedVectorAction=g_theater.leftMoveAction_; movedVectorState={0.0f,1.0f};
  headViewOrientation={0,0.70710678f,0,0.70710678f};
  Check(g_theater.RenderCompleteFrame(g_d3d9Device,&DrawTail),
        "head-turned gameplay renders a complete stereo frame");
  gtasa_vr::GtaPadState headTurnPad{};
  g_theater.ApplyNativePadInput(headTurnPad);
  Check(eyeMatrices[0].forward.x>0.99f&&headTurnPad.leftStickX==128&&
        headTurnPad.leftStickY==0,
        "native left-stick forward follows the same physical head yaw as the rendered eyes");
  headViewOrientation={0,0,0,1};
  movedVectorAction=XR_NULL_HANDLE; movedVectorState={};
  movedVectorAction=g_theater.rightLookAction_; movedVectorState={1.0f,0.0f};
  movedFloatAction=g_theater.leftTriggerAction_; movedFloatState=1.0f;
  pressedBooleanAction=g_theater.buttonAAction_;
  *reinterpret_cast<uint8_t*>(g_config.frontEndMenuActive)=0;
  g_theater.PollInput();
  gtasa_vr::GtaPadState nativePad{}; nativePad.dPadUp=255;
  g_theater.ApplyNativePadInput(nativePad);
  Check(sentInputs.empty()&&nativePad.rightStickX==0&&nativePad.buttonSquare==255&&
        nativePad.rightShoulder1==255&&nativePad.dPadUp==255,
        "snap turn consumes only horizontal look; jump and aim preserve mod input");
  Check(Near(g_theater.snapTurn_.yawRadians,0.523598776f),
        "right OpenXR thumbstick produces one thirty-degree VR rig turn");
  HookedPadUpdate();
  Check(setHeadingCalls==1&&Near(lastSetHeading,1.047197551f)&&
        Near(*reinterpret_cast<float*>(playerPedFixture+0x558),lastSetHeading)&&
        Near(*reinterpret_cast<float*>(playerPedFixture+0x55C),lastSetHeading)&&
        Near(g_theater.snapTurn_.pendingBodyRadians,0.0f),
        "next native pad update commits one body turn and keeps current/goal headings aligned");
  HookedPadUpdate();
  Check(setHeadingCalls==1,"held thumbstick cannot turn the body a second time");
  validHandTracking=true;
  movedVectorAction=g_theater.leftMoveAction_; movedVectorState={0.0f,1.0f};
  Check(g_theater.RenderCompleteFrame(g_d3d9Device,&DrawTail),
        "turned rig renders another complete stereo frame");
  gtasa_vr::GtaPadState snapMovePad{};
  g_theater.ApplyNativePadInput(snapMovePad);
  Check(snapMovePad.leftStickX==64&&snapMovePad.leftStickY==-111,
        "native forward movement shares the thirty-degree rig snap seen by both eyes");
  Check(Near(eyeMatrices[0].forward.x,0.5f)&&
        Near(eyeMatrices[1].forward.x,0.5f)&&
        Near(eyeMatrices[0].forward.y,0.8660254f)&&
        Near(eyeMatrices[1].forward.y,0.8660254f),
        "both stereo eyes receive the same rightward rig yaw");
  const auto leftFromRig=gtasa_vr::Subtract(
      g_theater.armFrame_.leftHand.position,g_theater.vrCameraBasis_.position);
  const auto rightFromRig=gtasa_vr::Subtract(
      g_theater.armFrame_.rightHand.position,g_theater.vrCameraBasis_.position);
  Check(g_theater.armFrame_.valid &&
        Near(gtasa_vr::Dot(leftFromRig,g_theater.vrCameraBasis_.right),0.25f)&&
        Near(gtasa_vr::Dot(rightFromRig,g_theater.vrCameraBasis_.right),-0.25f)&&
        Near(gtasa_vr::Dot(leftFromRig,g_theater.vrCameraBasis_.forward),0.35f)&&
        Near(gtasa_vr::Dot(rightFromRig,g_theater.vrCameraBasis_.forward),0.35f),
        "both controller targets receive the same rig yaw as both stereo eyes");
  Check(std::memcmp(camera,&base,sizeof(base))==0,
        "turned rig still restores GTA's original camera byte-for-byte");
  camera->right={0.8660254f,-0.5f,0};
  camera->forward={0.5f,0.8660254f,0};
  const auto followedCamera=*camera;
  Check(g_theater.RenderCompleteFrame(g_d3d9Device,&DrawTail),
        "GTA camera catch-up renders a subsequent stereo frame");
  Check(Near(g_theater.snapTurn_.yawRadians,0.0f)&&
        Near(g_theater.snapTurn_.cameraCatchupRadians,0.0f)&&
        Near(eyeMatrices[0].forward.x,0.5f)&&Near(eyeMatrices[1].forward.x,0.5f)&&
        std::memcmp(camera,&followedCamera,sizeof(followedCamera))==0,
        "native camera follow-through replaces rig yaw without double-turning either eye");
  gtasa_vr::GtaPadState caughtUpPad{};
  g_theater.ApplyNativePadInput(caughtUpPad);
  Check(caughtUpPad.leftStickX==0&&caughtUpPad.leftStickY==-128,
        "camera catch-up removes the extra movement yaw exactly once");
  g_turnMode=gtasa_vr::TurnMode::Smooth;
  g_smoothTurnDegreesPerSecond=90.0f;
  movedVectorAction=g_theater.rightLookAction_; movedVectorState={1.0f,0.0f};
  const int bodyCallsBeforeSmooth=setHeadingCalls;
  g_theater.PollInput(1000000000);
  Check(Near(g_theater.snapTurn_.yawRadians,0.0f),
        "first smooth-turn sample establishes time without rotating");
  g_theater.PollInput(1011111111);
  gtasa_vr::GtaPadState smoothPad{};
  g_theater.ApplyNativePadInput(smoothPad);
  Check(Near(g_theater.snapTurn_.yawRadians,0.017453293f)&&
        smoothPad.rightStickX==0,
        "held stick produces a time-scaled rig turn without also orbiting GTA's camera");
  HookedPadUpdate();
  Check(setHeadingCalls==bodyCallsBeforeSmooth+1&&
        Near(g_theater.snapTurn_.pendingBodyRadians,0.0f),
        "smooth visual turn commits the same player-body angle once");
  movedVectorAction=XR_NULL_HANDLE; movedVectorState={};
  Check(g_theater.RenderCompleteFrame(g_d3d9Device,&DrawTail)&&
        Near(eyeMatrices[0].forward.x,std::sin(0.523598776f+0.017453293f)),
        "both eyes see continuous smooth yaw from the shared rig");
  camera->right={std::cos(0.523598776f+0.017453293f),
                 -std::sin(0.523598776f+0.017453293f),0};
  camera->forward={std::sin(0.523598776f+0.017453293f),
                   std::cos(0.523598776f+0.017453293f),0};
  Check(g_theater.RenderCompleteFrame(g_d3d9Device,&DrawTail)&&
        Near(g_theater.snapTurn_.yawRadians,0.0f)&&
        Near(eyeMatrices[0].forward.x,camera->forward.x),
        "GTA camera follow-through removes a smooth turn without doubling it");
  g_turnMode=gtasa_vr::TurnMode::Snap;
  g_snapTurnDegrees=45.0f;
  g_theater.snapTurn_={};
  movedVectorAction=g_theater.rightLookAction_; movedVectorState={0.0f,0.0f};
  g_theater.PollInput();
  movedVectorState={1.0f,0.0f};
  g_theater.PollInput();
  Check(Near(g_theater.snapTurn_.yawRadians,0.785398163f)&&
        Near(g_theater.snapTurn_.pendingBodyRadians,0.785398163f),
        "production input path uses selected forty-five-degree snap for rig and body");
  g_theater.snapTurn_={};
  g_snapTurnDegrees=30.0f;
  *camera=base;
  movedVectorAction=XR_NULL_HANDLE; movedVectorState={};
  movedFloatAction=XR_NULL_HANDLE; movedFloatState=0.0f;
  pressedBooleanAction=XR_NULL_HANDLE;
  *reinterpret_cast<uint8_t*>(g_config.frontEndMenuActive)=1;
  pressedBooleanAction=g_theater.buttonAAction_;
  movedVectorAction=g_theater.rightLookAction_; movedVectorState={1.0f,0.0f};
  const float yawBeforeMenu=g_theater.snapTurn_.yawRadians;
  g_theater.PollInput();
  Check(Near(g_theater.snapTurn_.yawRadians,yawBeforeMenu)&&!g_theater.snapTurn_.armed,
        "frontend menu cannot turn or leave a held stick armed");
  movedVectorAction=XR_NULL_HANDLE; movedVectorState={};
  gtasa_vr::GtaPadState menuPad{};
  g_theater.ApplyNativePadInput(menuPad);
  Check(menuPad.buttonCross==255,
        "A selects a frontend item through GTA native CPad");
  pressedBooleanAction=XR_NULL_HANDLE;
  movedVectorAction=g_theater.leftMoveAction_; movedVectorState={0.0f,1.0f};
  g_theater.PollInput();
  menuPad={}; g_theater.ApplyNativePadInput(menuPad);
  Check(menuPad.buttonCross==0 && menuPad.leftStickY==-128,
        "left stick navigates frontend and releasing A clears select");
  movedVectorAction=XR_NULL_HANDLE; movedVectorState={};
  pressedBooleanAction=g_theater.buttonBAction_;
  g_theater.PollInput();
  menuPad={}; g_theater.ApplyNativePadInput(menuPad);
  Check(menuPad.buttonTriangle==255,"B goes back through GTA native CPad");
  pressedBooleanAction=XR_NULL_HANDLE;
  *reinterpret_cast<uint8_t*>(g_config.frontEndMenuActive)=0;
  // Exercise the actual production x86 stack shim many times.
  auto* emptyTail=reinterpret_cast<uint8_t*>(NativeAddress(0x500000));
  emptyTail[0]=0x83; emptyTail[1]=0xC4; emptyTail[2]=8; emptyTail[3]=0xC3;
  g_renderTailTrampoline=emptyTail;
  for (unsigned n=0;n<10000;++n) CallOriginalRenderTail();
  Check(true,"render-tail scratch stack remains balanced");
  g_theater.Shutdown();
  g_d3d9Device->Release(); g_d3d9Device=nullptr; d3d->Release();
  DestroyWindow(testWindow);
  for (void* region : fixtureRegions) VirtualFree(region,0,MEM_RELEASE);
  if (failures) return 1;
  std::cout << "Production bridge integration: stereo pixels, menus, tracking loss, input release and x86 stack passed\n";
}

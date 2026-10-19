#define XR_NO_PROTOTYPES
#include <openxr/openxr.h>
#include <windows.h>
#include <string>
#include <iostream>
#include <stdexcept>
void ok(XrResult r){if(XR_FAILED(r))throw std::runtime_error("Loader integration failed: "+std::to_string(r));}
int wmain(int argc,wchar_t** argv){
 if(argc!=3)return 2;
 auto loader=LoadLibraryW(argv[1]);if(!loader)throw std::runtime_error("Official loader missing");
 auto get=reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(loader,"xrGetInstanceProcAddr"));
 PFN_xrVoidFunction fn{};ok(get(XR_NULL_HANDLE,"xrCreateInstance",&fn));
 XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};info.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);
 strcpy_s(info.applicationInfo.applicationName,"AVRcade controller adapter test");
 XrInstance instance{};ok(reinterpret_cast<PFN_xrCreateInstance>(fn)(&info,&instance));
 ok(get(instance,"xrStringToPath",&fn));auto path=reinterpret_cast<PFN_xrStringToPath>(fn);
 XrPath profile{},button{};ok(path(instance,"/interaction_profiles/oculus/touch_controller",&profile));ok(path(instance,"/user/hand/left/input/x/click",&button));
 ok(get(instance,"xrCreateActionSet",&fn));XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};strcpy_s(setInfo.actionSetName,"main");strcpy_s(setInfo.localizedActionSetName,"Main");XrActionSet set{};ok(reinterpret_cast<PFN_xrCreateActionSet>(fn)(instance,&setInfo,&set));
 ok(get(instance,"xrCreateAction",&fn));XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};strcpy_s(actionInfo.actionName,"duck");strcpy_s(actionInfo.localizedActionName,"Duck");actionInfo.actionType=XR_ACTION_TYPE_BOOLEAN_INPUT;XrAction action{};ok(reinterpret_cast<PFN_xrCreateAction>(fn)(set,&actionInfo,&action));
 XrActionSuggestedBinding binding{action,button};XrInteractionProfileSuggestedBinding suggestions{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};suggestions.interactionProfile=profile;suggestions.countSuggestedBindings=1;suggestions.suggestedBindings=&binding;
 ok(get(instance,"xrSuggestInteractionProfileBindings",&fn));ok(reinterpret_cast<PFN_xrSuggestInteractionProfileBindings>(fn)(instance,&suggestions));
 auto runtime=LoadLibraryW(argv[2]);auto captured=reinterpret_cast<const char*(*)()>(GetProcAddress(runtime,"CapturedBinding"));
 if(!captured||std::string(captured())!="/user/hand/left/input/y/click")throw std::runtime_error("Layer did not remap through official loader");
 ok(get(instance,"xrDestroyInstance",&fn));ok(reinterpret_cast<PFN_xrDestroyInstance>(fn)(instance));
 std::cout<<"Official OpenXR loader -> AVRcade layer -> fake runtime: mapped input PASS\n";
}

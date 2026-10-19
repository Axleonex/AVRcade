#include "layer.cpp"
#include <stdexcept>
#include <iostream>
#include <type_traits>
template<class T>T fakeHandle(uintptr_t n){if constexpr(std::is_pointer_v<T>)return reinterpret_cast<T>(n);else return static_cast<T>(n);}
void require(bool value){if(!value)throw std::runtime_error("Controller remap test failed");}
std::unordered_map<XrPath,std::string> paths;std::vector<XrActionSuggestedBinding> captured;XrResult response=XR_SUCCESS;
XrResult XRAPI_CALL fakeString(XrInstance,const char* text,XrPath* path){
 for(auto& p:paths)if(p.second==text){*path=p.first;return XR_SUCCESS;}
 *path=paths.size()+1;paths[*path]=text;return XR_SUCCESS;
}
XrResult XRAPI_CALL fakePath(XrInstance,XrPath path,uint32_t capacity,uint32_t* size,char* buffer){
 if(!paths.contains(path))return XR_ERROR_PATH_INVALID;*size=static_cast<uint32_t>(paths[path].size()+1);
 if(capacity){if(capacity<*size)return XR_ERROR_SIZE_INSUFFICIENT;memcpy(buffer,paths[path].c_str(),*size);}return XR_SUCCESS;
}
XrResult XRAPI_CALL fakeSuggest(XrInstance,const XrInteractionProfileSuggestedBinding* info){
 captured.assign(info->suggestedBindings,info->suggestedBindings+info->countSuggestedBindings);return response;
}
XrResult XRAPI_CALL fakeDestroy(XrInstance){return XR_SUCCESS;}
XrResult XRAPI_CALL fakeGet(XrInstance,const char* name,PFN_xrVoidFunction* fn){
 if(strcmp(name,"xrStringToPath")==0)*fn=reinterpret_cast<PFN_xrVoidFunction>(fakeString);
 else if(strcmp(name,"xrPathToString")==0)*fn=reinterpret_cast<PFN_xrVoidFunction>(fakePath);
 else if(strcmp(name,"xrSuggestInteractionProfileBindings")==0)*fn=reinterpret_cast<PFN_xrVoidFunction>(fakeSuggest);
 else if(strcmp(name,"xrDestroyInstance")==0)*fn=reinterpret_cast<PFN_xrVoidFunction>(fakeDestroy);
 else return XR_ERROR_FUNCTION_UNSUPPORTED;return XR_SUCCESS;
}
XrResult XRAPI_CALL fakeCreate(const XrInstanceCreateInfo*,const XrApiLayerCreateInfo*,XrInstance* instance){*instance=fakeHandle<XrInstance>(42);return XR_SUCCESS;}
int main(){
 const std::string x="/user/hand/left/input/x/click",y="/user/hand/left/input/y/click";
 auto quest=avrcade::Parse("quest-touch|"+x+"|"+y+"\nvalve-index|"+x+"|"+x+"\n","quest-touch");
 require(avrcade::Remap(quest,x)==y);
 require(avrcade::Remap(quest,"/user/hand/left/input/grip/pose")=="/user/hand/left/input/grip/pose");
 require(avrcade::Remap(quest,"/user/hand/left/input/x/touch")=="/user/hand/left/input/x/touch");
 require(avrcade::Remap(quest,"/user/hand/left/input/thumbstick")=="/user/hand/left/input/thumbstick");
 require(avrcade::Parse("quest-touch|"+x+"|/user/hand/right/input/a/click\n","quest-touch").empty());
 require(avrcade::Remap({},x)==x);
 require(avrcade::Parse("broken\n","quest-touch").empty());
 XrApiLayerNextInfo next{};next.nextCreateApiLayerInstance=fakeCreate;next.nextGetInstanceProcAddr=fakeGet;
 XrApiLayerCreateInfo chain{};chain.nextInfo=&next;XrInstance instance{};XrInstanceCreateInfo create{XR_TYPE_INSTANCE_CREATE_INFO};
 require(Create(&create,&chain,&instance)==XR_SUCCESS);
 XrPath profile{},button{},pose{};fakeString(instance,"/interaction_profiles/oculus/touch_controller",&profile);
 fakeString(instance,x.c_str(),&button);fakeString(instance,"/user/hand/left/input/grip/pose",&pose);
 XrActionSuggestedBinding original[]={{fakeHandle<XrAction>(1),button},{fakeHandle<XrAction>(2),pose}};
 XrInteractionProfileSuggestedBinding info{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
 info.interactionProfile=profile;info.countSuggestedBindings=2;info.suggestedBindings=original;
 PFN_xrVoidFunction proc{};require(GetProc(instance,"xrSuggestInteractionProfileBindings",&proc)==XR_SUCCESS);
 auto submit=reinterpret_cast<PFN_xrSuggestInteractionProfileBindings>(proc);
 require(submit(instance,&info)==XR_SUCCESS);require(paths[captured[0].binding]==y);
 require(captured[1].binding==pose);require(original[0].binding==button);
 response=XR_ERROR_PATH_UNSUPPORTED;require(submit(instance,&info)==XR_ERROR_PATH_UNSUPPORTED);response=XR_SUCCESS;
 fakeString(instance,"/interaction_profiles/valve/index_controller",&info.interactionProfile);
 require(submit(instance,&info)==XR_SUCCESS);require(captured[0].binding==button);
 require(Destroy(instance)==XR_SUCCESS);require(GetProc(instance,"xrDestroyInstance",&proc)==XR_ERROR_HANDLE_INVALID);
 std::cout<<"Controller paths: profile isolation, same-hand swaps, passthrough pose/touch/stick, malformed data PASS\n";
}

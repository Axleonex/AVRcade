#define XR_NO_PROTOTYPES
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <memory>
#include <cstring>
#include "remap.h"

namespace {
struct Dispatch {
    PFN_xrGetInstanceProcAddr get{};
    PFN_xrPathToString pathToString{};
    PFN_xrStringToPath stringToPath{};
    PFN_xrSuggestInteractionProfileBindings suggest{};
    PFN_xrDestroyInstance destroy{};
    std::string mappings;
};
std::mutex mutex;
std::unordered_map<XrInstance,std::shared_ptr<Dispatch>> instances;
std::shared_ptr<Dispatch> Find(XrInstance instance) {
    std::lock_guard lock(mutex);
    const auto it=instances.find(instance);return it==instances.end()?nullptr:it->second;
}
std::string Read(const std::filesystem::path& path) {
    std::error_code error;
    if(std::filesystem::file_size(path,error)>1024*1024 || error) return {};
    std::ifstream stream(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
std::string LoadForProcess() {
#ifdef AVRCADE_LAYER_TEST
    return "quest-touch|/user/hand/left/input/x/click|/user/hand/left/input/y/click\n";
#else
    wchar_t executable[32768]{}; const auto n=GetModuleFileNameW(nullptr,executable,32768);
    if(n==0||n==32768) return {};
    PWSTR local{};
    if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local))) return {};
    const auto routes=std::filesystem::path(local)/L"VRClient"/L"controllers"/L"runtime"/L"routes.txt";
    CoTaskMemFree(local);
    std::istringstream lines(Read(routes));std::string line;
    while(std::getline(lines,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        const auto split=line.find('|'); if(split==std::string::npos)continue;
        const auto utf8path=[](const std::string& s){return std::filesystem::path(std::u8string(
            reinterpret_cast<const char8_t*>(s.data()),s.size()));};
        const auto exe=utf8path(line.substr(0,split));
        if(_wcsicmp(exe.c_str(),executable)!=0)continue;
        const auto settings=utf8path(line.substr(split+1));
        // Only exact executable routes written by AVRcade can activate this layer.
        return Read(settings);
    }
    return {};
#endif
}
std::string Path(XrInstance instance,const Dispatch& d,XrPath path) {
    uint32_t n{};if(XR_FAILED(d.pathToString(instance,path,0,&n,nullptr))||n>1024||n==0)return {};
    std::vector<char> value(n);
    if(XR_FAILED(d.pathToString(instance,path,n,&n,value.data())))return {};
    return value.data();
}
XrResult XRAPI_CALL Suggest(XrInstance instance,const XrInteractionProfileSuggestedBinding* info) {
    auto d=Find(instance);if(!d)return XR_ERROR_HANDLE_INVALID;
    if(!info||info->countSuggestedBindings==0||d->mappings.empty())return d->suggest(instance,info);
    const auto profile=Path(instance,*d,info->interactionProfile);
    const auto family=profile=="/interaction_profiles/oculus/touch_controller"?"quest-touch":
        profile=="/interaction_profiles/valve/index_controller"?"valve-index":"";
    if(*family==0)return d->suggest(instance,info);
    const auto mapping=avrcade::Parse(d->mappings,family);
    if(mapping.empty())return d->suggest(instance,info);
    std::vector<XrActionSuggestedBinding> bindings(info->suggestedBindings,info->suggestedBindings+info->countSuggestedBindings);
    for(auto& binding:bindings) {
        const auto original=Path(instance,*d,binding.binding);
        const auto replacement=avrcade::Remap(mapping,original);
        if(replacement==original)continue;
        XrPath path{};
        if(XR_FAILED(d->stringToPath(instance,replacement.c_str(),&path)))return XR_ERROR_PATH_INVALID;
        binding.binding=path;
    }
    auto edited=*info;edited.suggestedBindings=bindings.data();
    const auto result=d->suggest(instance,&edited);
    // Surface invalid bindings to the application; do not silently report custom
    // controls as active while applying defaults.
    OutputDebugStringA(XR_SUCCEEDED(result)?"AVRcade controller mappings applied\n":"AVRcade controller mappings rejected by runtime\n");
    return result;
}
XrResult XRAPI_CALL Destroy(XrInstance instance) {
    auto d=Find(instance);if(!d)return XR_ERROR_HANDLE_INVALID;
    auto result=d->destroy(instance);if(XR_SUCCEEDED(result)){std::lock_guard lock(mutex);instances.erase(instance);}return result;
}
XrResult XRAPI_CALL GetProc(XrInstance instance,const char* name,PFN_xrVoidFunction* function) {
    if(!name||!function)return XR_ERROR_VALIDATION_FAILURE;
    auto d=Find(instance);if(!d)return XR_ERROR_HANDLE_INVALID;
    const auto result=d->get(instance,name,function);if(XR_FAILED(result))return result;
    if(strcmp(name,"xrSuggestInteractionProfileBindings")==0)*function=reinterpret_cast<PFN_xrVoidFunction>(Suggest);
    else if(strcmp(name,"xrDestroyInstance")==0)*function=reinterpret_cast<PFN_xrVoidFunction>(Destroy);
    return result;
}
XrResult XRAPI_CALL Create(const XrInstanceCreateInfo* info,const XrApiLayerCreateInfo* layer,XrInstance* instance) {
    if(!layer||!layer->nextInfo||!instance)return XR_ERROR_INITIALIZATION_FAILED;
    const auto next=layer->nextInfo;
    auto chain=*layer;chain.nextInfo=next->next;
    auto result=next->nextCreateApiLayerInstance(info,&chain,instance);if(XR_FAILED(result))return result;
    auto d=std::make_shared<Dispatch>();d->get=next->nextGetInstanceProcAddr;
    const auto resolve=[&](const char* name,auto& pointer){
        return d->get(*instance,name,reinterpret_cast<PFN_xrVoidFunction*>(&pointer));};
    resolve("xrDestroyInstance",d->destroy);
    if(XR_FAILED(resolve("xrPathToString",d->pathToString))||XR_FAILED(resolve("xrStringToPath",d->stringToPath))||
        XR_FAILED(resolve("xrSuggestInteractionProfileBindings",d->suggest))||!d->destroy) {
        if(d->destroy)d->destroy(*instance);return XR_ERROR_INITIALIZATION_FAILED;
    }
    try{d->mappings=LoadForProcess();}catch(...){d->mappings.clear();}
    {std::lock_guard lock(mutex);instances[*instance]=d;}
    return result;
}
}
extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loader,const char*,XrNegotiateApiLayerRequest* request) {
    if(!loader||!request||loader->structType!=XR_LOADER_INTERFACE_STRUCT_LOADER_INFO||
        request->structType!=XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST||loader->minInterfaceVersion>1||
        loader->maxInterfaceVersion<1)return XR_ERROR_INITIALIZATION_FAILED;
    request->layerInterfaceVersion=1;request->layerApiVersion=XR_MAKE_VERSION(1,0,0);
    request->getInstanceProcAddr=GetProc;request->createApiLayerInstance=Create;return XR_SUCCESS;
}

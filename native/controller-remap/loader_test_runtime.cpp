#define XR_NO_PROTOTYPES
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include <string>
#include <unordered_map>
#include <cstring>
#include <type_traits>
namespace {
template<class T>T handle(uintptr_t n){if constexpr(std::is_pointer_v<T>)return reinterpret_cast<T>(n);else return static_cast<T>(n);}
std::unordered_map<XrPath,std::string> paths;std::string captured;
XrResult XRAPI_CALL Create(const XrInstanceCreateInfo*,XrInstance* i){*i=handle<XrInstance>(41);return XR_SUCCESS;}
XrResult XRAPI_CALL Destroy(XrInstance){return XR_SUCCESS;}
XrResult XRAPI_CALL Extensions(const char*,uint32_t,uint32_t* count,XrExtensionProperties*){*count=0;return XR_SUCCESS;}
XrResult XRAPI_CALL String(XrInstance,const char* text,XrPath* p){for(auto& pair:paths)if(pair.second==text){*p=pair.first;return XR_SUCCESS;}*p=paths.size()+1;paths[*p]=text;return XR_SUCCESS;}
XrResult XRAPI_CALL Path(XrInstance,XrPath p,uint32_t cap,uint32_t* n,char* out){if(!paths.contains(p))return XR_ERROR_PATH_INVALID;*n=static_cast<uint32_t>(paths[p].size()+1);if(cap){if(cap<*n)return XR_ERROR_SIZE_INSUFFICIENT;memcpy(out,paths[p].c_str(),*n);}return XR_SUCCESS;}
XrResult XRAPI_CALL Suggest(XrInstance,const XrInteractionProfileSuggestedBinding* s){captured=s->countSuggestedBindings?paths[s->suggestedBindings[0].binding]:"";return XR_SUCCESS;}
XrResult XRAPI_CALL ActionSet(XrInstance,const XrActionSetCreateInfo*,XrActionSet* a){*a=handle<XrActionSet>(42);return XR_SUCCESS;}
XrResult XRAPI_CALL Action(XrActionSet,const XrActionCreateInfo*,XrAction* a){*a=handle<XrAction>(43);return XR_SUCCESS;}
XrResult XRAPI_CALL Get(XrInstance,const char* name,PFN_xrVoidFunction* fn){
 #define API(n,f) if(strcmp(name,n)==0){*fn=reinterpret_cast<PFN_xrVoidFunction>(f);return XR_SUCCESS;}
 API("xrGetInstanceProcAddr",Get) API("xrCreateInstance",Create) API("xrDestroyInstance",Destroy)
 API("xrEnumerateInstanceExtensionProperties",Extensions) API("xrStringToPath",String) API("xrPathToString",Path)
 API("xrSuggestInteractionProfileBindings",Suggest) API("xrCreateActionSet",ActionSet) API("xrCreateAction",Action)
 *fn=nullptr;return XR_ERROR_FUNCTION_UNSUPPORTED;
 #undef API
}
}
extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderRuntimeInterface(const XrNegotiateLoaderInfo*,XrNegotiateRuntimeRequest* r){r->runtimeInterfaceVersion=1;r->runtimeApiVersion=XR_MAKE_VERSION(1,0,0);r->getInstanceProcAddr=Get;return XR_SUCCESS;}
extern "C" __declspec(dllexport) const char* CapturedBinding(){return captured.c_str();}

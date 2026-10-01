#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <array>
#include <filesystem>
#include <vector>
#include <stdexcept>
#include <string>

namespace {
struct Handle {
    HANDLE value;
    ~Handle(){ if(value && value!=INVALID_HANDLE_VALUE) CloseHandle(value); }
};
constexpr wchar_t expectedHash[]=L"3a87f92f011e5dc9179ddf733cf08be2b39ea6e5b7a8a9e3a9a72dafcc1b104d";
bool SupportedPatchedBuild(const wchar_t* path,HANDLE file) {
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file,&size) || size.QuadPart<15'000'000 || size.QuadPart>20'000'000) return false;
    unsigned char dos[64]{}; DWORD count=0;
    LARGE_INTEGER start{};
    if(!SetFilePointerEx(file,start,nullptr,FILE_BEGIN) || !ReadFile(file,dos,sizeof(dos),&count,nullptr) ||
        count!=sizeof(dos) || dos[0]!='M' || dos[1]!='Z') return false;
    const auto peOffset=*reinterpret_cast<const LONG*>(dos+0x3c);
    if(peOffset<64 || peOffset>size.QuadPart-26) return false;
    LARGE_INTEGER pe{}; pe.QuadPart=peOffset;
    unsigned char header[26]{};
    if(!SetFilePointerEx(file,pe,nullptr,FILE_BEGIN) || !ReadFile(file,header,sizeof(header),&count,nullptr) ||
        count!=sizeof(header) || std::memcmp(header,"PE\0\0",4) ||
        *reinterpret_cast<const WORD*>(header+4)!=IMAGE_FILE_MACHINE_I386 ||
        !(*reinterpret_cast<const WORD*>(header+22)&IMAGE_FILE_LARGE_ADDRESS_AWARE) ||
        *reinterpret_cast<const WORD*>(header+24)!=IMAGE_NT_OPTIONAL_HDR32_MAGIC) return false;
    DWORD versionBytes=GetFileVersionInfoSizeW(path,nullptr);
    if(!versionBytes) return false;
    std::vector<unsigned char> version(versionBytes);
    VS_FIXEDFILEINFO* fixed=nullptr; UINT fixedBytes=0;
    return GetFileVersionInfoW(path,0,versionBytes,version.data()) &&
        VerQueryValueW(version.data(),L"\\",reinterpret_cast<void**>(&fixed),&fixedBytes) &&
        fixed && fixedBytes>=sizeof(*fixed) && fixed->dwSignature==VS_FFI_SIGNATURE &&
        HIWORD(fixed->dwFileVersionMS)==1 && LOWORD(fixed->dwFileVersionMS)==4 &&
        HIWORD(fixed->dwFileVersionLS)==0 && LOWORD(fixed->dwFileVersionLS)==525;
}
bool SupportedFile(const wchar_t* path) {
    Handle file{CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
    if(file.value==INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return false;
    bool ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    std::array<unsigned char,65536> bytes{};
    DWORD count=0;
    while(ok) {
        if(!ReadFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)){ok=false;break;}
        if(!count) break;
        ok=BCryptHashData(hash,bytes.data(),count,0)>=0;
    }
    unsigned char digest[32]{};
    if(ok) ok=BCryptFinishHash(hash,digest,32,0)>=0;
    if(hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm,0);
    wchar_t hex[65]{};
    for(unsigned i=0;i<32;++i) swprintf_s(hex+i*2,3,L"%02x",digest[i]);
    return ok && (wcscmp(hex,expectedHash)==0 || SupportedPatchedBuild(path,file.value));
}
uintptr_t RemoteModule(DWORD pid,const std::filesystem::path& path) {
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid)};
    MODULEENTRY32W item{sizeof(item)};
    if(!Module32FirstW(snapshot.value,&item)) return 0;
    do {
        std::error_code error;
        if(std::filesystem::equivalent(path,item.szExePath,error) && !error)
            return reinterpret_cast<uintptr_t>(item.modBaseAddr);
    } while(Module32NextW(snapshot.value,&item));
    return 0;
}
DWORD FindGame() {
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)};
    PROCESSENTRY32W item{sizeof(item)};
    if(!Process32FirstW(snapshot.value,&item)) return 0;
    DWORD found=0;
    do {
        if(_wcsicmp(item.szExeFile,L"FalloutNV.exe")) continue;
        if(found) throw std::runtime_error("More than one New Vegas game is running. Close the extra copy.");
        found=item.th32ProcessID;
    } while(Process32NextW(snapshot.value,&item));
    return found;
}
DWORD LaunchSteamGame(const std::filesystem::path& game) {
    if(_wcsicmp(game.filename().c_str(),L"FalloutNV.exe") || !SupportedFile(game.c_str()))
        throw std::runtime_error("Select a supported Steam New Vegas game folder in VRClient. No game was launched.");
    if(!SetEnvironmentVariableW(L"SteamAppId",L"22380") ||
       !SetEnvironmentVariableW(L"SteamGameId",L"22380"))
        throw std::runtime_error("Could not set the Steam identity needed to start New Vegas.");
    STARTUPINFOW startup{}; startup.cb=sizeof(startup);
    PROCESS_INFORMATION child{};
    const auto executable=game.wstring();
    const auto workingDirectory=game.parent_path().wstring();
    if(!CreateProcessW(executable.c_str(),nullptr,nullptr,nullptr,FALSE,0,nullptr,
        workingDirectory.c_str(),&startup,&child))
        throw std::runtime_error("New Vegas could not start. Open Steam and check that the purchased game is installed.");
    Handle process{child.hProcess};
    Handle thread{child.hThread};
    WaitForInputIdle(process.value,10000);
    if(WaitForSingleObject(process.value,0)==WAIT_OBJECT_0)
        throw std::runtime_error("New Vegas exited before opening its game window. Try launching the flat game first.");
    MessageBoxW(nullptr,L"Wait until the New Vegas main menu is visible, then click OK to attach the experimental VR adapter.",
        L"VRClient — New Vegas",MB_OK|MB_ICONINFORMATION);
    if(WaitForSingleObject(process.value,0)==WAIT_OBJECT_0)
        throw std::runtime_error("New Vegas closed before the VR adapter could attach.");
    return child.dwProcessId;
}
void ShowGame(DWORD pid) {
    EnumWindows([](HWND window,LPARAM requested)->BOOL {
        DWORD owner=0; GetWindowThreadProcessId(window,&owner);
        if(owner!=static_cast<DWORD>(requested) || !IsWindowVisible(window) || GetWindow(window,GW_OWNER)) return TRUE;
        ShowWindow(window,SW_RESTORE);
        SetForegroundWindow(window);
        return FALSE;
    },static_cast<LPARAM>(pid));
}
DWORD Call(HANDLE process,uintptr_t function,void* argument,bool& completed) {
    completed=false;
    Handle thread{CreateRemoteThread(process,nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(function),argument,0,nullptr)};
    if(!thread.value) throw std::runtime_error("Could not start the adapter. Run Steam and this launcher at the same privilege level.");
    if(WaitForSingleObject(thread.value,10000)!=WAIT_OBJECT_0)
        throw std::runtime_error("Adapter startup is still pending. Do not retry; close the game normally before trying again.");
    completed=true;
    DWORD result=0;
    if(!GetExitCodeThread(thread.value,&result)) throw std::runtime_error("Could not read adapter startup result.");
    return result;
}
void Attach(DWORD pid,bool stop) {
    Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ|PROCESS_VM_WRITE|
        PROCESS_VM_OPERATION|PROCESS_CREATE_THREAD,FALSE,pid)};
    if(!process.value) throw std::runtime_error("Cannot access the running New Vegas process.");
    wchar_t image[32768]{}; DWORD size=32768;
    if(!QueryFullProcessImageNameW(process.value,0,image,&size) ||
        _wcsicmp(std::filesystem::path(image).filename().c_str(),L"FalloutNV.exe") || !SupportedFile(image))
        throw std::runtime_error("This test supports only the verified Steam New Vegas 1.4.0.525 executable or its legitimate large-address-aware patched form. No changes were made.");
    wchar_t own[32768]{};
    if(!GetModuleFileNameW(nullptr,own,32768)) throw std::runtime_error("Cannot locate this launcher.");
    auto dll=std::filesystem::path(own).parent_path()/L"vrclient_fnv_stereo.dll";
    if(!std::filesystem::is_regular_file(dll)) throw std::runtime_error("The native stereo DLL is missing beside this launcher.");
    uintptr_t remoteBase=RemoteModule(pid,dll);
    if(!remoteBase && stop) return;
    if(!remoteBase) {
        auto load=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");
        HMODULE owner=nullptr;
        if(!load || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(load),&owner)) throw std::runtime_error("Cannot resolve the system loader.");
        wchar_t ownerPath[32768]{};
        GetModuleFileNameW(owner,ownerPath,32768);
        uintptr_t remoteOwner=RemoteModule(pid,ownerPath);
        if(!remoteOwner) throw std::runtime_error("System loader identity does not match the game.");
        const auto function=remoteOwner+(reinterpret_cast<uintptr_t>(load)-reinterpret_cast<uintptr_t>(owner));
        const auto text=dll.wstring(); const size_t bytes=(text.size()+1)*sizeof(wchar_t);
        void* memory=VirtualAllocEx(process.value,nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        if(!memory) throw std::runtime_error("Cannot allocate adapter path.");
        SIZE_T written=0;
        if(!WriteProcessMemory(process.value,memory,text.c_str(),bytes,&written) || written!=bytes) {
            VirtualFreeEx(process.value,memory,0,MEM_RELEASE); throw std::runtime_error("Cannot pass adapter path.");
        }
        bool completed=false;
        DWORD loaded=0;
        try { loaded=Call(process.value,function,memory,completed); }
        catch(...) {
            // On timeout the thread may still read its argument. Keep it alive
            // until process exit instead of freeing memory out from under it.
            if(completed) VirtualFreeEx(process.value,memory,0,MEM_RELEASE);
            throw;
        }
        VirtualFreeEx(process.value,memory,0,MEM_RELEASE);
        if(!loaded || !(remoteBase=RemoteModule(pid,dll))) throw std::runtime_error("Windows could not load the native stereo adapter.");
    }
    HMODULE local=LoadLibraryExW(dll.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!local) throw std::runtime_error("Cannot inspect adapter exports.");
    auto entry=GetProcAddress(local,stop ? "FnvStop" : "FnvStart");
    const auto offset=reinterpret_cast<uintptr_t>(entry)-reinterpret_cast<uintptr_t>(local);
    FreeLibrary(local);
    if(!entry) throw std::runtime_error("Adapter startup export is missing.");
    bool completed=false;
    if(Call(process.value,remoteBase+offset,nullptr,completed)!=1)
        throw std::runtime_error("Adapter refused to start. See fnv-native.log beside this launcher. The game remains unverified for VR.");
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    bool quiet=false;
    bool stop=false;
    try {
        std::filesystem::path game;
        int count=0;
        auto arguments=CommandLineToArgvW(GetCommandLineW(),&count);
        if(!arguments) throw std::runtime_error("Could not read the native launcher arguments.");
        for(int index=1;index<count;++index) {
            if(!_wcsicmp(arguments[index],L"--quiet")) quiet=true;
            else if(!_wcsicmp(arguments[index],L"--stop")) stop=true;
            else if(!_wcsicmp(arguments[index],L"--game") && index+1<count) game=arguments[++index];
            else { LocalFree(arguments); throw std::runtime_error("Invalid native launcher arguments."); }
        }
        LocalFree(arguments);
        DWORD pid=FindGame();
        if(!pid && !quiet && !stop) {
            if(game.empty()) throw std::runtime_error("Select the New Vegas game folder in VRClient before starting native VR.");
            pid=LaunchSteamGame(game);
        }
        if(!pid) throw std::runtime_error("New Vegas is not running. Open the game (not just its launcher) first.");
        Attach(pid,stop);
        if(!stop) ShowGame(pid);
        if(!quiet) MessageBoxW(nullptr,stop ? L"Stereo disabled. Close the game normally to unload the adapter." :
            L"Experimental adapter attached. Use your existing controls; F8 recenters the headset. Frame submission is recorded in fnv-native.log. Attachment alone does not confirm working VR.",
            L"VRClient — New Vegas",MB_OK|MB_ICONINFORMATION);
        return 0;
    } catch(const std::exception& error) {
        if(!quiet) MessageBoxA(nullptr,error.what(),"VRClient — New Vegas",MB_OK|MB_ICONERROR);
        OutputDebugStringA(error.what());
        return 1;
    }
}

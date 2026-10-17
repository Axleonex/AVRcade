// Exercise the production callback with Windows delivery and UEVR acknowledgment mocked.
#include <uevr/Plugin.hpp>
#include <cassert>
#include <string>
#include <vector>

static bool wrong_game{}, drawing_ui{}, delivery_failed{}, partial_send{},hmd{},can_focus{},owned_visible{true};
static DWORD foreground_process = 12;
static std::wstring delivered;
static std::string status;
static std::vector<INPUT> sent;
static std::vector<UINT> messages;
static bool (*message_gate)(HWND,UINT,WPARAM,LPARAM){};
static void log_message(const char*, ...) {}
static bool test_drawing_ui() { return drawing_ui; }
static void test_reply(const char* event, const char* data) {
    if (std::string{event}=="avrcade.keyboard.finish") { status="finish"; return; }
    assert(std::string{event} == "avrcade.keyboard.result"); status = data;
}
static DWORD test_module_name(HMODULE, wchar_t* out, DWORD size) {
    const auto path = wrong_game ? L"C:\\OtherGame.exe" : L"C:\\Backrooms-Win64-Shipping.exe";
    wcscpy_s(out, size, path);
    return static_cast<DWORD>(wcslen(path));
}
static HWND test_foreground() { return reinterpret_cast<HWND>(static_cast<uintptr_t>(foreground_process==12 ? 1 : 2)); }
static DWORD test_window_process(HWND window, DWORD* process) { *process = window==reinterpret_cast<HWND>(1) ? 12 : 99; return 1; }
static DWORD test_current_process() { return 12; }
static LRESULT test_send_message(HWND window, UINT message, WPARAM character, LPARAM data,UINT flags,UINT timeout,PDWORD_PTR result) {
    assert(window==reinterpret_cast<HWND>(1) && timeout==100 && (flags&SMTO_ABORTIFHUNG));
    if (delivery_failed) return 0;
    if (message_gate) assert(message_gate(window,message,character,data));
    messages.push_back(message); *result=0;
    if (message==WM_CHAR) delivered += static_cast<wchar_t>(character);
    return 1;
}
static bool test_hmd() { return hmd; }
static BOOL test_enum_windows(WNDENUMPROC callback,LPARAM data) { callback(reinterpret_cast<HWND>(2),data); callback(reinterpret_cast<HWND>(1),data); return TRUE; }
static int test_class_name(HWND window,LPWSTR out,int size) { wcscpy_s(out,size,window==reinterpret_cast<HWND>(1) ? L"UnrealWindow" : L"OtherWindow"); return 1; }
static BOOL test_visible(HWND) { return owned_visible; }
static BOOL test_focus(HWND window) { assert(window==reinterpret_cast<HWND>(1)); if (can_focus) foreground_process=12; return can_focus; }
static UINT test_send_input(UINT count, INPUT* inputs, int size) {
    assert(size == sizeof(INPUT));
    sent.insert(sent.end(), inputs, inputs + count);
    return partial_send && count == 6 ? 2 : count;
}
#define GetModuleFileNameW test_module_name
#define GetForegroundWindow test_foreground
#define GetWindowThreadProcessId test_window_process
#define GetCurrentProcessId test_current_process
#define SendMessageTimeoutW test_send_message
#define EnumWindows test_enum_windows
#define GetClassNameW test_class_name
#define IsWindowVisible test_visible
#define SetForegroundWindow test_focus
#define SendInput test_send_input
#include "../BackroomsTextInput.cpp"

int main() {
    UEVR_PluginFunctions functions{};
    functions.log_info = functions.log_warn = functions.log_error = log_message;
    functions.is_drawing_ui = test_drawing_ui;
    functions.dispatch_lua_event = test_reply;
    UEVR_PluginInitializeParam parameters{};
    UEVR_VRData vr{}; vr.is_hmd_active=test_hmd;
    parameters.functions = &functions;
    parameters.vr=&vr;
    uevr::API::initialize(&parameters);
    plugin.on_initialize();
    message_gate=[](HWND window,UINT message,WPARAM key,LPARAM flags) { return plugin.on_message(window,message,key,flags); };
    plugin.on_custom_event("avrcade.keyboard.begin", "");
    assert(status == "ok" && delivered.empty());
    plugin.on_custom_event("avrcade.keyboard.text", "Lobby 2_Ab-.");
    assert(status == "ok" && delivered == L"Lobby 2_Ab-.");
    plugin.on_custom_event("avrcade.keyboard.backspace", "");
    assert(status == "ok" && delivered.back() == '\b');
    assert(!plugin.on_message(reinterpret_cast<HWND>(1),WM_KEYDOWN,VK_RETURN,0) && status=="finish");
    status.clear();
    assert(!plugin.on_message(reinterpret_cast<HWND>(1),WM_KEYDOWN,VK_RETURN,1LL<<30) && status.empty());
    plugin.on_custom_event("avrcade.keyboard.commit", "");
    assert(status=="ok" && delivered.back()=='\r' && messages.back()==WM_KEYUP);
    delivered.clear();
    foreground_process = 99;
    plugin.on_custom_event("avrcade.keyboard.text", "q");
    assert(status == "unfocused" && delivered.empty());
    plugin.on_custom_event("avrcade.keyboard.clear", "");
    assert(status == "unfocused" && sent.empty());
    hmd=true;
    plugin.on_custom_event("avrcade.keyboard.text","q");
    assert(status=="ok" && delivered==L"q"); delivered.clear();
    plugin.on_custom_event("avrcade.keyboard.clear","");
    assert(status=="unfocused" && sent.empty());
    can_focus=true;
    plugin.on_custom_event("avrcade.keyboard.clear","");
    assert(status=="ok" && sent.size()==6 && foreground_process==12);
    sent.clear(); can_focus=false; foreground_process=99;
    owned_visible=false;
    plugin.on_custom_event("avrcade.keyboard.text","q");
    assert(status=="unfocused" && delivered.empty()); owned_visible=true;
    hmd=false;
    foreground_process = 12;
    drawing_ui = true;
    assert(plugin.on_message(reinterpret_cast<HWND>(1),WM_KEYDOWN,VK_RETURN,0));
    plugin.on_custom_event("avrcade.keyboard.text", "q");
    assert(status == "unfocused" && delivered.empty());
    drawing_ui = false;
    for (const auto text : {"", "bad\nname", "bad\tname", "\xC3\xA9"}) {
        plugin.on_custom_event("avrcade.keyboard.text", text);
        assert(status == "invalid" && delivered.empty());
    }
    plugin.on_custom_event("avrcade.keyboard.text", std::string(65, 'a').c_str());
    assert(status == "invalid" && delivered.empty());
    delivery_failed = true;
    plugin.on_custom_event("avrcade.keyboard.text", "q");
    assert(status == "failed" && delivered.empty());
    plugin.on_custom_event("avrcade.keyboard.commit", "");
    assert(status=="failed" && delivered.empty());
    assert(!plugin.on_message(reinterpret_cast<HWND>(1),WM_KEYDOWN,VK_RETURN,0) && status=="finish");
    delivery_failed = false;
    plugin.on_custom_event("avrcade.keyboard.clear", "");
    assert(status == "ok" && sent.size() == 6);
    assert(sent[0].ki.wVk == VK_CONTROL && sent[0].ki.dwFlags == 0);
    assert(sent[1].ki.wVk == 'A' && sent[2].ki.dwFlags == KEYEVENTF_KEYUP);
    assert(sent[3].ki.wVk == VK_CONTROL && sent[3].ki.dwFlags == KEYEVENTF_KEYUP);
    assert(sent[4].ki.wVk == VK_BACK && sent[5].ki.dwFlags == KEYEVENTF_KEYUP);
    sent.clear(); partial_send = true;
    plugin.on_custom_event("avrcade.keyboard.clear", "");
    assert(status == "failed" && sent.size() == 9);
    for (size_t i = 6; i < 9; ++i) assert(sent[i].ki.dwFlags == KEYEVENTF_KEYUP);
    plugin.on_custom_event("avrcade.keyboard.end", "");
    assert(plugin.on_message(reinterpret_cast<HWND>(1),WM_KEYDOWN,VK_RETURN,0));
    status.clear();
    plugin.on_custom_event("other.event", "q");
    plugin.on_custom_event("avrcade.backrooms.name", "VRLobby");
    plugin.on_custom_event("avrcade.keyboard.nope", "q");
    plugin.on_custom_event(nullptr, "q");
    plugin.on_custom_event("avrcade.keyboard.text", nullptr);
    assert(status.empty() && delivered.empty());
    wrong_game = true;
    plugin.on_initialize();
    plugin.on_custom_event("avrcade.keyboard.text", "q");
    assert(status.empty() && delivered.empty());
    puts("Native keyboard tests passed: text, editing, foreground/menu guard, validation, acknowledgment, partial-input cleanup.");
}

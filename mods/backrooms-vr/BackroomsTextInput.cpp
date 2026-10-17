// UEVR Lua cannot marshal FText. Use the focused game's normal Slate input.
#include <uevr/Plugin.hpp>
#include <cstring>
#include <cwchar>
#include <string_view>
#include <atomic>

class BackroomsTextInput final : public uevr::Plugin {
public:
    void on_initialize() override {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        const auto filename = std::wcsrchr(path, L'\\');
        enabled_ = filename && _wcsicmp(filename + 1, L"Backrooms-Win64-Shipping.exe") == 0;
        uevr::API::get()->log_info("[AVRcade Keyboard] Text transport %s", enabled_ ? "loaded" : "disabled: wrong game");
    }

    void on_custom_event(const char* event, const char* data) override {
        if (!enabled_ || !event || !data) return;
        const std::string_view name{event};
        constexpr std::string_view prefix{"avrcade.keyboard."};
        if (!name.starts_with(prefix)) return;
        const auto action = name.substr(prefix.size());
        if (action == "end") { keyboard_active_=false; reply("ok"); return; }
        if (action != "begin" && action != "text" && action != "backspace" && action != "clear" && action != "commit") return;
        HWND window = GetForegroundWindow();
        DWORD process{};
        GetWindowThreadProcessId(window, &process);
        if (process!=GetCurrentProcessId()) {
            window=nullptr;
            const auto vr=uevr::API::get()->param()->vr;
            // Target only this game's visible Unreal window while a headset is
            // active. Character messages never go to the desktop's foreground app.
            if (vr && vr->is_hmd_active && vr->is_hmd_active()) {
                EnumWindows([](HWND candidate,LPARAM context)->BOOL {
                    DWORD pid{}; GetWindowThreadProcessId(candidate,&pid);
                    wchar_t name[64]{}; GetClassNameW(candidate,name,64);
                    if (pid==GetCurrentProcessId() && IsWindowVisible(candidate) && std::wcscmp(name,L"UnrealWindow")==0) {
                        *reinterpret_cast<HWND*>(context)=candidate; return FALSE;
                    }
                    return TRUE;
                },reinterpret_cast<LPARAM>(&window));
            }
        }
        if (!window || uevr::API::get()->param()->functions->is_drawing_ui()) {
            reply("unfocused");
            return;
        }
        if (action == "begin") { keyboard_active_=true; reply("ok"); return; }
        if (action == "clear") {
            // SendInput is global, unlike targeted character messages. Require
            // this game to be foreground before Ctrl+A; never clear another app.
            if (GetForegroundWindow()!=window) {
                SetForegroundWindow(window);
                if (GetForegroundWindow()!=window) { reply("unfocused"); return; }
            }
            // Slate reads the actual Ctrl modifier; posted WM_KEYDOWN alone cannot select all.
            INPUT inputs[6]{};
            const WORD keys[]{VK_CONTROL, 'A', 'A', VK_CONTROL, VK_BACK, VK_BACK};
            for (size_t i = 0; i < 6; ++i) {
                inputs[i].type = INPUT_KEYBOARD;
                inputs[i].ki.wVk = keys[i];
                inputs[i].ki.dwFlags = (i == 2 || i == 3 || i == 5) ? KEYEVENTF_KEYUP : 0;
            }
            const bool sent = SendInput(6, inputs, sizeof(INPUT)) == 6;
            if (!sent) {
                INPUT releases[3]{};
                const WORD held[]{VK_CONTROL, 'A', VK_BACK};
                for (size_t i = 0; i < 3; ++i) {
                    releases[i].type = INPUT_KEYBOARD;
                    releases[i].ki.wVk = held[i];
                    releases[i].ki.dwFlags = KEYEVENTF_KEYUP;
                }
                SendInput(3, releases, sizeof(INPUT));
            }
            reply(sent ? "ok" : "failed");
            return;
        }
        if (action == "backspace") {
            reply(deliver(window, WM_CHAR, '\b', 1) ? "ok" : "failed");
            return;
        }
        if (action == "commit") {
            injecting_=true;
            const bool down=deliver(window,WM_KEYDOWN,VK_RETURN,1);
            const bool character=down && deliver(window,WM_CHAR,'\r',1);
            const bool up=deliver(window,WM_KEYUP,VK_RETURN,0xC0000001);
            injecting_=false;
            reply(down && character && up ? "ok" : "failed");
            return;
        }
        const std::string_view text{data};
        if (text.empty() || text.size() > 64) { reply("invalid"); return; }
        for (const unsigned char ch : text) {
            if (ch < 32 || ch > 126) { reply("invalid"); return; }
        }
        for (const unsigned char ch : text) {
            if (!deliver(window, WM_CHAR, ch, 1)) { reply("failed"); return; }
        }
        reply("ok");
    }

    bool on_message(HWND window,UINT message,WPARAM key,LPARAM flags) override {
        if (!enabled_ || !keyboard_active_ || injecting_ || key!=VK_RETURN ||
            uevr::API::get()->param()->functions->is_drawing_ui()) return true;
        DWORD process{}; GetWindowThreadProcessId(window,&process);
        if (process!=GetCurrentProcessId()) return true;
        if (message==WM_KEYDOWN) {
            if ((flags & (1LL<<30))==0) uevr::API::get()->dispatch_lua_event("avrcade.keyboard.finish","");
            return false;
        }
        return message!=WM_KEYUP && message!=WM_CHAR;
    }

private:
    static bool deliver(HWND window,UINT message,WPARAM key,LPARAM flags) {
        DWORD_PTR result{};
        // Avoid reporting success merely for posting to a queue. Unreal may
        // still defer Slate processing; Lua retains field focus across that gap.
        return SendMessageTimeoutW(window,message,key,flags,SMTO_BLOCK|SMTO_ABORTIFHUNG,100,&result)!=0;
    }
    static void reply(const char* status) {
        uevr::API::get()->dispatch_lua_event("avrcade.keyboard.result", status);
    }
    bool enabled_{};
    std::atomic_bool keyboard_active_{false},injecting_{false};
};

static BackroomsTextInput plugin;

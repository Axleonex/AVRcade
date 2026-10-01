// Read-only probe for the retail FNV render entry points. Steam's on-disk
// executable is packed; validate the running image before authoring hooks.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <cstring>
#include <string_view>
#include "hde32.h"
#include "../../adapters/fallout_new_vegas/camera_layout.h"
#include <limits>

// Decode only complete instructions in a bounded local copy. Never scan or
// execute remote code. Padding permits HDE's lookahead at the last instruction.
bool PrintInstructions(const unsigned char* bytes, size_t size, uintptr_t address) {
    bool first = true;
    for (size_t offset = 0; offset < size;) {
        std::array<unsigned char, 32> padded{};
        const size_t remaining = size - offset;
        std::memcpy(padded.data(), bytes + offset, (remaining < 16 ? remaining : 16));
        hde32s instruction{};
        const auto length = hde32_disasm(padded.data(), &instruction);
        if (!length || (instruction.flags & F_ERROR) || length > remaining) return false;
        std::printf("%s{\"address\":\"0x%08lX\",\"bytes\":\"", first ? "" : ",",
            static_cast<unsigned long>(address + offset));
        first = false;
        for (unsigned i = 0; i < length; ++i) std::printf("%02X", bytes[offset + i]);
        std::printf("\"");
        if (instruction.opcode == 0xE8 && !instruction.p_66)
            std::printf(",\"direct_call\":\"0x%08lX\"", static_cast<unsigned long>(
                static_cast<uint32_t>(address + offset + length) + instruction.imm.imm32));
        std::printf("}");
        offset += length;
    }
    return true;
}

int SelfTest() {
    vrclient::fnv::Camera camera{};
    camera.world.scale = 1;
    camera.frustum = {-0.5f,0.5f,0.5f,-0.5f,1,2,0,{}};
    if (!vrclient::fnv::IsPlausible(camera)) return 1;
    camera.frustum.nearPlane = std::numeric_limits<float>::quiet_NaN();
    if (vrclient::fnv::IsPlausible(camera)) return 1;
    camera.frustum.nearPlane = 3;
    if (vrclient::fnv::IsPlausible(camera)) return 1;
    // Prologue, relative call, and a disp32 camera-member access.
    const std::array<unsigned char, 32> bytes{0x55,0x8B,0xEC,0xE8,0x10,0,0,0,
        0x8B,0x81,0xAC,0,0,0};
    hde32s decoded{};
    if (hde32_disasm(bytes.data(), &decoded) != 1 || decoded.opcode != 0x55) return 1;
    if (hde32_disasm(bytes.data()+1, &decoded) != 2 || decoded.flags & F_ERROR) return 1;
    if (hde32_disasm(bytes.data()+3, &decoded) != 5 || decoded.imm.imm32 != 16) return 1;
    if (hde32_disasm(bytes.data()+8, &decoded) != 6 || decoded.disp.disp32 != 0xAC) return 1;
    // The print path must reject a truncated multibyte instruction.
    if (PrintInstructions(bytes.data()+1, 1, 0)) return 1;
    std::puts("observer decoder checks passed");
    return 0;
}

struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

void PrintCameraObservation(HANDLE process, bool world) {
    const auto read = [process](uintptr_t address, void* value, size_t size) {
        SIZE_T actual = 0;
        return ReadProcessMemory(process, reinterpret_cast<const void*>(address),
            value, size, &actual) && actual == size;
    };
    // Main::camera is a menu camera. RenderWorldSceneGraph calls 0x45C670
    // (scene graph at 0x11DEB7C), then 0x6629F0 (camera member +0xAC).
    // These observed accessors identify candidates, not an ABI guarantee.
    uint32_t main = 0, camera = 0;
    std::array<unsigned char, sizeof(vrclient::fnv::Camera)> bytes{};
    const bool ok = read(world ? 0x011DEB7C : 0x011DEA0C, &main, sizeof(main)) && main &&
        read(main + (world ? 0xAC : 0xA0), &camera, sizeof(camera)) && camera &&
        read(camera, bytes.data(), bytes.size());
    std::printf(",\"%s\":{\"read\":%s,\"root\":\"0x%08lX\",\"camera\":\"0x%08lX\",\"words\":[",
        world ? "world_camera_observation" : "camera_observation",
        ok ? "true" : "false", static_cast<unsigned long>(main), static_cast<unsigned long>(camera));
    if (ok) for (size_t offset = 0; offset < bytes.size(); offset += 4) {
        uint32_t value;
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        std::printf("%s\"%08lX\"", offset ? "," : "", static_cast<unsigned long>(value));
    }
    std::printf("]");
    if (ok) {
        vrclient::fnv::Camera value;
        std::memcpy(&value, bytes.data(), sizeof(value));
        const bool plausible = vrclient::fnv::IsPlausible(value);
        std::printf(",\"plausible\":%s", plausible ? "true" : "false");
        if (plausible) std::printf(",\"position\":[%.9g,%.9g,%.9g],\"frustum\":[%.9g,%.9g,%.9g,%.9g,%.9g,%.9g]",
            value.world.position[0], value.world.position[1], value.world.position[2],
            value.frustum.left,value.frustum.right,value.frustum.top,value.frustum.bottom,
            value.frustum.nearPlane,value.frustum.farPlane);
    }
    std::printf("}");
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring_view(argv[1]) == L"--self-test") return SelfTest();
    const bool renderMap = argc == 3 && std::wstring_view(argv[2]) == L"--render-map";
    if (argc != 2 && !renderMap) {
        std::fputs("Usage: fnv-render-observer <absolute FalloutNV.exe path> [--render-map]\n", stderr);
        return 2;
    }
    const auto requested = std::filesystem::path(argv[1]);
    std::error_code pathError;
    if (!requested.is_absolute() || _wcsicmp(requested.filename().c_str(), L"FalloutNV.exe") ||
        !std::filesystem::is_regular_file(requested, pathError) || pathError) return 2;

    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    PROCESSENTRY32W entry{sizeof(entry)};
    if (!Process32FirstW(snapshot.value, &entry)) return 3;
    do {
        if (_wcsicmp(entry.szExeFile, L"FalloutNV.exe") != 0) continue;
        Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
            FALSE, entry.th32ProcessID)};
        if (!process.value) continue;
        std::array<wchar_t, 32768> path{};
        DWORD length = static_cast<DWORD>(path.size());
        if (!QueryFullProcessImageNameW(process.value, 0, path.data(), &length)) continue;
        std::error_code error;
        if (!std::filesystem::equivalent(requested, path.data(), error) || error) continue;

        // ponytail: bounded render entries and getters called by them,
        // no process-wide scan or debugger.
        // These are observations, never authorization to install a detour.
        constexpr uintptr_t candidates[] = {0x008706B0, 0x00873200, 0x00875110, 0x00878610,
            0x004E3270, 0x0043C4B0, 0x008D80E0, 0x006629F0, 0x004EBBC0, 0x0045C670};
        std::printf("{\"pid\":%lu,\"read_only\":true,\"candidates\":[", entry.th32ProcessID);
        bool allRead = true;
        for (size_t i = 0; i < std::size(candidates); ++i) {
            std::array<unsigned char, 4096> bytes{};
            const size_t requestedBytes = renderMap ? bytes.size() : 48;
            SIZE_T read = 0;
            const bool ok = ReadProcessMemory(process.value,
                reinterpret_cast<const void*>(candidates[i]), bytes.data(), requestedBytes, &read)
                && read == requestedBytes;
            allRead &= ok;
            std::printf("%s{\"address\":\"0x%08llX\",\"read\":%s,\"bytes\":\"",
                i ? "," : "", static_cast<unsigned long long>(candidates[i]), ok ? "true" : "false");
            if (ok && !renderMap) for (size_t b = 0; b < read; ++b) std::printf("%02X", bytes[b]);
            std::printf("\"");
            if (ok && renderMap) {
                std::printf(",\"instructions\":[");
                const bool decoded = PrintInstructions(bytes.data(), read, candidates[i]);
                std::printf("],\"decoded_entire_window\":%s", decoded ? "true" : "false");
            }
            std::printf("}");
        }
        std::printf("]");
        if (renderMap) {
            PrintCameraObservation(process.value, false);
            PrintCameraObservation(process.value, true);
        }
        std::puts("}");
        return allRead ? 0 : 4;
    } while (Process32NextW(snapshot.value, &entry));
    std::fputs("The specified retail New Vegas process is not running.\n", stderr);
    return 3;
}

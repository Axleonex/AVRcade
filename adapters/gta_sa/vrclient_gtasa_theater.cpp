#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11

#include <windows.h>
#include <bcrypt.h>
#include <d3d9.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "gtasa_vr_math.h"

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "user32.lib")

namespace {

constexpr uintptr_t NativeAddress(uintptr_t address) {
#ifdef VRCLIENT_GTASA_TESTS
  // Isolated fixtures must not collide with the test process's CRT heaps.
  return address + 0x30000000;
#else
  return address;
#endif
}

constexpr uint16_t kExpectedMachine = IMAGE_FILE_MACHINE_I386;
constexpr size_t kGtaCameraMatrixSize = 0x48;
constexpr unsigned int kPlayerHeadBone = 5;
constexpr unsigned int kRightUpperArmBone = 22;
constexpr unsigned int kRightForearmBone = 23;
constexpr unsigned int kRightHandBone = 24;
constexpr unsigned int kRightFingerBone = 25;
constexpr unsigned int kRightFingerTipBone = 26;
constexpr unsigned int kLeftUpperArmBone = 32;
constexpr unsigned int kLeftForearmBone = 33;
constexpr unsigned int kLeftHandBone = 34;
constexpr unsigned int kLeftFingerBone = 35;
constexpr unsigned int kLeftFingerTipBone = 36;
constexpr size_t kEntityRwObjectOffset = 0x18;
constexpr float kFirstPersonForwardOffset = 0.12f;
constexpr bool kCorrectHorizontalTracking = true;
constexpr bool kSwapSubmittedEyes = false;
constexpr float kCinematicPanelDistance = 2.20f;
constexpr float kCinematicPanelWidth = 1.80f;
constexpr char kExpectedNativeConfigSha256[] =
    "2c0de79194e8d6a1b6c3461bb60333dbfe799eb42e6d5e417bfe0f42b46369ab";

struct GtaSaNativeConfig {
  std::string gameSha256;
  uint32_t imageSize = 0;
  uintptr_t imageBase = 0;
  uintptr_t cameraAddress = 0;
  uintptr_t cameraMatrixOffset = 0;
  uintptr_t fovAddress = 0;
  uintptr_t aspectRatioAddress = 0;
  uintptr_t copyCameraMatrixToRwCam = 0;
  uintptr_t rwCameraOffset = 0;
  uintptr_t setRwViewWindow = 0;
  uintptr_t setRwViewOffset = 0;
  uintptr_t screenDimensions = 0;
  uintptr_t deriveCamera = 0;
  uintptr_t frontEndMenuActive = 0;
  uintptr_t fadeStatus = 0;
  uintptr_t cutsceneRunning = 0;
  uintptr_t cutsceneProcessing = 0;
  uintptr_t padUpdateCall = 0;
  uintptr_t frontendPadUpdateCall = 0;
  uintptr_t playerPad = 0;
  uintptr_t findPlayerPed = 0;
  uintptr_t setPlayerHeading = 0;
  uintptr_t findPlayerVehicle = 0;
  uintptr_t getBonePosition = 0;
  uintptr_t pedPreRender = 0;
  uintptr_t getAnimHierarchyFromClump = 0;
  uintptr_t rpHAnimIdGetIndex = 0;
  uintptr_t rpHAnimGetMatrixArray = 0;
  uintptr_t hudPlayerInfo = 0;
  uintptr_t hudWanted = 0;
  uintptr_t hudRadar = 0;
  uintptr_t hudVitalStats = 0;
  uintptr_t fontPrintString = 0;
  uintptr_t renderTail = 0;
  uintptr_t renderTailReturn = 0;
  uintptr_t cameraSize = 0;
  uintptr_t d3d9DelayThunk = 0;
  std::vector<uint8_t> d3d9DelayThunkExpected;
  std::vector<uint8_t> createDeviceExpected;
  std::vector<uint8_t> padUpdateCallExpected;
  std::vector<uint8_t> frontendPadUpdateCallExpected;
  std::vector<uint8_t> findPlayerPedExpected;
  std::vector<uint8_t> setPlayerHeadingExpected;
  std::vector<uint8_t> findPlayerVehicleExpected;
  std::vector<uint8_t> getBonePositionExpected;
  std::vector<uint8_t> pedPreRenderExpected;
  std::vector<uint8_t> getAnimHierarchyFromClumpExpected;
  std::vector<uint8_t> rpHAnimIdGetIndexExpected;
  std::vector<uint8_t> rpHAnimGetMatrixArrayExpected;
  std::vector<uint8_t> hudPlayerInfoExpected;
  std::vector<uint8_t> hudWantedExpected;
  std::vector<uint8_t> hudRadarExpected;
  std::vector<uint8_t> hudVitalStatsExpected;
  std::vector<uint8_t> fontPrintStringExpected;
  std::vector<uint8_t> renderTailExpected;
  std::vector<uint8_t> renderTailReturnExpected;
  std::vector<uint8_t> cameraSizeExpected;
};

GtaSaNativeConfig g_config;
bool g_configLoaded = false;

bool Sha256File(const char* path, std::string& digest);

HMODULE g_bridgeModule = nullptr;
std::atomic<bool> g_bridgeEnabled{false};
std::atomic<bool> g_shutdown{false};
std::mutex g_logMutex;

void Log(const char* format, ...) {
  char buffer[1024]{};
  va_list args;
  va_start(args, format);
  _vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
  va_end(args);

  std::string line = "[VRClient GTA SA] ";
  line += buffer;
  line += "\r\n";
  OutputDebugStringA(line.c_str());

  std::lock_guard lock(g_logMutex);
  char gamePath[MAX_PATH]{};
  const DWORD length = GetModuleFileNameA(nullptr, gamePath, sizeof(gamePath));
  if (length == 0 || length >= sizeof(gamePath)) return;
  std::string logPath(gamePath, length);
  const auto slash = logPath.find_last_of("\\/");
  logPath = (slash == std::string::npos ? std::string{} : logPath.substr(0, slash + 1)) +
      "vrclient_gtasa.log";
  HANDLE file = CreateFileA(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
  CloseHandle(file);
}

bool ReadTextFile(const char* path, std::string& text) {
  HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  bool ok = GetFileSizeEx(file, &size) && size.QuadPart >= 0 && size.QuadPart <= 64 * 1024;
  if (ok) {
    text.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    ok = text.empty() || ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
    ok = ok && read == text.size();
  }
  CloseHandle(file);
  return ok;
}

bool LocateJsonValue(const std::string& document, std::string_view key, size_t& valueStart) {
  const std::string needle = "\"" + std::string(key) + "\"";
  const size_t keyStart = document.find(needle);
  if (keyStart == std::string::npos || document.find(needle, keyStart + needle.size()) != std::string::npos)
    return false;
  size_t cursor = keyStart + needle.size();
  while (cursor < document.size() && std::isspace(static_cast<unsigned char>(document[cursor]))) ++cursor;
  if (cursor >= document.size() || document[cursor++] != ':') return false;
  while (cursor < document.size() && std::isspace(static_cast<unsigned char>(document[cursor]))) ++cursor;
  valueStart = cursor;
  return cursor < document.size();
}

bool ParseJsonString(const std::string& document, std::string_view key, std::string& value) {
  size_t cursor = 0;
  if (!LocateJsonValue(document, key, cursor) || document[cursor++] != '"') return false;
  value.clear();
  while (cursor < document.size()) {
    const char ch = document[cursor++];
    if (ch == '"') return true;
    if (ch == '\\') {
      if (cursor >= document.size()) return false;
      const char escaped = document[cursor++];
      if (escaped != '"' && escaped != '\\' && escaped != '/') return false;
      value.push_back(escaped);
    } else {
      value.push_back(ch);
    }
  }
  return false;
}

bool ParseJsonUnsigned(const std::string& document, std::string_view key, uint64_t& value) {
  size_t cursor = 0;
  if (!LocateJsonValue(document, key, cursor)) return false;
  std::string token;
  if (document[cursor] == '"') {
    if (!ParseJsonString(document, key, token)) return false;
  } else {
    const size_t start = cursor;
    while (cursor < document.size() && !std::isspace(static_cast<unsigned char>(document[cursor])) &&
           document[cursor] != ',' && document[cursor] != '}') ++cursor;
    token = document.substr(start, cursor - start);
  }
  if (token.empty()) return false;
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(token.c_str(), &end, 0);
  if (end == token.c_str() || *end != '\0') return false;
  value = parsed;
  return true;
}

int HexDigit(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

bool ParseHexBytes(const std::string& document, std::string_view key, std::vector<uint8_t>& bytes) {
  std::string text;
  if (!ParseJsonString(document, key, text)) return false;
  bytes.clear();
  size_t cursor = 0;
  while (cursor < text.size()) {
    while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor]))) ++cursor;
    if (cursor + 1 >= text.size()) return false;
    const int high = HexDigit(text[cursor++]);
    const int low = HexDigit(text[cursor++]);
    if (high < 0 || low < 0) return false;
    bytes.push_back(static_cast<uint8_t>((high << 4) | low));
    if (cursor < text.size() && !std::isspace(static_cast<unsigned char>(text[cursor]))) return false;
  }
  return !bytes.empty();
}

bool ParseJsonAddress(const std::string& document, std::string_view key, uintptr_t& value) {
  uint64_t parsed = 0;
  if (!ParseJsonUnsigned(document, key, parsed) ||
      parsed > static_cast<uint64_t>(std::numeric_limits<uintptr_t>::max())) return false;
  value = static_cast<uintptr_t>(parsed);
  return value != 0;
}

bool LoadGtaSaNativeConfig() {
  char path[MAX_PATH]{};
  const DWORD configuredLength = GetEnvironmentVariableA(
      "VRCLIENT_GTASA_HOOK_CONFIG", path, sizeof(path));
  if (configuredLength == 0 || configuredLength >= sizeof(path)) {
    if (g_bridgeModule == nullptr || GetModuleFileNameA(g_bridgeModule, path, sizeof(path)) == 0)
      return false;
    std::string adjacent(path);
    const auto slash = adjacent.find_last_of("\\/");
    adjacent = (slash == std::string::npos ? std::string{} : adjacent.substr(0, slash + 1)) +
        "vrclient_gtasa_theater.json";
    strncpy_s(path, adjacent.c_str(), _TRUNCATE);
  }

  std::string document;
  if (!ReadTextFile(path, document)) {
    Log("refusing GTA SA bridge startup: native hook profile is unreadable (%s)", path);
    return false;
  }
  std::string configDigest;
  if (!Sha256File(path, configDigest) ||
      _stricmp(configDigest.c_str(), kExpectedNativeConfigSha256) != 0) {
    Log("refusing GTA SA bridge startup: native hook profile SHA-256=%s expected=%s",
        configDigest.c_str(), kExpectedNativeConfigSha256);
    return false;
  }

  GtaSaNativeConfig parsed;
  uint64_t imageSize = 0;
  if (!ParseJsonString(document, "game_sha256", parsed.gameSha256) ||
      parsed.gameSha256.size() != 64 ||
      !ParseJsonUnsigned(document, "image_size", imageSize) || imageSize == 0 ||
      imageSize > std::numeric_limits<uint32_t>::max() ||
      !ParseJsonAddress(document, "image_base", parsed.imageBase) ||
      !ParseJsonAddress(document, "camera_address", parsed.cameraAddress) ||
      !ParseJsonAddress(document, "camera_matrix_offset", parsed.cameraMatrixOffset) ||
      !ParseJsonAddress(document, "fov_address", parsed.fovAddress) ||
      !ParseJsonAddress(document, "aspect_ratio_address", parsed.aspectRatioAddress) ||
      !ParseJsonAddress(document, "copy_camera_matrix_to_rw_cam", parsed.copyCameraMatrixToRwCam) ||
      !ParseJsonAddress(document, "rw_camera_offset", parsed.rwCameraOffset) ||
      !ParseJsonAddress(document, "set_rw_view_window", parsed.setRwViewWindow) ||
      !ParseJsonAddress(document, "set_rw_view_offset", parsed.setRwViewOffset) ||
      !ParseJsonAddress(document, "screen_dimensions", parsed.screenDimensions) ||
      !ParseJsonAddress(document, "derive_camera", parsed.deriveCamera) ||
      !ParseJsonAddress(document, "front_end_menu_active", parsed.frontEndMenuActive) ||
      !ParseJsonAddress(document, "fade_status", parsed.fadeStatus) ||
      !ParseJsonAddress(document, "cutscene_running", parsed.cutsceneRunning) ||
      !ParseJsonAddress(document, "cutscene_processing", parsed.cutsceneProcessing) ||
      !ParseJsonAddress(document, "pad_update_call", parsed.padUpdateCall) ||
      !ParseJsonAddress(document, "frontend_pad_update_call", parsed.frontendPadUpdateCall) ||
      !ParseJsonAddress(document, "player_pad", parsed.playerPad) ||
      !ParseJsonAddress(document, "find_player_ped", parsed.findPlayerPed) ||
      !ParseJsonAddress(document, "set_player_heading", parsed.setPlayerHeading) ||
      !ParseJsonAddress(document, "find_player_vehicle", parsed.findPlayerVehicle) ||
      !ParseJsonAddress(document, "get_bone_position", parsed.getBonePosition) ||
      !ParseJsonAddress(document, "ped_pre_render", parsed.pedPreRender) ||
      !ParseJsonAddress(document, "get_anim_hierarchy_from_clump", parsed.getAnimHierarchyFromClump) ||
      !ParseJsonAddress(document, "rp_hanim_id_get_index", parsed.rpHAnimIdGetIndex) ||
      !ParseJsonAddress(document, "rp_hanim_get_matrix_array", parsed.rpHAnimGetMatrixArray) ||
      !ParseJsonAddress(document, "hud_player_info", parsed.hudPlayerInfo) ||
      !ParseJsonAddress(document, "hud_wanted", parsed.hudWanted) ||
      !ParseJsonAddress(document, "hud_radar", parsed.hudRadar) ||
      !ParseJsonAddress(document, "hud_vital_stats", parsed.hudVitalStats) ||
      !ParseJsonAddress(document, "font_print_string", parsed.fontPrintString) ||
      !ParseJsonAddress(document, "render_tail", parsed.renderTail) ||
      !ParseJsonAddress(document, "render_tail_return", parsed.renderTailReturn) ||
      !ParseJsonAddress(document, "camera_size", parsed.cameraSize) ||
      !ParseJsonAddress(document, "d3d9_delay_thunk", parsed.d3d9DelayThunk) ||
      !ParseHexBytes(document, "d3d9_delay_thunk_expected", parsed.d3d9DelayThunkExpected) ||
      !ParseHexBytes(document, "create_device_expected", parsed.createDeviceExpected) ||
      !ParseHexBytes(document, "pad_update_call_expected", parsed.padUpdateCallExpected) ||
      !ParseHexBytes(document, "frontend_pad_update_call_expected", parsed.frontendPadUpdateCallExpected) ||
      !ParseHexBytes(document, "find_player_ped_expected", parsed.findPlayerPedExpected) ||
      !ParseHexBytes(document, "set_player_heading_expected", parsed.setPlayerHeadingExpected) ||
      !ParseHexBytes(document, "find_player_vehicle_expected", parsed.findPlayerVehicleExpected) ||
      !ParseHexBytes(document, "get_bone_position_expected", parsed.getBonePositionExpected) ||
      !ParseHexBytes(document, "ped_pre_render_expected", parsed.pedPreRenderExpected) ||
      !ParseHexBytes(document, "get_anim_hierarchy_from_clump_expected", parsed.getAnimHierarchyFromClumpExpected) ||
      !ParseHexBytes(document, "rp_hanim_id_get_index_expected", parsed.rpHAnimIdGetIndexExpected) ||
      !ParseHexBytes(document, "rp_hanim_get_matrix_array_expected", parsed.rpHAnimGetMatrixArrayExpected) ||
      !ParseHexBytes(document, "hud_player_info_expected", parsed.hudPlayerInfoExpected) ||
      !ParseHexBytes(document, "hud_wanted_expected", parsed.hudWantedExpected) ||
      !ParseHexBytes(document, "hud_radar_expected", parsed.hudRadarExpected) ||
      !ParseHexBytes(document, "hud_vital_stats_expected", parsed.hudVitalStatsExpected) ||
      !ParseHexBytes(document, "font_print_string_expected", parsed.fontPrintStringExpected) ||
      !ParseHexBytes(document, "render_tail_expected", parsed.renderTailExpected) ||
      !ParseHexBytes(document, "render_tail_return_expected", parsed.renderTailReturnExpected) ||
      !ParseHexBytes(document, "camera_size_expected", parsed.cameraSizeExpected)) {
    Log("refusing GTA SA bridge startup: native hook profile is malformed (%s)", path);
    return false;
  }
  parsed.imageSize = static_cast<uint32_t>(imageSize);
  g_config = std::move(parsed);
  g_configLoaded = true;
  Log("loaded external GTA SA native hook profile=%s", path);
  return true;
}

std::string HexDigest(const uint8_t* bytes, ULONG size) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.resize(size * 2);
  for (ULONG i = 0; i < size; ++i) {
    result[i * 2] = digits[bytes[i] >> 4u];
    result[i * 2 + 1] = digits[bytes[i] & 0x0fu];
  }
  return result;
}

bool Sha256File(const char* path, std::string& digest) {
  HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;

  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD objectLength = 0;
  DWORD resultLength = 0;
  DWORD hashLength = 0;
  bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0;
  ok = ok && BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                               reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
                               &resultLength, 0) == 0;
  ok = ok && BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                               reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength),
                               &resultLength, 0) == 0;
  std::vector<uint8_t> object(objectLength);
  std::vector<uint8_t> output(hashLength);
  ok = ok && BCryptCreateHash(algorithm, &hash, object.data(), objectLength,
                              nullptr, 0, 0) == 0;

  std::vector<uint8_t> input(1024 * 1024);
  while (ok) {
    DWORD read = 0;
    if (!ReadFile(file, input.data(), static_cast<DWORD>(input.size()), &read, nullptr)) {
      ok = false;
      break;
    }
    if (read == 0) break;
    ok = BCryptHashData(hash, input.data(), read, 0) == 0;
  }
  ok = ok && BCryptFinishHash(hash, output.data(), hashLength, 0) == 0;
  if (ok) digest = HexDigest(output.data(), hashLength);
  if (hash != nullptr) BCryptDestroyHash(hash);
  if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
  CloseHandle(file);
  return ok;
}

bool ImageRangeIsValid(uintptr_t address, size_t size, uint32_t mappedImageSize) {
  if (g_config.imageBase == 0 || mappedImageSize == 0 || address < g_config.imageBase)
    return false;
  const uintptr_t offset = address - g_config.imageBase;
  return offset <= mappedImageSize && size <= mappedImageSize - offset;
}

bool IsExpectedGameImage() {
  auto* image = reinterpret_cast<uint8_t*>(GetModuleHandleA(nullptr));
  if (image == nullptr || reinterpret_cast<uintptr_t>(image) != g_config.imageBase) {
    Log("refusing unsupported gta_sa.exe load base; expected 0x%08Ix", g_config.imageBase);
    return false;
  }
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
    Log("refusing gta_sa.exe with an invalid DOS header");
    return false;
  }
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != kExpectedMachine ||
      nt->OptionalHeader.ImageBase != g_config.imageBase) {
    Log("refusing gta_sa.exe with an unexpected PE header");
    return false;
  }
  const uint32_t mappedImageSize = nt->OptionalHeader.SizeOfImage;
  if (mappedImageSize == 0) {
    Log("refusing gta_sa.exe with an empty mapped image");
    return false;
  }

  char gamePath[MAX_PATH]{};
  const DWORD length = GetModuleFileNameA(nullptr, gamePath, sizeof(gamePath));
  if (length == 0 || length >= sizeof(gamePath)) {
    Log("failed to resolve the game executable path");
    return false;
  }

  HANDLE file = CreateFileA(gamePath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    Log("failed to open the game executable for identity verification");
    return false;
  }
  LARGE_INTEGER size{};
  const bool sizeOk = GetFileSizeEx(file, &size);
  CloseHandle(file);
  if (!sizeOk || size.QuadPart != g_config.imageSize) {
    Log("refusing unsupported executable size=%lld expected=%u", size.QuadPart,
        g_config.imageSize);
    return false;
  }

  if (g_config.cameraMatrixOffset > std::numeric_limits<uintptr_t>::max() - g_config.cameraAddress ||
      g_config.rwCameraOffset > std::numeric_limits<uintptr_t>::max() - g_config.cameraAddress ||
      !ImageRangeIsValid(g_config.cameraAddress + g_config.cameraMatrixOffset,
                         kGtaCameraMatrixSize, mappedImageSize) ||
      !ImageRangeIsValid(g_config.cameraAddress + g_config.rwCameraOffset,
                         sizeof(void*), mappedImageSize) ||
      !ImageRangeIsValid(g_config.fovAddress, sizeof(float), mappedImageSize) ||
      !ImageRangeIsValid(g_config.aspectRatioAddress, sizeof(float), mappedImageSize) ||
      !ImageRangeIsValid(g_config.copyCameraMatrixToRwCam, 1, mappedImageSize) ||
      !ImageRangeIsValid(g_config.setRwViewWindow, 1, mappedImageSize) ||
      !ImageRangeIsValid(g_config.setRwViewOffset, 1, mappedImageSize) ||
      !ImageRangeIsValid(g_config.screenDimensions, sizeof(int32_t) * 2, mappedImageSize) ||
      !ImageRangeIsValid(g_config.deriveCamera, 1, mappedImageSize) ||
      !ImageRangeIsValid(g_config.frontEndMenuActive, sizeof(uint8_t), mappedImageSize) ||
      !ImageRangeIsValid(g_config.fadeStatus, 1, mappedImageSize) ||
      !ImageRangeIsValid(g_config.cutsceneRunning, sizeof(uint8_t), mappedImageSize) ||
      !ImageRangeIsValid(g_config.cutsceneProcessing, sizeof(uint8_t), mappedImageSize) ||
      !ImageRangeIsValid(g_config.padUpdateCall, g_config.padUpdateCallExpected.size(), mappedImageSize) ||
      !ImageRangeIsValid(g_config.frontendPadUpdateCall,
                         g_config.frontendPadUpdateCallExpected.size(), mappedImageSize) ||
      !ImageRangeIsValid(g_config.playerPad, sizeof(gtasa_vr::GtaPadState), mappedImageSize) ||
      !ImageRangeIsValid(g_config.findPlayerPed, g_config.findPlayerPedExpected.size(), mappedImageSize) ||
      !ImageRangeIsValid(g_config.setPlayerHeading,
                         g_config.setPlayerHeadingExpected.size(), mappedImageSize) ||
      !ImageRangeIsValid(g_config.findPlayerVehicle, g_config.findPlayerVehicleExpected.size(), mappedImageSize) ||
      !ImageRangeIsValid(g_config.getBonePosition, g_config.getBonePositionExpected.size(), mappedImageSize) ||
      !ImageRangeIsValid(g_config.pedPreRender, g_config.pedPreRenderExpected.size(), mappedImageSize) ||
      !ImageRangeIsValid(g_config.getAnimHierarchyFromClump,
                         g_config.getAnimHierarchyFromClumpExpected.size(), mappedImageSize) ||
      !ImageRangeIsValid(g_config.rpHAnimIdGetIndex,
                         g_config.rpHAnimIdGetIndexExpected.size(), mappedImageSize) ||
      !ImageRangeIsValid(g_config.rpHAnimGetMatrixArray,
                         g_config.rpHAnimGetMatrixArrayExpected.size(), mappedImageSize) ||
      !ImageRangeIsValid(g_config.hudPlayerInfo, g_config.hudPlayerInfoExpected.size(), mappedImageSize) ||
      !ImageRangeIsValid(g_config.hudWanted, g_config.hudWantedExpected.size(), mappedImageSize) ||
      !ImageRangeIsValid(g_config.hudRadar, g_config.hudRadarExpected.size(), mappedImageSize) ||
      !ImageRangeIsValid(g_config.hudVitalStats, g_config.hudVitalStatsExpected.size(), mappedImageSize) ||
      !ImageRangeIsValid(g_config.fontPrintString, g_config.fontPrintStringExpected.size(), mappedImageSize) ||
      !ImageRangeIsValid(g_config.d3d9DelayThunk, g_config.d3d9DelayThunkExpected.size(), mappedImageSize) ||
      !ImageRangeIsValid(g_config.renderTail, g_config.renderTailExpected.size(), mappedImageSize) ||
      !ImageRangeIsValid(g_config.renderTailReturn, g_config.renderTailReturnExpected.size(), mappedImageSize) ||
      !ImageRangeIsValid(g_config.cameraSize, g_config.cameraSizeExpected.size(), mappedImageSize)) {
    Log("refusing native hook profile with an address outside the pinned PE image");
    return false;
  }

  if (std::memcmp(reinterpret_cast<const void*>(g_config.findPlayerPed),
                  g_config.findPlayerPedExpected.data(),
                  g_config.findPlayerPedExpected.size()) != 0 ||
      std::memcmp(reinterpret_cast<const void*>(g_config.setPlayerHeading),
                  g_config.setPlayerHeadingExpected.data(),
                  g_config.setPlayerHeadingExpected.size()) != 0 ||
      std::memcmp(reinterpret_cast<const void*>(g_config.findPlayerVehicle),
                  g_config.findPlayerVehicleExpected.data(),
                  g_config.findPlayerVehicleExpected.size()) != 0 ||
      std::memcmp(reinterpret_cast<const void*>(g_config.getBonePosition),
                  g_config.getBonePositionExpected.data(),
                  g_config.getBonePositionExpected.size()) != 0 ||
      std::memcmp(reinterpret_cast<const void*>(g_config.pedPreRender),
                  g_config.pedPreRenderExpected.data(),
                  g_config.pedPreRenderExpected.size()) != 0 ||
      std::memcmp(reinterpret_cast<const void*>(g_config.getAnimHierarchyFromClump),
                  g_config.getAnimHierarchyFromClumpExpected.data(),
                  g_config.getAnimHierarchyFromClumpExpected.size()) != 0 ||
      std::memcmp(reinterpret_cast<const void*>(g_config.rpHAnimIdGetIndex),
                  g_config.rpHAnimIdGetIndexExpected.data(),
                  g_config.rpHAnimIdGetIndexExpected.size()) != 0 ||
      std::memcmp(reinterpret_cast<const void*>(g_config.rpHAnimGetMatrixArray),
                  g_config.rpHAnimGetMatrixArrayExpected.data(),
                  g_config.rpHAnimGetMatrixArrayExpected.size()) != 0 ||
      std::memcmp(reinterpret_cast<const void*>(g_config.padUpdateCall),
                  g_config.padUpdateCallExpected.data(),
                  g_config.padUpdateCallExpected.size()) != 0 ||
      std::memcmp(reinterpret_cast<const void*>(g_config.frontendPadUpdateCall),
                  g_config.frontendPadUpdateCallExpected.data(),
                  g_config.frontendPadUpdateCallExpected.size()) != 0) {
    Log("refusing first-person/arm bridge: player skeleton signatures do not match the pinned executable");
    return false;
  }

  std::string digest;
  if (!Sha256File(gamePath, digest) || _stricmp(digest.c_str(), g_config.gameSha256.c_str()) != 0) {
    Log("refusing unsupported gta_sa.exe SHA-256=%s expected=%s", digest.c_str(),
        g_config.gameSha256.c_str());
    return false;
  }
  Log("verified gta_sa.exe x86 identity SHA-256=%s", digest.c_str());
  return true;
}

template <typename T>
T ReadPointer(const uint8_t* address) {
  T value{};
  std::memcpy(&value, address, sizeof(value));
  return value;
}

using Vec3 = gtasa_vr::Vec3;
using Quaternion = gtasa_vr::Quaternion;

struct GtaCameraMatrix {
  Vec3 right;
  uint32_t rightFlags = 0;
  Vec3 forward;
  uint32_t forwardPad = 0;
  Vec3 up;
  uint32_t upPad = 0;
  Vec3 position;
  uint32_t positionPad = 0;
  void* matrixPtr = nullptr;
  uint32_t haveRwMatrix = 0;
};

struct RwMatrixNative {
  Vec3 right{}; uint32_t flags{};
  Vec3 up{}; uint32_t pad1{};
  Vec3 at{}; uint32_t pad2{};
  Vec3 position{}; uint32_t pad3{};
};
static_assert(sizeof(RwMatrixNative) == 0x40);

struct TrackedHandPose {
  bool valid = false;
  Vec3 position{};
  Quaternion orientation{};
};

struct ArmFrameSnapshot {
  bool valid = false;
  gtasa_vr::Basis leftHand{};
  gtasa_vr::Basis rightHand{};
  XrTime displayTime = 0;
  uint64_t serial = 0;
};

void RotateRwBasis(RwMatrixNative& matrix, Quaternion rotation) {
  matrix.right = gtasa_vr::Rotate(rotation, matrix.right);
  matrix.up = gtasa_vr::Rotate(rotation, matrix.up);
  matrix.at = gtasa_vr::Rotate(rotation, matrix.at);
}

void SetRwBasis(RwMatrixNative& matrix, const gtasa_vr::Basis& basis) {
  matrix.right = basis.right;
  // RenderWare names the GTA Y/forward vector `up` and the GTA Z/up vector `at`.
  matrix.up = basis.forward;
  matrix.at = basis.up;
}

gtasa_vr::Basis GetRwBasis(const RwMatrixNative& matrix) {
  return {matrix.right, matrix.up, matrix.at, matrix.position};
}

void SetRwMatrix(RwMatrixNative& matrix, const gtasa_vr::Basis& basis) {
  SetRwBasis(matrix, basis);
  matrix.position = basis.position;
}

static_assert(sizeof(GtaCameraMatrix) == kGtaCameraMatrixSize);
static_assert(offsetof(GtaCameraMatrix, right) == 0x00);
static_assert(offsetof(GtaCameraMatrix, forward) == 0x10);
static_assert(offsetof(GtaCameraMatrix, up) == 0x20);
static_assert(offsetof(GtaCameraMatrix, position) == 0x30);
static_assert(offsetof(GtaCameraMatrix, matrixPtr) == 0x40);
static_assert(offsetof(GtaCameraMatrix, haveRwMatrix) == 0x44);

using VoidFn = void(__cdecl*)();
using CopyCameraMatrixFn = void(__thiscall*)(void*, bool);


CopyCameraMatrixFn g_copyCameraMatrixToRwCam =
    nullptr;
void* g_renderTailTrampoline = nullptr;
void* g_cameraSizeTrampoline = nullptr;
void* g_pedPreRenderTrampoline = nullptr;
IDirect3DDevice9* g_d3d9Device = nullptr;

bool g_stereoHooksInstalled = false;
bool g_stereoRequested = false;
bool g_inputRequested = false;
bool g_firstPersonRequested = true;
gtasa_vr::TurnMode g_turnMode = gtasa_vr::TurnMode::Snap;
float g_snapTurnDegrees = 30.0f;
float g_smoothTurnDegreesPerSecond = 90.0f;
std::atomic<bool> g_renderTailObserved{false};

void ConfigureTurnMode() {
  char mode[16]{};
  const DWORD modeLength=GetEnvironmentVariableA(
      "VRCLIENT_GTASA_TURN_MODE",mode,sizeof(mode));
  g_turnMode=modeLength>0 && modeLength<sizeof(mode) &&
      _stricmp(mode,"smooth")==0
      ? gtasa_vr::TurnMode::Smooth : gtasa_vr::TurnMode::Snap;
  char speed[24]{};
  const DWORD speedLength=GetEnvironmentVariableA(
      "VRCLIENT_GTASA_SMOOTH_TURN_DPS",speed,sizeof(speed));
  g_smoothTurnDegreesPerSecond=90.0f;
  if (speedLength>0 && speedLength<sizeof(speed)) {
    char* end=nullptr;
    const float parsed=std::strtof(speed,&end);
    if (end!=speed && *end=='\0' && std::isfinite(parsed) &&
        parsed>=30.0f && parsed<=180.0f)
      g_smoothTurnDegreesPerSecond=parsed;
  }
  char angle[24]{};
  const DWORD angleLength=GetEnvironmentVariableA(
      "VRCLIENT_GTASA_SNAP_TURN_DEGREES",angle,sizeof(angle));
  g_snapTurnDegrees=30.0f;
  if (angleLength>0 && angleLength<sizeof(angle)) {
    char* end=nullptr;
    const float parsed=std::strtof(angle,&end);
    if (end!=angle && *end=='\0' && std::isfinite(parsed) &&
        parsed>=15.0f && parsed<=90.0f)
      g_snapTurnDegrees=parsed;
  }
  Log("input trace: VR turn mode=%s snap angle=%.1f degrees smooth speed=%.1f degrees/second",
      g_turnMode==gtasa_vr::TurnMode::Smooth ? "smooth" : "snap",
      g_snapTurnDegrees,g_smoothTurnDegreesPerSecond);
}

bool IsStereoRequested() {
  char value[8]{};
  const DWORD length = GetEnvironmentVariableA("VRCLIENT_GTASA_STEREO", value, sizeof(value));
  return length > 0 && length < sizeof(value) &&
      (_stricmp(value, "1") == 0 || _stricmp(value, "true") == 0 ||
       _stricmp(value, "yes") == 0);
}

bool IsInputRequested() {
  char value[8]{};
  const DWORD length = GetEnvironmentVariableA("VRCLIENT_GTASA_INPUT", value, sizeof(value));
  return length > 0 && length < sizeof(value) &&
      (_stricmp(value, "1") == 0 || _stricmp(value, "true") == 0 ||
       _stricmp(value, "yes") == 0);
}

bool IsFirstPersonRequested() {
  char value[16]{};
  const DWORD length = GetEnvironmentVariableA("VRCLIENT_GTASA_FIRST_PERSON", value, sizeof(value));
  if (length == 0) return true;
  return length < sizeof(value) &&
      !(_stricmp(value, "0") == 0 || _stricmp(value, "off") == 0 ||
        _stricmp(value, "false") == 0 || _stricmp(value, "no") == 0);
}

bool IsBridgeDisabled() {
  char value[8]{};
  const DWORD length = GetEnvironmentVariableA("VRCLIENT_GTASA_DISABLE", value, sizeof(value));
  return length > 0 && length < sizeof(value) &&
      (_stricmp(value, "1") == 0 || _stricmp(value, "true") == 0 ||
       _stricmp(value, "yes") == 0);
}

bool IsStereoSeamHookDisabled() {
  char value[16]{};
  const DWORD length = GetEnvironmentVariableA("VRCLIENT_GTASA_SEAMS", value, sizeof(value));
  return length > 0 && length < sizeof(value) &&
      (_stricmp(value, "0") == 0 || _stricmp(value, "off") == 0 ||
       _stricmp(value, "none") == 0 || _stricmp(value, "false") == 0);
}

template <typename T>
bool PatchPointer(T* slot, T replacement, T& original) {
  DWORD oldProtect = 0;
  if (!VirtualProtect(slot, sizeof(T), PAGE_READWRITE, &oldProtect)) return false;
  original = *slot;
  *slot = replacement;
  FlushInstructionCache(GetCurrentProcess(), slot, sizeof(T));
  DWORD ignored = 0;
  VirtualProtect(slot, sizeof(T), oldProtect, &ignored);
  return true;
}

bool InstallCallHook(uintptr_t address,const std::vector<uint8_t>& expected,
                     void* replacement,VoidFn& original) {
  if (expected.size()!=5||expected[0]!=0xE8||replacement==nullptr) return false;
  auto* call=reinterpret_cast<uint8_t*>(address);
  if (std::memcmp(call,expected.data(),expected.size())!=0) {
    Log("refusing pad-update call hook at 0x%08Ix: executable bytes did not match",address);
    return false;
  }
  int32_t oldDisplacement=0;
  std::memcpy(&oldDisplacement,call+1,sizeof(oldDisplacement));
  original=reinterpret_cast<VoidFn>(address+5+oldDisplacement);
  const intptr_t replacementDelta=static_cast<intptr_t>(reinterpret_cast<uintptr_t>(replacement))-
      static_cast<intptr_t>(address+5);
  if (replacementDelta<INT32_MIN||replacementDelta>INT32_MAX) return false;
  DWORD oldProtect=0;
  if (!VirtualProtect(call,expected.size(),PAGE_EXECUTE_READWRITE,&oldProtect)) return false;
  const int32_t newDisplacement=static_cast<int32_t>(replacementDelta);
  std::memcpy(call+1,&newDisplacement,sizeof(newDisplacement));
  FlushInstructionCache(GetCurrentProcess(),call,expected.size());
  DWORD ignored=0;
  VirtualProtect(call,expected.size(),oldProtect,&ignored);
  return true;
}

bool InstallInlineHook(uintptr_t address, const uint8_t* expected, size_t expectedSize,
                       void* replacement, void** trampoline) {
  if (expected == nullptr || expectedSize < 5 || replacement == nullptr || trampoline == nullptr)
    return false;

  auto* target = reinterpret_cast<uint8_t*>(address);
  if (std::memcmp(target, expected, expectedSize) != 0) {
    Log("refusing inline hook at 0x%08Ix: executable bytes did not match", address);
    return false;
  }

  const size_t trampolineSize = expectedSize + 5;
  auto* bridge = reinterpret_cast<uint8_t*>(VirtualAlloc(
      nullptr, trampolineSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  if (bridge == nullptr) {
    Log("VirtualAlloc failed for inline hook at 0x%08Ix", address);
    return false;
  }
  std::memcpy(bridge, target, expectedSize);
  for (size_t index = 0; index + 5 <= expectedSize; ++index) {
    if (bridge[index] != 0xE8 && bridge[index] != 0xE9) continue;
    int32_t displacement = 0;
    std::memcpy(&displacement, bridge + index + 1, sizeof(displacement));
    const uintptr_t originalDestination = address + index + 5 + displacement;
    const intptr_t relocatedDelta = static_cast<intptr_t>(originalDestination) -
        static_cast<intptr_t>(reinterpret_cast<uintptr_t>(bridge + index + 5));
    if (relocatedDelta < INT32_MIN || relocatedDelta > INT32_MAX) {
      VirtualFree(bridge, 0, MEM_RELEASE);
      Log("inline hook relative branch could not be relocated at 0x%08Ix", address + index);
      return false;
    }
    displacement = static_cast<int32_t>(relocatedDelta);
    std::memcpy(bridge + index + 1, &displacement, sizeof(displacement));
  }

  const uintptr_t trampolineReturn = reinterpret_cast<uintptr_t>(target + expectedSize);
  const intptr_t trampolineDelta = static_cast<intptr_t>(trampolineReturn) -
      static_cast<intptr_t>(reinterpret_cast<uintptr_t>(bridge + expectedSize + 5));
  if (trampolineDelta < INT32_MIN || trampolineDelta > INT32_MAX) {
    VirtualFree(bridge, 0, MEM_RELEASE);
    Log("inline hook trampoline is out of x86 relative-jump range at 0x%08Ix", address);
    return false;
  }
  bridge[expectedSize] = 0xE9;
  const auto trampolineDisplacement = static_cast<int32_t>(trampolineDelta);
  std::memcpy(bridge + expectedSize + 1, &trampolineDisplacement, sizeof(trampolineDisplacement));

  const uintptr_t replacementAddress = reinterpret_cast<uintptr_t>(replacement);
  const intptr_t replacementDelta = static_cast<intptr_t>(replacementAddress) -
      static_cast<intptr_t>(address + 5);
  if (replacementDelta < INT32_MIN || replacementDelta > INT32_MAX) {
    VirtualFree(bridge, 0, MEM_RELEASE);
    Log("inline hook replacement is out of x86 relative-jump range at 0x%08Ix", address);
    return false;
  }

  DWORD oldProtect = 0;
  if (!VirtualProtect(target, expectedSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
    VirtualFree(bridge, 0, MEM_RELEASE);
    Log("VirtualProtect failed for inline hook at 0x%08Ix", address);
    return false;
  }
  // Publish and flush the callable original before exposing the replacement.
  *trampoline = bridge;
  FlushInstructionCache(GetCurrentProcess(), bridge, trampolineSize);
  target[0] = 0xE9;
  const auto replacementDisplacement = static_cast<int32_t>(replacementDelta);
  std::memcpy(target + 1, &replacementDisplacement, sizeof(replacementDisplacement));
  for (size_t index = 5; index < expectedSize; ++index) target[index] = 0x90;
  FlushInstructionCache(GetCurrentProcess(), target, expectedSize);
  DWORD ignored = 0;
  VirtualProtect(target, expectedSize, oldProtect, &ignored);
  *trampoline = bridge;
  return true;
}

bool RemoveInlineHook(uintptr_t address, const uint8_t* expected, size_t expectedSize,
                      void** trampoline) {
  if (expected == nullptr || expectedSize < 5 || trampoline == nullptr || *trampoline == nullptr)
    return true;

  auto* target = reinterpret_cast<uint8_t*>(address);
  if (target[0] != 0xE9) {
    Log("refusing inline-hook rollback at 0x%08Ix: target no longer contains our jump", address);
    return false;
  }
  DWORD oldProtect = 0;
  if (!VirtualProtect(target, expectedSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
    Log("VirtualProtect failed during inline-hook rollback at 0x%08Ix", address);
    return false;
  }
  std::memcpy(target, expected, expectedSize);
  FlushInstructionCache(GetCurrentProcess(), target, expectedSize);
  DWORD ignored = 0;
  VirtualProtect(target, expectedSize, oldProtect, &ignored);
  VirtualFree(*trampoline, 0, MEM_RELEASE);
  *trampoline = nullptr;
  return true;
}

using Direct3DCreate9Fn = IDirect3D9*(WINAPI*)(UINT);
using CreateDeviceFn = HRESULT(STDMETHODCALLTYPE*)(
    IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
using CreateDeviceExFn = HRESULT(STDMETHODCALLTYPE*)(
    IDirect3D9Ex*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*,
    D3DDISPLAYMODEEX*, IDirect3DDevice9Ex**);
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(
    IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using ResetFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using DrawPrimitiveUpFn = HRESULT(STDMETHODCALLTYPE*)(
    IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
using DrawIndexedPrimitiveUpFn = HRESULT(STDMETHODCALLTYPE*)(
    IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void*, D3DFORMAT,
    const void*, UINT);

Direct3DCreate9Fn g_realDirect3DCreate9 = nullptr;
void* g_direct3DCreate9Trampoline = nullptr;
CreateDeviceFn g_realCreateDevice = nullptr;
void* g_createDeviceImplementationTrampoline = nullptr;
CreateDeviceExFn g_realCreateDeviceEx = nullptr;
PresentFn g_realPresent = nullptr;
ResetFn g_realReset = nullptr;
DrawPrimitiveUpFn g_realDrawPrimitiveUp = nullptr;
DrawIndexedPrimitiveUpFn g_realDrawIndexedPrimitiveUp = nullptr;
std::atomic<bool> g_d3d9Hooked{false};
std::atomic<bool> g_d3d9ImportHooked{false};
std::atomic<bool> g_deviceHooked{false};
std::atomic<bool> g_createDeviceObserved{false};
std::atomic<bool> g_presentObserved{false};
std::atomic<bool> g_firstStereoFrameTrace{false};
bool g_traceStereoFrame = false;
bool g_vrHudDrawHooksInstalled = false;
bool g_vrHudFunctionHooksInstalled = false;
thread_local gtasa_vr::HudCluster g_vrHudCluster = gtasa_vr::HudCluster::None;
void* g_hudPlayerInfoTrampoline = nullptr;
void* g_hudWantedTrampoline = nullptr;
void* g_hudRadarTrampoline = nullptr;
void* g_hudVitalStatsTrampoline = nullptr;
void* g_fontPrintStringTrampoline = nullptr;

class OpenXrTheater {
 public:
  bool Submit(IDirect3DDevice9* device);
  bool EnsureInitialized(IDirect3DDevice9* device);
  bool BeginStereoFrame(IDirect3DDevice9* device);
  bool EndStereoFrame(IDirect3DDevice9* device);
  bool RenderCompleteFrame(IDirect3DDevice9* device, VoidFn renderTail);
  bool IsNativeTailFallback() const { return nativeTailFallback_; }
  void CaptureRenderedEye(IDirect3DDevice9* device);
  void ApplyRwProjection(void* camera);
  void AfterPedPreRender(void* ped);
  void RestoreArmOverride();
  void ApplyNativePadInput(gtasa_vr::GtaPadState& state);
  void ApplyPendingBodyTurn();
  bool IsStereoFrameActive() const { return stereoFrameActive_; }
  void Shutdown();

#ifdef VRCLIENT_GTASA_TESTS
 public:
#else
 private:
#endif
  bool Initialize(IDirect3DDevice9* device);
  bool LoadFunctions();
  bool LoadInstanceFunctions();
  bool PollEvents();
  void HandleSessionLoss(XrSessionState state);
  void HandleInstanceLoss(XrTime lossTime);
  bool StartSessionIfReady();
  bool Upload(IDirect3DSurface9* source, const D3DSURFACE_DESC& desc);
  bool Render();
  bool RenderProjection();
  bool LocateViews(XrTime displayTime);
  bool InitializeInput();
  void PollInput(XrTime turnSampleTime=0);
  void ReleaseInput();
  void ReleasePressedInputs();
  void SetKey(WORD key, bool pressed, bool& state);
  void SetMouseButton(DWORD downFlag, DWORD upFlag, bool pressed, bool& state);
  void SetButtonAction(XrAction action, bool& previous, WORD key);
  void SetFloatAction(XrAction action, bool& previous, DWORD buttonFlag, float threshold);
  void SetFloatKeyAction(XrAction action, bool& previous, WORD key, float threshold);
  bool ReadBooleanAction(XrAction action);
  float ReadFloatAction(XrAction action);
  XrVector2f ReadVectorAction(XrAction action);
  void ObserveInputAction(bool active);
  void ClearTrackedHands();
  void SampleTrackedHands();
  void BuildArmFrameSnapshot();
  bool ApplyEye(IDirect3DDevice9* device, uint32_t eye);
  float CalculateEyeFov(const XrFovf& fov) const;
  float CalculateEyeAspectRatio(const XrFovf& fov) const;
  void CaptureBaseCamera();
  bool CaptureFirstPersonBasis();
  void RestoreGameState(IDirect3DDevice9* device);
  bool UploadPixels(uint32_t width, uint32_t height, const uint8_t* pixels);
  bool Check(XrResult result, const char* operation);
  void ReleaseGraphics();
  void ReleaseOpenXrResources(bool skipSessionTermination = false);
  bool FailInitialization();

  HMODULE loader_ = nullptr;
  PFN_xrGetInstanceProcAddr getInstanceProcAddr_ = nullptr;
  PFN_xrEnumerateInstanceExtensionProperties enumerateInstanceExtensionProperties_ = nullptr;
  PFN_xrCreateInstance createInstance_ = nullptr;
  PFN_xrGetSystem getSystem_ = nullptr;
  PFN_xrPollEvent pollEvent_ = nullptr;
  PFN_xrDestroyInstance destroyInstance_ = nullptr;
  PFN_xrGetD3D11GraphicsRequirementsKHR getD3D11GraphicsRequirements_ = nullptr;
  PFN_xrCreateSession createSession_ = nullptr;
  PFN_xrDestroySession destroySession_ = nullptr;
  PFN_xrBeginSession beginSession_ = nullptr;
  PFN_xrEndSession endSession_ = nullptr;
  PFN_xrCreateReferenceSpace createReferenceSpace_ = nullptr;
  PFN_xrCreateActionSpace createActionSpace_ = nullptr;
  PFN_xrDestroySpace destroySpace_ = nullptr;
  PFN_xrLocateSpace locateSpace_ = nullptr;
  PFN_xrLocateViews locateViews_ = nullptr;
  PFN_xrEnumerateViewConfigurationViews enumerateViewConfigurationViews_ = nullptr;
  PFN_xrEnumerateSwapchainFormats enumerateSwapchainFormats_ = nullptr;
  PFN_xrCreateSwapchain createSwapchain_ = nullptr;
  PFN_xrDestroySwapchain destroySwapchain_ = nullptr;
  PFN_xrEnumerateSwapchainImages enumerateSwapchainImages_ = nullptr;
  PFN_xrAcquireSwapchainImage acquireSwapchainImage_ = nullptr;
  PFN_xrWaitSwapchainImage waitSwapchainImage_ = nullptr;
  PFN_xrReleaseSwapchainImage releaseSwapchainImage_ = nullptr;
  PFN_xrWaitFrame waitFrame_ = nullptr;
  PFN_xrBeginFrame beginFrame_ = nullptr;
  PFN_xrEndFrame endFrame_ = nullptr;
  PFN_xrStringToPath stringToPath_ = nullptr;
  PFN_xrCreateActionSet createActionSet_ = nullptr;
  PFN_xrDestroyActionSet destroyActionSet_ = nullptr;
  PFN_xrCreateAction createAction_ = nullptr;
  PFN_xrDestroyAction destroyAction_ = nullptr;
  PFN_xrSuggestInteractionProfileBindings suggestInteractionProfileBindings_ = nullptr;
  PFN_xrAttachSessionActionSets attachSessionActionSets_ = nullptr;
  PFN_xrSyncActions syncActions_ = nullptr;
  PFN_xrGetActionStateBoolean getActionStateBoolean_ = nullptr;
  PFN_xrGetActionStateFloat getActionStateFloat_ = nullptr;
  PFN_xrGetActionStateVector2f getActionStateVector2f_ = nullptr;
  PFN_xrGetActionStatePose getActionStatePose_ = nullptr;

  XrInstance instance_ = XR_NULL_HANDLE;
  XrSystemId system_ = XR_NULL_SYSTEM_ID;
  XrSession session_ = XR_NULL_HANDLE;
  XrSpace viewSpace_ = XR_NULL_HANDLE;
  XrSpace localSpace_ = XR_NULL_HANDLE;
  XrSwapchain swapchain_ = XR_NULL_HANDLE;
  XrActionSet inputActionSet_ = XR_NULL_HANDLE;
  XrAction leftMoveAction_ = XR_NULL_HANDLE;
  XrAction rightLookAction_ = XR_NULL_HANDLE;
  XrAction leftTriggerAction_ = XR_NULL_HANDLE;
  XrAction rightTriggerAction_ = XR_NULL_HANDLE;
  XrAction leftGripAction_ = XR_NULL_HANDLE;
  XrAction rightGripAction_ = XR_NULL_HANDLE;
  XrAction buttonAAction_ = XR_NULL_HANDLE;
  XrAction buttonBAction_ = XR_NULL_HANDLE;
  XrAction buttonXAction_ = XR_NULL_HANDLE;
  XrAction buttonYAction_ = XR_NULL_HANDLE;
  XrAction leftMenuAction_ = XR_NULL_HANDLE;
  XrAction leftHandPoseAction_ = XR_NULL_HANDLE;
  XrAction rightHandPoseAction_ = XR_NULL_HANDLE;
  XrSpace leftHandSpace_ = XR_NULL_HANDLE;
  XrSpace rightHandSpace_ = XR_NULL_HANDLE;
  XrSessionState sessionState_ = XR_SESSION_STATE_UNKNOWN;
  bool sessionRunning_ = false;
  bool initialized_ = false;
  bool initializationAttempted_ = false;
  ULONGLONG nextInitializationAttemptMs_ = 0;
  bool frameBegun_ = false;
  XrFrameState frameState_{XR_TYPE_FRAME_STATE};
  bool stereoFrameActive_ = false;
  bool stereoRendered_ = false;
  bool nativeTailFallback_ = false;
  bool cinematicModeActive_ = false;
  uint32_t currentEye_ = 0;
  bool eyeCaptured_[2]{};
  gtasa_vr::Frustum eyeFrustums_[2]{};
  std::vector<uint8_t> stereoPixels_;
  void* rwCamera_ = nullptr;
  XrVector2f baseViewWindow_{}, baseViewOffset_{};
  bool baseCameraCaptured_ = false;
  bool baseFovCaptured_ = false;
  bool baseAspectRatioCaptured_ = false;
  bool viewportSaved_ = false;
  D3DVIEWPORT9 savedViewport_{};
  GtaCameraMatrix baseCamera_{};
  gtasa_vr::Basis vrCameraBasis_{};
  gtasa_vr::SnapTurnState snapTurn_{};
  XrTime lastTurnSampleTime_ = 0;
  bool smoothTurnObservedLogged_ = false;
  bool smoothBodyTurnLogged_ = false;
  float baseFov_ = 0.0f;
  float baseAspectRatio_ = 0.0f;
  std::vector<XrView> views_;
  XrViewState viewState_{XR_TYPE_VIEW_STATE};
  Quaternion referenceOrientation_{};
  Vec3 referenceHeadPosition_{};
  bool referencePoseCaptured_ = false;
  bool firstPersonHeadAnchorObserved_ = false;
  bool firstPersonHeadAnchorUnavailableObserved_ = false;
  TrackedHandPose trackedHands_[2]{};
  ArmFrameSnapshot armFrame_{};
  uint64_t armFrameSerial_ = 0;
  bool armOverrideActive_ = false;
  void* armOverridePed_ = nullptr;
  RwMatrixNative* armOverrideMatrices_[10]{};
  RwMatrixNative armOverrideOriginal_[10]{};
  bool armGripUnavailableLogged_ = false;
  bool armGripAvailableLogged_ = false;
  bool armSnapshotLogged_ = false;
  bool armPlayerAttemptLogged_ = false;
  bool armPlayerHierarchyLogged_ = false;
  bool armSolveFailureLogged_ = false;
  bool armOverrideAppliedLogged_ = false;
  bool handForwardRollLogged_[2]{};
  bool armReachLogged_[2]{};
  bool inputEnabled_ = false;
  bool inputAttached_ = false;
  bool inputSyncObserved_ = false;
  bool inputActionActiveObserved_ = false;
  bool inputGateStateKnown_ = false;
  bool inputGateAllowed_ = false;
  bool moveVectorObservedLogged_ = false;
  bool lateralVectorObservedLogged_ = false;
  bool lookVectorObservedLogged_ = false;
  bool lookDispatchFailureLogged_ = false;
  bool nativePadDispatchLogged_ = false;
  bool moveAlignedLogged_ = false;
  bool inputMenuMode_ = false;
  bool inputMenuModeKnown_ = false;
  bool headRelativeMoveAllowed_ = false;
  XrVector2f rawMove_{};
  bool keyW_ = false;
  bool keyA_ = false;
  bool keyS_ = false;
  bool keyD_ = false;
  bool keyShift_ = false;
  bool keySpace_ = false;
  bool keyF_ = false;
  bool keyR_ = false;
  bool keyEnter_ = false;
  bool keyEscape_ = false;
  bool mouseAim_ = false;
  bool mouseFire_ = false;
  gtasa_vr::GtaPadState nativePadState_{};
  bool nativePadActive_ = false;
  bool mouseAltFire_ = false;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  uint32_t gameWidth_ = 0;
  uint32_t gameHeight_ = 0;
  uint32_t gameEyeWidth_ = 0;
  uint32_t captureWidth_ = 0;
  uint32_t captureHeight_ = 0;
  uint32_t uploadWidth_ = 0;
  uint32_t uploadHeight_ = 0;
  int64_t swapchainFormat_ = 0;

  ID3D11Device* d3d11Device_ = nullptr;
  ID3D11DeviceContext* d3d11Context_ = nullptr;
  ID3D11Texture2D* uploadTexture_ = nullptr;
  ID3D11ShaderResourceView* uploadView_ = nullptr;
  ID3D11VertexShader* vertexShader_ = nullptr;
  ID3D11PixelShader* pixelShader_ = nullptr;
  ID3D11SamplerState* sampler_ = nullptr;
  ID3D11Buffer* blitConstants_ = nullptr;
  std::vector<XrSwapchainImageD3D11KHR> images_;
  std::vector<ID3D11RenderTargetView*> renderTargets_;
  std::vector<uint8_t> convertedPixels_;
  D3DFORMAT sourceFormat_ = D3DFMT_UNKNOWN;
  IDirect3DDevice9* captureDevice_ = nullptr;
  IDirect3DSurface9* resolveSurface_ = nullptr;
  IDirect3DSurface9* captureSurface_ = nullptr;
  std::mutex mutex_;
};

OpenXrTheater g_theater;
VoidFn g_realPadUpdate = nullptr;

void __cdecl HookedPadUpdate() {
  // Preserve GTA/GInput's complete pad update first, then layer only active VR
  // controls into CPad::NewState before the game simulates this frame.
  if (g_realPadUpdate != nullptr) g_realPadUpdate();
  if (!g_bridgeEnabled.load() || g_shutdown.load() || !g_inputRequested) return;
  auto* playerPad = reinterpret_cast<gtasa_vr::GtaPadState*>(g_config.playerPad);
  g_theater.ApplyPendingBodyTurn();
  g_theater.ApplyNativePadInput(*playerPad);
}

bool InstallPadUpdateHook() {
  if (!InstallCallHook(g_config.padUpdateCall, g_config.padUpdateCallExpected,
                       reinterpret_cast<void*>(&HookedPadUpdate), g_realPadUpdate)) {
    Log("native CPad update hook installation failed; controller input is disabled");
    return false;
  }
  VoidFn frontendOriginal = nullptr;
  if (!InstallCallHook(g_config.frontendPadUpdateCall, g_config.frontendPadUpdateCallExpected,
                       reinterpret_cast<void*>(&HookedPadUpdate), frontendOriginal) ||
      frontendOriginal != g_realPadUpdate) {
    Log("frontend CPad update hook installation failed or had a different original target");
    return false;
  }
  Log("native CPad update hook installed at 0x%08Ix (original target=%p)",
      g_config.padUpdateCall, reinterpret_cast<void*>(g_realPadUpdate));
  Log("frontend CPad update hook installed at 0x%08Ix", g_config.frontendPadUpdateCall);
  return true;
}

HRESULT STDMETHODCALLTYPE HookedCreateDevice(
    IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
HRESULT STDMETHODCALLTYPE HookedCreateDeviceEx(
    IDirect3D9Ex*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*,
    IDirect3DDevice9Ex**);
HRESULT STDMETHODCALLTYPE HookedReset(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
HRESULT STDMETHODCALLTYPE HookedPresent(IDirect3DDevice9*, const RECT*, const RECT*, HWND,
                                         const RGNDATA*);
HRESULT STDMETHODCALLTYPE HookedDrawPrimitiveUp(
    IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
HRESULT STDMETHODCALLTYPE HookedDrawIndexedPrimitiveUp(
    IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void*, D3DFORMAT,
    const void*, UINT);
bool InstallStereoHooks();
bool InstallVrHudHooks();

template <typename T>
bool LoadInstanceFunction(PFN_xrGetInstanceProcAddr getProc, XrInstance instance,
                          const char* name, T& function) {
  PFN_xrVoidFunction raw = nullptr;
  const XrResult result = getProc(instance, name, &raw);
  if (result != XR_SUCCESS || raw == nullptr) {
    Log("OpenXR function unavailable: %s result=%d", name, static_cast<int>(result));
    return false;
  }
  function = reinterpret_cast<T>(raw);
  return true;
}

bool OpenXrTheater::Check(XrResult result, const char* operation) {
  if (result == XR_SUCCESS) return true;
  if (result == XR_ERROR_API_VERSION_UNSUPPORTED)
    Log("%s failed: OpenXR API version unsupported by the selected runtime", operation);
  if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE)
    Log("%s failed: no OpenXR HMD form factor is currently available", operation);
  if (result == XR_ERROR_RUNTIME_UNAVAILABLE)
    Log("%s failed: OpenXR runtime unavailable; headset/runtime connection is required",
        operation);
  Log("%s failed: XrResult=%d", operation, static_cast<int>(result));
  return false;
}

bool OpenXrTheater::LoadFunctions() {
  char configured[MAX_PATH]{};
  const DWORD configuredLength = GetEnvironmentVariableA(
      "VRCLIENT_GTASA_OPENXR_LOADER", configured, sizeof(configured));
  if (configuredLength > 0 && configuredLength < sizeof(configured)) {
    Log("OpenXR x86 loader override=%s", configured);
    loader_ = LoadLibraryA(configured);
  }

  char runtimeManifest[MAX_PATH]{};
  const DWORD runtimeLength = GetEnvironmentVariableA(
      "XR_RUNTIME_JSON", runtimeManifest, sizeof(runtimeManifest));
  if (runtimeLength > 0 && runtimeLength < sizeof(runtimeManifest))
    Log("OpenXR runtime manifest override=%s", runtimeManifest);

  if (loader_ == nullptr && g_bridgeModule != nullptr) {
    char bridgePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(g_bridgeModule, bridgePath, sizeof(bridgePath));
    if (length > 0 && length < sizeof(bridgePath)) {
      std::string adjacent(bridgePath, length);
      const auto slash = adjacent.find_last_of("\\/");
      adjacent = (slash == std::string::npos ? std::string{} : adjacent.substr(0, slash + 1)) +
          "openxr_loader.dll";
      loader_ = LoadLibraryA(adjacent.c_str());
    }
  }
  if (loader_ == nullptr) loader_ = LoadLibraryA("openxr_loader.dll");
  if (loader_ == nullptr) {
    Log("OpenXR x86 loader not found; place openxr_loader.dll beside the ASI or set VRCLIENT_GTASA_OPENXR_LOADER");
    return false;
  }

  getInstanceProcAddr_ = reinterpret_cast<PFN_xrGetInstanceProcAddr>(
      GetProcAddress(loader_, "xrGetInstanceProcAddr"));
  if (getInstanceProcAddr_ == nullptr) {
    Log("OpenXR loader does not export xrGetInstanceProcAddr");
    return false;
  }
  return LoadInstanceFunction(getInstanceProcAddr_, XR_NULL_HANDLE,
                              "xrEnumerateInstanceExtensionProperties",
                              enumerateInstanceExtensionProperties_) &&
      LoadInstanceFunction(getInstanceProcAddr_, XR_NULL_HANDLE, "xrCreateInstance",
                           createInstance_);
}

bool OpenXrTheater::LoadInstanceFunctions() {
  return
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrGetSystem", getSystem_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrPollEvent", pollEvent_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrDestroyInstance", destroyInstance_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrCreateSession", createSession_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrDestroySession", destroySession_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrEnumerateViewConfigurationViews",
                           enumerateViewConfigurationViews_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrCreateReferenceSpace",
                           createReferenceSpace_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrCreateActionSpace",
                           createActionSpace_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrDestroySpace", destroySpace_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrLocateSpace", locateSpace_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrEnumerateSwapchainFormats",
                           enumerateSwapchainFormats_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrCreateSwapchain", createSwapchain_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrDestroySwapchain", destroySwapchain_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrEnumerateSwapchainImages",
                           enumerateSwapchainImages_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrAcquireSwapchainImage",
                           acquireSwapchainImage_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrWaitSwapchainImage",
                           waitSwapchainImage_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrReleaseSwapchainImage",
                           releaseSwapchainImage_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrWaitFrame", waitFrame_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrBeginFrame", beginFrame_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrEndFrame", endFrame_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrBeginSession", beginSession_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrEndSession", endSession_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrStringToPath", stringToPath_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrCreateActionSet", createActionSet_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrDestroyActionSet", destroyActionSet_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrCreateAction", createAction_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrDestroyAction", destroyAction_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrSuggestInteractionProfileBindings",
                           suggestInteractionProfileBindings_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrAttachSessionActionSets",
                           attachSessionActionSets_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrSyncActions", syncActions_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrGetActionStateBoolean",
                           getActionStateBoolean_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrGetActionStateFloat",
                           getActionStateFloat_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrGetActionStateVector2f",
                           getActionStateVector2f_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrGetActionStatePose",
                           getActionStatePose_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrGetD3D11GraphicsRequirementsKHR",
                           getD3D11GraphicsRequirements_) &&
      LoadInstanceFunction(getInstanceProcAddr_, instance_, "xrLocateViews", locateViews_);
}

bool OpenXrTheater::Initialize(IDirect3DDevice9* device) {
  std::lock_guard lock(mutex_);
  if (initialized_) return true;
  if (initializationAttempted_ && GetTickCount64() < nextInitializationAttemptMs_)
    return false;
  initializationAttempted_ = true;
  Log("OpenXR initialization started");
  if (device == nullptr || !LoadFunctions()) return FailInitialization();

  if (g_stereoRequested) {
    IDirect3DSurface9* backBuffer = nullptr;
    D3DSURFACE_DESC backBufferDesc{};
    if (FAILED(device->GetRenderTarget(0, &backBuffer)) || backBuffer == nullptr ||
        FAILED(backBuffer->GetDesc(&backBufferDesc)) || backBufferDesc.Width < 2 ||
        backBufferDesc.Height == 0) {
      if (backBuffer != nullptr) backBuffer->Release();
      Log("stereo mode requires a readable D3D9 backbuffer at least two pixels wide");
      return FailInitialization();
    }
    backBuffer->Release();
    Log("D3D9 stereo source backbuffer=%ux%u format=%u multisample=%u quality=%lu",
        backBufferDesc.Width, backBufferDesc.Height, static_cast<unsigned>(backBufferDesc.Format),
        static_cast<unsigned>(backBufferDesc.MultiSampleType),
        static_cast<unsigned long>(backBufferDesc.MultiSampleQuality));
    gameWidth_ = backBufferDesc.Width;
    gameHeight_ = backBufferDesc.Height;
    gameEyeWidth_ = gameWidth_; // each eye renders a complete native backbuffer
    if (gameEyeWidth_ == 0) {
      Log("stereo mode rejected an invalid D3D9 backbuffer width=%u", gameWidth_);
      return FailInitialization();
    }
  }

  uint32_t extensionCount = 0;
  if (!Check(enumerateInstanceExtensionProperties_(nullptr, 0, &extensionCount, nullptr),
             "xrEnumerateInstanceExtensionProperties(count)")) return FailInitialization();
  std::vector<XrExtensionProperties> extensions(extensionCount);
  for (auto& extension : extensions) extension.type = XR_TYPE_EXTENSION_PROPERTIES;
  if (!Check(enumerateInstanceExtensionProperties_(nullptr, extensionCount, &extensionCount,
                                                   extensions.data()),
             "xrEnumerateInstanceExtensionProperties")) return FailInitialization();
  const bool d3d11Extension = std::any_of(
      extensions.begin(), extensions.end(), [](const XrExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, XR_KHR_D3D11_ENABLE_EXTENSION_NAME) == 0;
      });
  if (!d3d11Extension) {
    Log("OpenXR runtime does not expose XR_KHR_D3D11_enable");
    return FailInitialization();
  }

  const char* enabledExtensions[] = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
  XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
  strncpy_s(createInfo.applicationInfo.applicationName, "VRClient GTA SA Theater", _TRUNCATE);
  strncpy_s(createInfo.applicationInfo.engineName, "VRClient", _TRUNCATE);
  createInfo.applicationInfo.applicationVersion = 1;
  createInfo.applicationInfo.engineVersion = 1;
  // The bridge is built against the newest OpenXR headers, but the installed
  // Virtual Desktop runtime is an older 32-bit implementation. Request the
  // stable 1.0 baseline instead of the 1.1.43 header default, which the
  // runtime rejects with XR_ERROR_API_VERSION_UNSUPPORTED (-4).
  createInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
  createInfo.enabledExtensionCount = 1;
  createInfo.enabledExtensionNames = enabledExtensions;
  if (!Check(createInstance_(&createInfo, &instance_), "xrCreateInstance"))
    return FailInitialization();
  if (!LoadInstanceFunctions()) return FailInitialization();
  XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
  systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  if (!Check(getSystem_(instance_, &systemInfo, &system_),
             "xrGetSystem")) return FailInitialization();

  XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
  if (!Check(getD3D11GraphicsRequirements_(instance_, system_, &requirements),
             "xrGetD3D11GraphicsRequirementsKHR")) return FailInitialization();

  IDXGIFactory1* factory = nullptr;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    Log("CreateDXGIFactory1 failed");
    return FailInitialization();
  }
  IDXGIAdapter* selectedAdapter = nullptr;
  for (UINT index = 0;; ++index) {
    IDXGIAdapter* adapter = nullptr;
    if (factory->EnumAdapters(index, &adapter) == DXGI_ERROR_NOT_FOUND) break;
    DXGI_ADAPTER_DESC description{};
    if (SUCCEEDED(adapter->GetDesc(&description)) &&
        description.AdapterLuid.LowPart == requirements.adapterLuid.LowPart &&
        description.AdapterLuid.HighPart == requirements.adapterLuid.HighPart) {
      selectedAdapter = adapter;
      break;
    }
    adapter->Release();
  }
  if (selectedAdapter == nullptr) {
    factory->Release();
    Log("OpenXR adapter LUID was not found in DXGI");
    return FailInitialization();
  }
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  D3D_FEATURE_LEVEL selectedLevel{};
  if (FAILED(D3D11CreateDevice(selectedAdapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, levels,
                               ARRAYSIZE(levels), D3D11_SDK_VERSION, &d3d11Device_,
                               &selectedLevel, &d3d11Context_))) {
    selectedAdapter->Release();
    factory->Release();
    Log("D3D11CreateDevice failed for the OpenXR adapter");
    return FailInitialization();
  }
  selectedAdapter->Release();
  factory->Release();

  XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
  binding.device = d3d11Device_;
  XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
  sessionInfo.next = &binding;
  sessionInfo.systemId = system_;
  if (!Check(createSession_(instance_, &sessionInfo, &session_), "xrCreateSession"))
    return FailInitialization();
  if (g_inputRequested && !InitializeInput())
    Log("OpenXR input bindings unavailable; continuing with physical keyboard/mouse input");

  XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
  spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
  spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
  if (!Check(createReferenceSpace_(session_, &spaceInfo, &viewSpace_),
             "xrCreateReferenceSpace(VIEW)")) return FailInitialization();
  if (g_stereoRequested) {
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    if (!Check(createReferenceSpace_(session_, &spaceInfo, &localSpace_),
               "xrCreateReferenceSpace(LOCAL)")) return FailInitialization();
  }

  uint32_t viewCount = 0;
  if (!Check(enumerateViewConfigurationViews_(
                 instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount,
                 nullptr),
             "xrEnumerateViewConfigurationViews(count)")) return FailInitialization();
  std::vector<XrViewConfigurationView> views(viewCount);
  for (auto& view : views) view.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
  if (!Check(enumerateViewConfigurationViews_(
                 instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount,
                 &viewCount, views.data()),
             "xrEnumerateViewConfigurationViews")) return FailInitialization();
  if (g_stereoRequested && viewCount != 2) {
    Log("stereo mode requires two OpenXR views; runtime reported %u", viewCount);
    return FailInitialization();
  }
  if (g_stereoRequested) {
    views_.resize(2);
    for (auto& view : views_) view.type = XR_TYPE_VIEW;
  }
  if (!views.empty()) {
    width_ = std::clamp(views.front().recommendedImageRectWidth, 1024u, 2048u);
    height_ = std::clamp(views.front().recommendedImageRectHeight, 576u, 2048u);
  } else {
    width_ = 1280;
    height_ = 720;
  }

  uint32_t formatCount = 0;
  if (!Check(enumerateSwapchainFormats_(session_, 0, &formatCount, nullptr),
             "xrEnumerateSwapchainFormats(count)")) return FailInitialization();
  std::vector<int64_t> formats(formatCount);
  if (!Check(enumerateSwapchainFormats_(session_, formatCount, &formatCount, formats.data()),
             "xrEnumerateSwapchainFormats")) return FailInitialization();
  const int64_t preferred[] = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM};
  for (const int64_t candidate : preferred) {
    if (std::find(formats.begin(), formats.end(), candidate) != formats.end()) {
      swapchainFormat_ = candidate;
      break;
    }
  }
  if (swapchainFormat_ == 0) {
    Log("OpenXR D3D11 swapchain has no supported 32-bit color format");
    return FailInitialization();
  }

  XrSwapchainCreateInfo swapchainInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
  swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
  swapchainInfo.format = swapchainFormat_;
  swapchainInfo.sampleCount = 1;
  swapchainInfo.width = width_;
  swapchainInfo.height = height_;
  swapchainInfo.faceCount = 1;
  swapchainInfo.arraySize = g_stereoRequested ? 2 : 1;
  swapchainInfo.mipCount = 1;
  if (!Check(createSwapchain_(session_, &swapchainInfo, &swapchain_), "xrCreateSwapchain"))
    return FailInitialization();

  uint32_t imageCount = 0;
  if (!Check(enumerateSwapchainImages_(swapchain_, 0, &imageCount, nullptr),
             "xrEnumerateSwapchainImages(count)")) return FailInitialization();
  images_.resize(imageCount);
  for (auto& image : images_) image.type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
  if (!Check(enumerateSwapchainImages_(
                 swapchain_, imageCount, &imageCount,
                 reinterpret_cast<XrSwapchainImageBaseHeader*>(images_.data())),
             "xrEnumerateSwapchainImages")) return FailInitialization();
  for (const auto& image : images_) {
    if (!g_stereoRequested) {
      ID3D11RenderTargetView* target = nullptr;
      const HRESULT result = d3d11Device_->CreateRenderTargetView(image.texture, nullptr, &target);
      if (FAILED(result)) {
        Log("CreateRenderTargetView(mono) failed: HRESULT=0x%08lX",
            static_cast<unsigned long>(result));
        return FailInitialization();
      }
      renderTargets_.push_back(target);
      continue;
    }
    D3D11_TEXTURE2D_DESC textureDescription{};
    image.texture->GetDesc(&textureDescription);
    for (UINT eye = 0; eye < 2; ++eye) {
      D3D11_RENDER_TARGET_VIEW_DESC targetDescription{};
      targetDescription.Format = static_cast<DXGI_FORMAT>(swapchainFormat_);
      targetDescription.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
      targetDescription.Texture2DArray.MipSlice = 0;
      targetDescription.Texture2DArray.FirstArraySlice = eye;
      targetDescription.Texture2DArray.ArraySize = 1;
      ID3D11RenderTargetView* target = nullptr;
      const HRESULT result = d3d11Device_->CreateRenderTargetView(
          image.texture, &targetDescription, &target);
      if (FAILED(result)) {
        Log("CreateRenderTargetView(stereo eye=%u) failed: "
            "HRESULT=0x%08lX textureFormat=%u arraySize=%u mipLevels=%u",
            eye, static_cast<unsigned long>(result),
            static_cast<unsigned>(textureDescription.Format), textureDescription.ArraySize,
            textureDescription.MipLevels);
        return FailInitialization();
      }
      renderTargets_.push_back(target);
    }
  }

  const char* vertexShaderSource = R"(
struct VSOut { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut main(uint id : SV_VertexID) {
  float2 p[4] = { float2(-1, -1), float2(-1, 1), float2(1, -1), float2(1, 1) };
  float2 t[4] = { float2(0, 1), float2(0, 0), float2(1, 1), float2(1, 0) };
  VSOut output; output.position = float4(p[id], 0, 1); output.uv = t[id]; return output;
})";
  const char* pixelShaderSource = R"(
Texture2D sourceTexture : register(t0);
SamplerState sourceSampler : register(s0);
cbuffer BlitConstants : register(b0) { float4 sourceRect; }
float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
  return sourceTexture.Sample(sourceSampler,
                              float2(sourceRect.x + uv.x * sourceRect.z,
                                     sourceRect.y + uv.y * sourceRect.w));
})";
  using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*,
                                        ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**,
                                        ID3DBlob**);
  HMODULE compiler = LoadLibraryA("d3dcompiler_47.dll");
  auto compile = compiler == nullptr
      ? nullptr
      : reinterpret_cast<D3DCompileFn>(GetProcAddress(compiler, "D3DCompile"));
  if (compile == nullptr) {
    if (compiler != nullptr) FreeLibrary(compiler);
    Log("d3dcompiler_47.dll is required for the theater blit shader");
    return FailInitialization();
  }
  ID3DBlob* vertexBytecode = nullptr;
  ID3DBlob* pixelBytecode = nullptr;
  const HRESULT vertexResult = compile(vertexShaderSource, std::strlen(vertexShaderSource),
                                       "gtasa_theater_vs", nullptr, nullptr, "main", "vs_4_0",
                                       0, 0, &vertexBytecode, nullptr);
  const HRESULT pixelResult = compile(pixelShaderSource, std::strlen(pixelShaderSource),
                                      "gtasa_theater_ps", nullptr, nullptr, "main", "ps_4_0",
                                      0, 0, &pixelBytecode, nullptr);
  // ID3DBlob methods live in the compiler DLL. Keep it loaded until every
  // blob has been consumed and released, including all failure paths.
  if (FAILED(vertexResult) || FAILED(pixelResult) ||
      vertexBytecode == nullptr || pixelBytecode == nullptr ||
      FAILED(d3d11Device_->CreateVertexShader(vertexBytecode->GetBufferPointer(),
                                               vertexBytecode->GetBufferSize(), nullptr,
                                               &vertexShader_)) ||
      FAILED(d3d11Device_->CreatePixelShader(pixelBytecode->GetBufferPointer(),
                                              pixelBytecode->GetBufferSize(), nullptr,
                                              &pixelShader_))) {
    if (vertexBytecode != nullptr) vertexBytecode->Release();
    if (pixelBytecode != nullptr) pixelBytecode->Release();
    FreeLibrary(compiler);
    Log("failed to compile or create the theater blit shader");
    return FailInitialization();
  }
  vertexBytecode->Release();
  pixelBytecode->Release();
  FreeLibrary(compiler);

  D3D11_SAMPLER_DESC samplerDescription{};
  samplerDescription.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
  samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  const HRESULT samplerResult =
      d3d11Device_->CreateSamplerState(&samplerDescription, &sampler_);
  if (FAILED(samplerResult)) {
    Log("CreateSamplerState failed: HRESULT=0x%08lX",
        static_cast<unsigned long>(samplerResult));
    return FailInitialization();
  }
  D3D11_BUFFER_DESC bufferDescription{};
  bufferDescription.ByteWidth = sizeof(float) * 4;
  bufferDescription.Usage = D3D11_USAGE_DEFAULT;
  bufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  const HRESULT bufferResult =
      d3d11Device_->CreateBuffer(&bufferDescription, nullptr, &blitConstants_);
  if (FAILED(bufferResult)) {
    Log("CreateBuffer(blit constants) failed: HRESULT=0x%08lX",
        static_cast<unsigned long>(bufferResult));
    return FailInitialization();
  }

  initialized_ = true;
  nextInitializationAttemptMs_ = 0;
  Log("OpenXR bridge initialized at %ux%u (%s)", width_, height_,
      g_stereoRequested ? "x86 stereo scene path" : "monoscopic theater");
  return true;
}

bool OpenXrTheater::EnsureInitialized(IDirect3DDevice9* device) {
  return Initialize(device);
}

bool OpenXrTheater::PollEvents() {
  while (true) {
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    const XrResult result = pollEvent_(instance_, &event);
    if (result == XR_EVENT_UNAVAILABLE)
      return true;
    if (result == XR_ERROR_INSTANCE_LOST) {
      Log("xrPollEvent reported XR_ERROR_INSTANCE_LOST");
      HandleInstanceLoss(0);
      return false;
    }
    if (result != XR_SUCCESS) {
      Log("xrPollEvent failed: XrResult=%d", static_cast<int>(result));
      return false;
    }

    switch (event.type) {
      case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
        const auto& loss = *reinterpret_cast<const XrEventDataInstanceLossPending*>(&event);
        Log("OpenXR instance loss pending at XrTime=%lld",
            static_cast<long long>(loss.lossTime));
        HandleInstanceLoss(loss.lossTime);
        return false;
      }
      case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
        const auto& changed = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
        sessionState_ = changed.state;
        if (sessionState_ != XR_SESSION_STATE_FOCUSED) ReleasePressedInputs();
        if (sessionState_ == XR_SESSION_STATE_STOPPING ||
            sessionState_ == XR_SESSION_STATE_EXITING ||
            sessionState_ == XR_SESSION_STATE_LOSS_PENDING) {
          HandleSessionLoss(sessionState_);
          return false;
        }
        break;
      }
      default:
        break;
    }
  }
}

void OpenXrTheater::HandleSessionLoss(XrSessionState state) {
  const char* stateName = state == XR_SESSION_STATE_STOPPING
      ? "STOPPING"
      : state == XR_SESSION_STATE_LOSS_PENDING ? "LOSS_PENDING" : "EXITING";
  Log("OpenXR session entered %s; releasing XR resources and scheduling recovery", stateName);
  ReleaseOpenXrResources();
  initializationAttempted_ = true;
  constexpr ULONGLONG retryDelayMs = 2000;
  nextInitializationAttemptMs_ = GetTickCount64() + retryDelayMs;
}

void OpenXrTheater::HandleInstanceLoss(XrTime lossTime) {
  Log("OpenXR instance loss; releasing XR resources and scheduling recovery (XrTime=%lld)",
      static_cast<long long>(lossTime));
  ReleaseOpenXrResources(true);
  initializationAttempted_ = true;
  constexpr ULONGLONG retryDelayMs = 2000;
  nextInitializationAttemptMs_ = GetTickCount64() + retryDelayMs;
}

bool OpenXrTheater::StartSessionIfReady() {
  if (sessionRunning_) return true;
  if (sessionState_ != XR_SESSION_STATE_READY) return false;
  XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
  beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
  if (!Check(beginSession_(session_, &beginInfo), "xrBeginSession")) return false;
  sessionRunning_ = true;
  return true;
}

bool OpenXrTheater::LocateViews(XrTime displayTime) {
  if (!g_stereoRequested || localSpace_ == XR_NULL_HANDLE || locateViews_ == nullptr ||
      views_.size() != 2) {
    return false;
  }
  XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
  locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
  locateInfo.displayTime = displayTime;
  locateInfo.space = localSpace_;
  viewState_ = XrViewState{XR_TYPE_VIEW_STATE};
  uint32_t viewCount = 0;
  if (!Check(locateViews_(session_, &locateInfo, &viewState_, static_cast<uint32_t>(views_.size()),
                         &viewCount, views_.data()),
             "xrLocateViews"))
    return false;
  if (viewCount != 2 ||
      (viewState_.viewStateFlags & (XR_VIEW_STATE_ORIENTATION_VALID_BIT |
                                   XR_VIEW_STATE_POSITION_VALID_BIT)) !=
          (XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT)) {
    Log("OpenXR did not provide two valid stereo poses (count=%u flags=0x%llx)", viewCount,
        static_cast<unsigned long long>(viewState_.viewStateFlags));
    return false;
  }

  for (uint32_t eye = 0; eye < 2; ++eye) {
    const auto& pose = views_[eye].pose;
    const auto& fov = views_[eye].fov;
    const float norm = pose.orientation.x*pose.orientation.x + pose.orientation.y*pose.orientation.y +
        pose.orientation.z*pose.orientation.z + pose.orientation.w*pose.orientation.w;
    if (!std::isfinite(norm) || std::abs(norm - 1.0f) > 0.01f ||
        !std::isfinite(pose.position.x) || !std::isfinite(pose.position.y) ||
        !std::isfinite(pose.position.z) ||
        !gtasa_vr::MakeTrackedFrustum(
            fov.angleLeft, fov.angleRight, fov.angleUp, fov.angleDown,
            kCorrectHorizontalTracking,eyeFrustums_[eye])) return false;
  }
  const Quaternion currentOrientation{
      views_[0].pose.orientation.x, views_[0].pose.orientation.y,
      views_[0].pose.orientation.z, views_[0].pose.orientation.w};
  const Vec3 currentHeadPosition{
      (views_[0].pose.position.x + views_[1].pose.position.x) * 0.5f,
      (views_[0].pose.position.y + views_[1].pose.position.y) * 0.5f,
      (views_[0].pose.position.z + views_[1].pose.position.z) * 0.5f};
  if (!referencePoseCaptured_) {
    referenceOrientation_ = currentOrientation;
    referenceHeadPosition_ = currentHeadPosition;
    referencePoseCaptured_ = true;
  }
  return true;
}

bool OpenXrTheater::InitializeInput() {
  if (!g_inputRequested || inputEnabled_) return inputEnabled_;
  if (inputActionSet_ != XR_NULL_HANDLE) return inputAttached_;
  if (stringToPath_ == nullptr || createActionSet_ == nullptr || createAction_ == nullptr ||
      createActionSpace_ == nullptr || suggestInteractionProfileBindings_ == nullptr ||
      attachSessionActionSets_ == nullptr) {
    return false;
  }

  XrActionSetCreateInfo actionSetInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
  strncpy_s(actionSetInfo.actionSetName, "gtasa_vr", _TRUNCATE);
  strncpy_s(actionSetInfo.localizedActionSetName, "GTA San Andreas VR", _TRUNCATE);
  actionSetInfo.priority = 0;
  if (!Check(createActionSet_(instance_, &actionSetInfo, &inputActionSet_),
             "xrCreateActionSet(gtasa_vr)")) {
    return false;
  }

  auto createAction = [&](const char* name, const char* localized, XrActionType type,
                          XrAction& action) {
    XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
    actionInfo.actionType = type;
    strncpy_s(actionInfo.actionName, name, _TRUNCATE);
    strncpy_s(actionInfo.localizedActionName, localized, _TRUNCATE);
    return Check(createAction_(inputActionSet_, &actionInfo, &action), name);
  };
  if (!createAction("left_move", "Move", XR_ACTION_TYPE_VECTOR2F_INPUT, leftMoveAction_) ||
      !createAction("right_look", "Look", XR_ACTION_TYPE_VECTOR2F_INPUT, rightLookAction_) ||
      !createAction("left_trigger", "Left trigger", XR_ACTION_TYPE_FLOAT_INPUT,
                    leftTriggerAction_) ||
      !createAction("right_trigger", "Right trigger", XR_ACTION_TYPE_FLOAT_INPUT,
                    rightTriggerAction_) ||
      !createAction("left_grip", "Left grip", XR_ACTION_TYPE_FLOAT_INPUT, leftGripAction_) ||
      !createAction("right_grip", "Right grip", XR_ACTION_TYPE_FLOAT_INPUT, rightGripAction_) ||
      !createAction("button_a", "A", XR_ACTION_TYPE_BOOLEAN_INPUT, buttonAAction_) ||
      !createAction("button_b", "B", XR_ACTION_TYPE_BOOLEAN_INPUT, buttonBAction_) ||
      !createAction("button_x", "X", XR_ACTION_TYPE_BOOLEAN_INPUT, buttonXAction_) ||
      !createAction("button_y", "Y", XR_ACTION_TYPE_BOOLEAN_INPUT, buttonYAction_) ||
      !createAction("left_menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT, leftMenuAction_) ||
      !createAction("left_hand_pose", "Left hand pose", XR_ACTION_TYPE_POSE_INPUT,
                    leftHandPoseAction_) ||
      !createAction("right_hand_pose", "Right hand pose", XR_ACTION_TYPE_POSE_INPUT,
                    rightHandPoseAction_)) {
    ReleaseInput();
    return false;
  }

  struct BindingSpec {
    XrAction action;
    const char* path;
  };
  const BindingSpec touchBindings[] = {
      {leftMoveAction_, "/user/hand/left/input/thumbstick"},
      {rightLookAction_, "/user/hand/right/input/thumbstick"},
      {leftTriggerAction_, "/user/hand/left/input/trigger/value"},
      {rightTriggerAction_, "/user/hand/right/input/trigger/value"},
      {leftGripAction_, "/user/hand/left/input/squeeze/value"},
      {rightGripAction_, "/user/hand/right/input/squeeze/value"},
      {buttonAAction_, "/user/hand/right/input/a/click"},
      {buttonBAction_, "/user/hand/right/input/b/click"},
      {buttonXAction_, "/user/hand/left/input/x/click"},
      {buttonYAction_, "/user/hand/left/input/y/click"},
      {leftMenuAction_, "/user/hand/left/input/menu/click"},
      {leftHandPoseAction_, "/user/hand/left/input/grip/pose"},
      {rightHandPoseAction_, "/user/hand/right/input/grip/pose"}};
  const BindingSpec stickBindings[] = {
      {leftMoveAction_, "/user/hand/left/input/thumbstick"},
      {rightLookAction_, "/user/hand/right/input/thumbstick"},
      {leftTriggerAction_, "/user/hand/left/input/trigger/value"},
      {rightTriggerAction_, "/user/hand/right/input/trigger/value"},
      {leftGripAction_, "/user/hand/left/input/squeeze/value"},
      {rightGripAction_, "/user/hand/right/input/squeeze/value"},
      {leftHandPoseAction_, "/user/hand/left/input/grip/pose"},
      {rightHandPoseAction_, "/user/hand/right/input/grip/pose"}};

  auto suggestBindings = [&](const char* profileName, const BindingSpec* specs, size_t count) {
    XrPath profile = XR_NULL_PATH;
    if (stringToPath_(instance_, profileName, &profile) != XR_SUCCESS) return false;
    std::vector<XrActionSuggestedBinding> bindings;
    bindings.reserve(count);
    for (size_t index = 0; index < count; ++index) {
      XrPath bindingPath = XR_NULL_PATH;
      if (stringToPath_(instance_, specs[index].path, &bindingPath) != XR_SUCCESS) continue;
      bindings.push_back({specs[index].action, bindingPath});
    }
    if (bindings.empty()) return false;
    XrInteractionProfileSuggestedBinding suggestion{
        XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggestion.interactionProfile = profile;
    suggestion.suggestedBindings = bindings.data();
    suggestion.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
    return suggestInteractionProfileBindings_(instance_, &suggestion) == XR_SUCCESS;
  };

  bool suggested = suggestBindings("/interaction_profiles/oculus/touch_controller",
                                  touchBindings, ARRAYSIZE(touchBindings));
  suggested = suggestBindings("/interaction_profiles/microsoft/motion_controller",
                              stickBindings, ARRAYSIZE(stickBindings)) || suggested;
  suggested = suggestBindings("/interaction_profiles/valve/index_controller",
                              stickBindings, ARRAYSIZE(stickBindings)) || suggested;
  if (!suggested) {
    Log("no compatible OpenXR controller interaction profile accepted the GTA SA bindings");
    ReleaseInput();
    return false;
  }

  auto createHandSpace = [&](XrAction action, XrSpace& space, const char* operation) {
    XrActionSpaceCreateInfo info{XR_TYPE_ACTION_SPACE_CREATE_INFO};
    info.action = action;
    info.poseInActionSpace.orientation.w = 1.0f;
    return Check(createActionSpace_(session_, &info, &space), operation);
  };
  if (!createHandSpace(leftHandPoseAction_, leftHandSpace_, "xrCreateActionSpace(left grip)") ||
      !createHandSpace(rightHandPoseAction_, rightHandSpace_, "xrCreateActionSpace(right grip)")) {
    ReleaseInput();
    return false;
  }

  XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
  attachInfo.countActionSets = 1;
  attachInfo.actionSets = &inputActionSet_;
  if (!Check(attachSessionActionSets_(session_, &attachInfo), "xrAttachSessionActionSets")) {
    ReleaseInput();
    return false;
  }
  inputAttached_ = true;
  inputEnabled_ = true;
  Log("OpenXR controller actions and grip-pose spaces attached; keyboard/mouse fallback is active");
  return true;
}

void OpenXrTheater::SetKey(WORD key, bool pressed, bool& state) {
  if (state == pressed) return;
  INPUT input{};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = key;
  input.ki.dwFlags = pressed ? 0 : KEYEVENTF_KEYUP;
  SendInput(1, &input, sizeof(input));
  state = pressed;
}

void OpenXrTheater::SetMouseButton(DWORD downFlag, DWORD upFlag, bool pressed, bool& state) {
  if (state == pressed) return;
  INPUT input{};
  input.type = INPUT_MOUSE;
  input.mi.dwFlags = pressed ? downFlag : upFlag;
  SendInput(1, &input, sizeof(input));
  state = pressed;
}

void OpenXrTheater::SetButtonAction(XrAction action, bool& previous, WORD key) {
  if (!inputEnabled_ || action == XR_NULL_HANDLE || getActionStateBoolean_ == nullptr) return;
  XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
  getInfo.action = action;
  XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
  if (getActionStateBoolean_(session_, &getInfo, &state) != XR_SUCCESS) return;
  ObserveInputAction(state.isActive == XR_TRUE);
  SetKey(key, state.isActive == XR_TRUE && state.currentState == XR_TRUE, previous);
}

void OpenXrTheater::SetFloatAction(XrAction action, bool& previous, DWORD buttonFlag,
                                   float threshold) {
  if (!inputEnabled_ || action == XR_NULL_HANDLE || getActionStateFloat_ == nullptr) return;
  XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
  getInfo.action = action;
  XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
  if (getActionStateFloat_(session_, &getInfo, &state) != XR_SUCCESS) return;
  ObserveInputAction(state.isActive == XR_TRUE);
  const bool pressed = state.isActive == XR_TRUE && state.currentState >= threshold;
  const DWORD upFlag = buttonFlag == MOUSEEVENTF_LEFTDOWN
      ? MOUSEEVENTF_LEFTUP
      : MOUSEEVENTF_RIGHTUP;
  SetMouseButton(buttonFlag, upFlag, pressed, previous);
}

void OpenXrTheater::SetFloatKeyAction(XrAction action, bool& previous, WORD key, float threshold) {
  if (!inputEnabled_ || action == XR_NULL_HANDLE || getActionStateFloat_ == nullptr) return;
  XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
  getInfo.action = action;
  XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
  if (getActionStateFloat_(session_, &getInfo, &state) != XR_SUCCESS) return;
  ObserveInputAction(state.isActive == XR_TRUE);
  SetKey(key, state.isActive == XR_TRUE && state.currentState >= threshold, previous);
}

bool OpenXrTheater::ReadBooleanAction(XrAction action) {
  if (!inputEnabled_||action==XR_NULL_HANDLE||getActionStateBoolean_==nullptr) return false;
  XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
  getInfo.action=action;
  XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
  if (getActionStateBoolean_(session_,&getInfo,&state)!=XR_SUCCESS) return false;
  ObserveInputAction(state.isActive==XR_TRUE);
  return state.isActive==XR_TRUE&&state.currentState==XR_TRUE;
}

float OpenXrTheater::ReadFloatAction(XrAction action) {
  if (!inputEnabled_||action==XR_NULL_HANDLE||getActionStateFloat_==nullptr) return 0.0f;
  XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
  getInfo.action=action;
  XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
  if (getActionStateFloat_(session_,&getInfo,&state)!=XR_SUCCESS) return 0.0f;
  ObserveInputAction(state.isActive==XR_TRUE);
  return state.isActive==XR_TRUE ? state.currentState : 0.0f;
}

XrVector2f OpenXrTheater::ReadVectorAction(XrAction action) {
  if (!inputEnabled_ || action == XR_NULL_HANDLE || getActionStateVector2f_ == nullptr)
    return {0.0f, 0.0f};
  XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
  getInfo.action = action;
  XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
  if (getActionStateVector2f_(session_, &getInfo, &state) != XR_SUCCESS) {
    return {0.0f, 0.0f};
  }
  ObserveInputAction(state.isActive == XR_TRUE);
  if (state.isActive != XR_TRUE) return {0.0f, 0.0f};
  return state.currentState;
}

void OpenXrTheater::ObserveInputAction(bool active) {
  if (!active || inputActionActiveObserved_) return;
  inputActionActiveObserved_ = true;
  Log("OpenXR controller bindings are active for the current interaction profile");
}

void OpenXrTheater::ClearTrackedHands() {
  trackedHands_[0] = {};
  trackedHands_[1] = {};
  armFrame_.valid = false;
  handForwardRollLogged_[0] = handForwardRollLogged_[1] = false;
  armReachLogged_[0] = armReachLogged_[1] = false;
}

void OpenXrTheater::SampleTrackedHands() {
  TrackedHandPose sampled[2]{};
  if (getActionStatePose_ == nullptr || locateSpace_ == nullptr ||
      localSpace_ == XR_NULL_HANDLE || frameState_.predictedDisplayTime == 0) {
    ClearTrackedHands();
    return;
  }
  const XrAction actions[2] = {leftHandPoseAction_, rightHandPoseAction_};
  const XrSpace spaces[2] = {leftHandSpace_, rightHandSpace_};
  XrResult actionResults[2] = {XR_ERROR_RUNTIME_FAILURE, XR_ERROR_RUNTIME_FAILURE};
  XrResult locateResults[2] = {XR_ERROR_RUNTIME_FAILURE, XR_ERROR_RUNTIME_FAILURE};
  XrBool32 actionActive[2] = {XR_FALSE, XR_FALSE};
  XrSpaceLocationFlags locationFlags[2]{};
  for (unsigned hand = 0; hand < 2; ++hand) {
    if (actions[hand] == XR_NULL_HANDLE || spaces[hand] == XR_NULL_HANDLE) continue;
    XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
    getInfo.action = actions[hand];
    XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};
    actionResults[hand] = getActionStatePose_(session_, &getInfo, &state);
    actionActive[hand] = state.isActive;
    if (actionResults[hand] != XR_SUCCESS || state.isActive != XR_TRUE) continue;
    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    locateResults[hand] = locateSpace_(spaces[hand], localSpace_,
                                       frameState_.predictedDisplayTime, &location);
    locationFlags[hand] = location.locationFlags;
    if (locateResults[hand] != XR_SUCCESS) continue;
    constexpr XrSpaceLocationFlags required =
        XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if ((location.locationFlags & required) != required) continue;
    const auto& pose = location.pose;
    const Vec3 position{pose.position.x, pose.position.y, pose.position.z};
    Quaternion orientation{pose.orientation.x, pose.orientation.y,
                           pose.orientation.z, pose.orientation.w};
    Quaternion normalized{};
    if (!gtasa_vr::Finite(position) || !gtasa_vr::Normalize(orientation, normalized)) continue;
    sampled[hand] = {true, position, normalized};
  }
  if (sampled[0].valid && sampled[1].valid) {
    constexpr float smoothingAlpha=0.45f;
    constexpr float positionDeadband=0.002f;
    for (unsigned hand=0;hand<2;++hand) {
      if (!trackedHands_[hand].valid) {
        trackedHands_[hand]=sampled[hand];
        continue;
      }
      gtasa_vr::FilteredTrackingPose filtered{};
      const gtasa_vr::FilteredTrackingPose previous{
          trackedHands_[hand].position,trackedHands_[hand].orientation};
      const gtasa_vr::FilteredTrackingPose current{
          sampled[hand].position,sampled[hand].orientation};
      if (!gtasa_vr::SmoothTrackedPose(
              previous,current,smoothingAlpha,positionDeadband,filtered)) {
        ClearTrackedHands();
        return;
      }
      trackedHands_[hand]={true,filtered.position,filtered.orientation};
    }
    if (!armGripAvailableLogged_) {
      armGripAvailableLogged_ = true;
      Log("arm trace: paired grip poses valid left=(%.3f,%.3f,%.3f) right=(%.3f,%.3f,%.3f)",
          trackedHands_[0].position.x, trackedHands_[0].position.y,
          trackedHands_[0].position.z, trackedHands_[1].position.x,
          trackedHands_[1].position.y, trackedHands_[1].position.z);
    }
  } else {
    ClearTrackedHands();
    if (!armGripUnavailableLogged_) {
      armGripUnavailableLogged_ = true;
      Log("arm trace: grip pose unavailable left(action=%d active=%u locate=%d flags=0x%llx) right(action=%d active=%u locate=%d flags=0x%llx)",
          static_cast<int>(actionResults[0]), static_cast<unsigned>(actionActive[0]),
          static_cast<int>(locateResults[0]), static_cast<unsigned long long>(locationFlags[0]),
          static_cast<int>(actionResults[1]), static_cast<unsigned>(actionActive[1]),
          static_cast<int>(locateResults[1]), static_cast<unsigned long long>(locationFlags[1]));
    }
  }
}

void OpenXrTheater::PollInput(XrTime turnSampleTime) {
  DWORD foregroundProcess = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
  const bool sessionFocused = sessionState_ == XR_SESSION_STATE_FOCUSED;
  const bool processForeground = foregroundProcess == GetCurrentProcessId();
  const bool canSendInput = gtasa_vr::CanSendInput(
      sessionRunning_, sessionFocused, processForeground, true);
  if (!inputGateStateKnown_ || inputGateAllowed_ != canSendInput) {
    inputGateStateKnown_ = true;
    inputGateAllowed_ = canSendInput;
    Log("input trace: action polling %s running=%u xrFocused=%u gameForeground=%u foregroundPid=%lu gamePid=%lu",
        canSendInput ? "enabled" : "gated", sessionRunning_ ? 1u : 0u,
        sessionFocused ? 1u : 0u, processForeground ? 1u : 0u,
        static_cast<unsigned long>(foregroundProcess),
        static_cast<unsigned long>(GetCurrentProcessId()));
  }
  if (!inputEnabled_ || !inputAttached_ || syncActions_ == nullptr || !canSendInput) {
    ReleasePressedInputs();
    ClearTrackedHands();
    return;
  }
  XrActiveActionSet activeSet{};
  activeSet.actionSet = inputActionSet_;
  activeSet.subactionPath = XR_NULL_PATH;
  XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
  syncInfo.countActiveActionSets = 1;
  syncInfo.activeActionSets = &activeSet;
  if (syncActions_(session_, &syncInfo) != XR_SUCCESS) {
    ReleasePressedInputs();
    ClearTrackedHands();
    return;
  }
  if (!inputSyncObserved_) {
    inputSyncObserved_ = true;
    Log("OpenXR controller action set synchronized");
  }
  SampleTrackedHands();

  const bool frontEndMenu = g_config.frontEndMenuActive != 0 &&
      *reinterpret_cast<const uint8_t*>(g_config.frontEndMenuActive) != 0;
  if (!inputMenuModeKnown_ || inputMenuMode_ != frontEndMenu) {
    ReleasePressedInputs();
    inputMenuMode_ = frontEndMenu;
    inputMenuModeKnown_ = true;
  }

  const XrVector2f move = ReadVectorAction(leftMoveAction_);
  rawMove_=move;
  if (!moveVectorObservedLogged_ && (std::abs(move.x) > 0.05f || std::abs(move.y) > 0.05f)) {
    moveVectorObservedLogged_ = true;
    Log("input trace: left thumbstick active x=%.3f y=%.3f", move.x, move.y);
  }
  if (!lateralVectorObservedLogged_ && std::abs(move.x) > 0.25f) {
    lateralVectorObservedLogged_ = true;
    Log("input trace: lateral thumbstick crossed dead zone x=%.3f dispatch=%s",
        move.x, move.x < 0.0f ? "A" : "D");
  }
  const XrVector2f look = ReadVectorAction(rightLookAction_);
  const float lookX = std::abs(look.x) > 0.12f ? look.x : 0.0f;
  const float lookY = std::abs(look.y) > 0.12f ? look.y : 0.0f;
  if (lookX != 0.0f || lookY != 0.0f) {
    if (!lookVectorObservedLogged_) {
      lookVectorObservedLogged_ = true;
      Log("input trace: right thumbstick active x=%.3f y=%.3f",look.x,look.y);
    }
  }
  const bool cutscene =
      *reinterpret_cast<const uint8_t*>(g_config.cutsceneRunning) != 0 ||
      *reinterpret_cast<const uint8_t*>(g_config.cutsceneProcessing) != 0;
  using FadeStatus = int(__thiscall*)(void*);
  const bool fullyFaded = !frontEndMenu &&
      reinterpret_cast<FadeStatus>(g_config.fadeStatus)(
          reinterpret_cast<void*>(g_config.cameraAddress)) == 2;
  using FindPlayerPed = void*(__cdecl*)(int);
  using FindPlayerVehicle = void*(__cdecl*)(int, bool);
  const bool onFoot = !frontEndMenu && !cutscene && !fullyFaded &&
      g_config.findPlayerPed != 0 && g_config.findPlayerVehicle != 0 &&
      reinterpret_cast<FindPlayerPed>(g_config.findPlayerPed)(0) != nullptr &&
      reinterpret_cast<FindPlayerVehicle>(g_config.findPlayerVehicle)(0, false) == nullptr;
  const bool canTurnRig = g_stereoRequested && g_firstPersonRequested &&
      !frontEndMenu && !cutscene && onFoot;
  headRelativeMoveAllowed_=canTurnRig;
  float turnElapsedSeconds=0.0f;
  if (canTurnRig && turnSampleTime>lastTurnSampleTime_ && lastTurnSampleTime_>0)
    turnElapsedSeconds=static_cast<float>(turnSampleTime-lastTurnSampleTime_)*1.0e-9f;
  lastTurnSampleTime_=canTurnRig ? turnSampleTime : 0;
  if (g_turnMode==gtasa_vr::TurnMode::Smooth) {
    if (gtasa_vr::AdvanceSmoothTurn(snapTurn_,look.x,turnElapsedSeconds,
                                    g_smoothTurnDegreesPerSecond,canTurnRig) &&
        !smoothTurnObservedLogged_) {
      smoothTurnObservedLogged_=true;
      Log("input trace: VR rig smooth turn active speed=%.1f degrees/second",
          g_smoothTurnDegreesPerSecond);
    }
  } else if (gtasa_vr::AdvanceSnapTurn(snapTurn_,look.x,canTurnRig,g_snapTurnDegrees)) {
    Log("input trace: VR rig snap turn yaw=%.1f degrees",snapTurn_.yawRadians*57.2957795f);
  }
  if (frontEndMenu) {
    nativePadActive_=gtasa_vr::BuildGtaMenuPadState(
        {move.x,move.y,0.0f},{look.x,look.y,0.0f},
        ReadBooleanAction(buttonAAction_),ReadBooleanAction(buttonBAction_),
        ReadFloatAction(rightTriggerAction_),ReadFloatAction(leftTriggerAction_),nativePadState_);
  } else {
    const Vec3 moveAxes{move.x,move.y,0.0f};
    // Horizontal look belongs to the shared VR rig, not GTA's independent
    // camera orbit. Feeding it to both would turn the view twice.
    const Vec3 lookAxes{canTurnRig ? 0.0f : look.x,look.y,0.0f};
    nativePadActive_=gtasa_vr::BuildGtaPadState(
        moveAxes,lookAxes,ReadBooleanAction(buttonAAction_),
        ReadBooleanAction(buttonBAction_),ReadBooleanAction(buttonXAction_),
        ReadBooleanAction(buttonYAction_),ReadFloatAction(leftTriggerAction_),
        ReadFloatAction(rightTriggerAction_),ReadFloatAction(leftGripAction_),nativePadState_);
    nativePadState_.start=gtasa_vr::GtaButton(ReadBooleanAction(leftMenuAction_));
  }
}

void OpenXrTheater::ApplyNativePadInput(gtasa_vr::GtaPadState& state) {
  if (!nativePadActive_||!inputGateAllowed_) return;
  gtasa_vr::MergeGtaPadState(state,nativePadState_);
  if (!nativePadDispatchLogged_) {
    nativePadDispatchLogged_=true;
    Log("input trace: OpenXR controls merged into GTA native CPad after game/GInput update");
  }
}

void OpenXrTheater::ApplyPendingBodyTurn() {
  const float pending=snapTurn_.pendingBodyRadians;
  if (std::abs(pending)<0.0001f) return;
  const bool frontEndMenu = *reinterpret_cast<const uint8_t*>(g_config.frontEndMenuActive) != 0;
  const bool cutscene = *reinterpret_cast<const uint8_t*>(g_config.cutsceneRunning) != 0 ||
      *reinterpret_cast<const uint8_t*>(g_config.cutsceneProcessing) != 0;
  using FadeStatus = int(__thiscall*)(void*);
  const bool faded = !frontEndMenu &&
      reinterpret_cast<FadeStatus>(g_config.fadeStatus)(
          reinterpret_cast<void*>(g_config.cameraAddress)) == 2;
  using FindPlayerPed = void*(__cdecl*)(int);
  using FindPlayerVehicle = void*(__cdecl*)(int, bool);
  void* ped = nullptr;
  if (nativePadActive_ && inputGateAllowed_ && !frontEndMenu && !cutscene && !faded &&
      g_config.findPlayerPed != 0 && g_config.findPlayerVehicle != 0 &&
      reinterpret_cast<FindPlayerVehicle>(g_config.findPlayerVehicle)(0, false) == nullptr)
    ped = reinterpret_cast<FindPlayerPed>(g_config.findPlayerPed)(0);
  if (ped == nullptr || g_config.setPlayerHeading == 0) {
    gtasa_vr::CancelPendingBodyTurn(snapTurn_);
    return;
  }
  auto* current = reinterpret_cast<float*>(static_cast<uint8_t*>(ped) + 0x558);
  auto* goal = reinterpret_cast<float*>(static_cast<uint8_t*>(ped) + 0x55C);
  if (!std::isfinite(*current) || !std::isfinite(*goal) ||
      std::abs(*current)>1000.0f || std::abs(*goal)>1000.0f) {
    gtasa_vr::CancelPendingBodyTurn(snapTurn_);
    Log("input trace: VR body turn cancelled because player heading is invalid");
    return;
  }
  // GTA heading is counterclockwise while the VR right stick's positive X is
  // a clockwise world turn. Keep the game's current and desired headings in
  // step so its next animation update cannot immediately unwind the snap.
  const float newHeading=std::remainder(*current-pending,gtasa_vr::kFullCircleRadians);
  using SetHeading = void(__thiscall*)(void*,float);
  reinterpret_cast<SetHeading>(g_config.setPlayerHeading)(ped,newHeading);
  *current=newHeading;
  *goal=newHeading;
  gtasa_vr::CommitBodyTurn(snapTurn_);
  if (g_turnMode==gtasa_vr::TurnMode::Snap) {
    Log("input trace: player body snap turn committed heading=%.1f degrees",
        newHeading*57.2957795f);
  } else if (!smoothBodyTurnLogged_) {
    smoothBodyTurnLogged_=true;
    Log("input trace: player body smooth turn committed; GTA camera follow-through enabled");
  }
}

void OpenXrTheater::ReleasePressedInputs() {
  nativePadActive_=false;
  nativePadState_={};
  headRelativeMoveAllowed_=false;
  rawMove_={};
  lastTurnSampleTime_=0;
  gtasa_vr::CancelPendingBodyTurn(snapTurn_);
  SetKey('W', false, keyW_);
  SetKey('A', false, keyA_);
  SetKey('S', false, keyS_);
  SetKey('D', false, keyD_);
  SetKey(VK_LSHIFT, false, keyShift_);
  SetKey(VK_SPACE, false, keySpace_);
  SetKey('F', false, keyF_);
  SetKey('R', false, keyR_);
  SetKey(VK_RETURN, false, keyEnter_);
  SetKey(VK_ESCAPE, false, keyEscape_);
  SetMouseButton(MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP, false, mouseAim_);
  SetMouseButton(MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP, false, mouseFire_);

}

void OpenXrTheater::ReleaseInput() {
  ReleasePressedInputs();
  RestoreArmOverride();
  ClearTrackedHands();
  if (destroySpace_ != nullptr) {
    for (XrSpace* space : {&leftHandSpace_, &rightHandSpace_}) {
      if (*space != XR_NULL_HANDLE) {
        destroySpace_(*space);
        *space = XR_NULL_HANDLE;
      }
    }
  }
  if (destroyAction_ != nullptr) {
    for (XrAction* action : {&leftMoveAction_, &rightLookAction_, &leftTriggerAction_,
                             &rightTriggerAction_, &leftGripAction_, &rightGripAction_,
                             &buttonAAction_, &buttonBAction_, &buttonXAction_,
                             &buttonYAction_, &leftMenuAction_, &leftHandPoseAction_,
                             &rightHandPoseAction_}) {
      if (*action != XR_NULL_HANDLE) {
        destroyAction_(*action);
        *action = XR_NULL_HANDLE;
      }
    }
  }
  if (inputActionSet_ != XR_NULL_HANDLE && destroyActionSet_ != nullptr) {
    destroyActionSet_(inputActionSet_);
    inputActionSet_ = XR_NULL_HANDLE;
  }
  inputAttached_ = false;
  inputEnabled_ = false;
  inputSyncObserved_ = false;
  inputActionActiveObserved_ = false;
  inputGateStateKnown_ = false;
  inputGateAllowed_ = false;
  moveVectorObservedLogged_ = false;
  lateralVectorObservedLogged_ = false;
  lookVectorObservedLogged_ = false;
  lookDispatchFailureLogged_ = false;
  nativePadDispatchLogged_ = false;
  inputMenuMode_ = false;
  inputMenuModeKnown_ = false;
}

void OpenXrTheater::CaptureBaseCamera() {
  auto* camera = reinterpret_cast<const uint8_t*>(g_config.cameraAddress + g_config.cameraMatrixOffset);
  std::memcpy(&baseCamera_, camera, sizeof(baseCamera_));
  vrCameraBasis_ = {baseCamera_.right, baseCamera_.forward,
                    baseCamera_.up, baseCamera_.position};
  if (g_firstPersonRequested) CaptureFirstPersonBasis();
  if (g_firstPersonRequested) {
    gtasa_vr::ReconcileCameraTurn(snapTurn_,vrCameraBasis_.forward);
    vrCameraBasis_ = gtasa_vr::YawBasis(vrCameraBasis_,snapTurn_.yawRadians);
  }
  if (nativePadActive_ && inputGateAllowed_ && headRelativeMoveAllowed_ &&
      referencePoseCaptured_ && views_.size()==2) {
    const auto& pose=views_[0].pose;
    const auto renderedHead=gtasa_vr::EyePose(
        vrCameraBasis_,referenceOrientation_,referenceHeadPosition_,
        {pose.orientation.x,pose.orientation.y,pose.orientation.z,pose.orientation.w},
        {pose.position.x,pose.position.y,pose.position.z},kCorrectHorizontalTracking);
    Vec3 aligned{};
    if (gtasa_vr::HeadRelativeMove({rawMove_.x,rawMove_.y,0},
        {baseCamera_.right,baseCamera_.forward,baseCamera_.up,baseCamera_.position},
        renderedHead,aligned)) {
      nativePadState_.leftStickX=gtasa_vr::GtaAxis(aligned.x);
      nativePadState_.leftStickY=gtasa_vr::GtaAxis(-aligned.y);
      if (!moveAlignedLogged_ && (std::abs(rawMove_.x)>0.05f || std::abs(rawMove_.y)>0.05f)) {
        moveAlignedLogged_=true;
        Log("input trace: head-relative left stick raw=(%.3f,%.3f) GTA=(%d,%d)",
            rawMove_.x,rawMove_.y,nativePadState_.leftStickX,nativePadState_.leftStickY);
      }
    }
  }
  baseCameraCaptured_ = true;
  rwCamera_ = *reinterpret_cast<void**>(g_config.cameraAddress + g_config.rwCameraOffset);
  if (rwCamera_ != nullptr) {
    std::memcpy(&baseViewWindow_, static_cast<uint8_t*>(rwCamera_) + 0x68, sizeof(baseViewWindow_));
    std::memcpy(&baseViewOffset_, static_cast<uint8_t*>(rwCamera_) + 0x78, sizeof(baseViewOffset_));
  }
  std::memcpy(&baseFov_, reinterpret_cast<const void*>(g_config.fovAddress), sizeof(baseFov_));
  baseFovCaptured_ = std::isfinite(baseFov_) && baseFov_ > 0.0f;
  std::memcpy(&baseAspectRatio_, reinterpret_cast<const void*>(g_config.aspectRatioAddress),
              sizeof(baseAspectRatio_));
  baseAspectRatioCaptured_ = std::isfinite(baseAspectRatio_) && baseAspectRatio_ > 0.0f;
}

void OpenXrTheater::BuildArmFrameSnapshot() {
  armFrame_.valid = false;
  if (!g_firstPersonRequested || !referencePoseCaptured_ ||
      !trackedHands_[0].valid || !trackedHands_[1].valid) return;
  auto mapHand = [&](const TrackedHandPose& tracked, gtasa_vr::Basis& hand) {
    hand = gtasa_vr::ControllerPose(
        vrCameraBasis_, referenceOrientation_, referenceHeadPosition_,
        tracked.orientation, tracked.position, kCorrectHorizontalTracking);
    return gtasa_vr::Finite(hand.position) && gtasa_vr::Finite(hand.right) &&
        gtasa_vr::Finite(hand.forward) && gtasa_vr::Finite(hand.up);
  };
  if (!mapHand(trackedHands_[0], armFrame_.leftHand) ||
      !mapHand(trackedHands_[1], armFrame_.rightHand)) return;
  armFrame_.displayTime = frameState_.predictedDisplayTime;
  armFrame_.serial = ++armFrameSerial_;
  armFrame_.valid = true;
  if (!armSnapshotLogged_) {
    armSnapshotLogged_ = true;
    Log("arm trace: world targets built left=(%.3f,%.3f,%.3f) right=(%.3f,%.3f,%.3f)",
        armFrame_.leftHand.position.x, armFrame_.leftHand.position.y,
        armFrame_.leftHand.position.z, armFrame_.rightHand.position.x,
        armFrame_.rightHand.position.y, armFrame_.rightHand.position.z);
  }
}

void OpenXrTheater::RestoreArmOverride() {
  if (!armOverrideActive_) return;
  for (unsigned index = 0; index < ARRAYSIZE(armOverrideMatrices_); ++index) {
    if (armOverrideMatrices_[index] != nullptr)
      *armOverrideMatrices_[index] = armOverrideOriginal_[index];
    armOverrideMatrices_[index] = nullptr;
  }
  armOverridePed_ = nullptr;
  armOverrideActive_ = false;
}

void OpenXrTheater::AfterPedPreRender(void* ped) {
  if (armOverrideActive_ || !armFrame_.valid || !stereoFrameActive_ || ped == nullptr ||
      g_config.findPlayerPed == 0 || g_config.findPlayerVehicle == 0) return;
  using FindPlayerPed = void*(__cdecl*)(int);
  using FindPlayerVehicle = void*(__cdecl*)(int, bool);
  if (reinterpret_cast<FindPlayerPed>(g_config.findPlayerPed)(0) != ped) return;
  if (reinterpret_cast<FindPlayerVehicle>(g_config.findPlayerVehicle)(0, false) != nullptr) {
    if (!armPlayerAttemptLogged_) {
      armPlayerAttemptLogged_ = true;
      Log("arm trace: player hook reached but vehicle mode keeps native arms");
    }
    return;
  }
  if (!armPlayerAttemptLogged_) {
    armPlayerAttemptLogged_ = true;
    Log("arm trace: player CPed::PreRender hook reached on foot");
  }

  void* clump = *reinterpret_cast<void**>(static_cast<uint8_t*>(ped) + kEntityRwObjectOffset);
  if (clump == nullptr) {
    if (!armSolveFailureLogged_) {
      armSolveFailureLogged_ = true;
      Log("arm trace: player has no RenderWare clump");
    }
    return;
  }
  using GetHierarchy = void*(__cdecl*)(void*);
  using GetIndex = int(__cdecl*)(void*, int);
  using GetMatrices = RwMatrixNative*(__cdecl*)(void*);
  void* hierarchy = reinterpret_cast<GetHierarchy>(g_config.getAnimHierarchyFromClump)(clump);
  if (hierarchy == nullptr) {
    if (!armSolveFailureLogged_) {
      armSolveFailureLogged_ = true;
      Log("arm trace: player clump has no HAnim hierarchy");
    }
    return;
  }
  const int hierarchyFlags = *reinterpret_cast<int*>(hierarchy);
  const int nodeCount = *reinterpret_cast<int*>(static_cast<uint8_t*>(hierarchy) + 4);
  RwMatrixNative* matrices =
      reinterpret_cast<GetMatrices>(g_config.rpHAnimGetMatrixArray)(hierarchy);
  if (!armPlayerHierarchyLogged_) {
    armPlayerHierarchyLogged_ = true;
    Log("arm trace: hierarchy flags=0x%08x nodes=%d matrices=%p",
        hierarchyFlags, nodeCount, matrices);
  }
  if (nodeCount <= 0 || nodeCount > 256 || matrices == nullptr) {
    if (!armSolveFailureLogged_) {
      armSolveFailureLogged_ = true;
      Log("arm trace: invalid hierarchy matrix array");
    }
    return;
  }

  const unsigned boneIds[10] = {
      kLeftUpperArmBone, kLeftForearmBone, kLeftHandBone,
      kLeftFingerBone, kLeftFingerTipBone,
      kRightUpperArmBone, kRightForearmBone, kRightHandBone,
      kRightFingerBone, kRightFingerTipBone};
  for (unsigned index = 0; index < ARRAYSIZE(boneIds); ++index) {
    const int matrixIndex = reinterpret_cast<GetIndex>(g_config.rpHAnimIdGetIndex)(
        hierarchy, static_cast<int>(boneIds[index]));
    const bool optionalFinger = index % 5 >= 3;
    if (matrixIndex < 0 || matrixIndex >= nodeCount) {
      if (optionalFinger) continue;
      if (!armSolveFailureLogged_) {
        armSolveFailureLogged_ = true;
        Log("arm trace: required bone id=%u was not present", boneIds[index]);
      }
      return;
    }
    armOverrideMatrices_[index] = &matrices[matrixIndex];
    armOverrideOriginal_[index] = matrices[matrixIndex];
  }

  auto solveArm = [&](unsigned first, unsigned handIndex, const char* side,
                      const gtasa_vr::Basis& tracked) {
    auto& upper = *armOverrideMatrices_[first];
    auto& forearm = *armOverrideMatrices_[first + 1];
    auto& hand = *armOverrideMatrices_[first + 2];
    const gtasa_vr::Basis nativeHand = GetRwBasis(hand);
    const Vec3 nativeUpper = gtasa_vr::Subtract(forearm.position, upper.position);
    const Vec3 nativeForearm = gtasa_vr::Subtract(hand.position, forearm.position);
    const float upperLength = gtasa_vr::Length(nativeUpper);
    const float forearmLength = gtasa_vr::Length(nativeForearm);
    if (upperLength < 0.10f || upperLength > 0.80f ||
        forearmLength < 0.10f || forearmLength > 0.80f) {
      if (!armSolveFailureLogged_) {
        armSolveFailureLogged_ = true;
        Log("arm trace: %s bone lengths rejected upper=%.4f forearm=%.4f shoulder=(%.3f,%.3f,%.3f) elbow=(%.3f,%.3f,%.3f) wrist=(%.3f,%.3f,%.3f)",
            side, upperLength, forearmLength,
            upper.position.x, upper.position.y, upper.position.z,
            forearm.position.x, forearm.position.y, forearm.position.z,
            hand.position.x, hand.position.y, hand.position.z);
      }
      return false;
    }
    gtasa_vr::TwoBoneSolution solution{};
    if (!gtasa_vr::SolveTwoBone(upper.position, forearm.position, tracked.position,
                                upperLength, forearmLength, solution)) {
      if (!armSolveFailureLogged_) {
        armSolveFailureLogged_ = true;
        Log("arm trace: %s two-bone solve rejected target=(%.3f,%.3f,%.3f)",
            side, tracked.position.x, tracked.position.y, tracked.position.z);
      }
      return false;
    }
    if (!armReachLogged_[handIndex]) {
      armReachLogged_[handIndex] = true;
      Log("arm trace: %s reach requested=%.4f available=%.4f clamped=%u shoulder=(%.3f,%.3f,%.3f) target=(%.3f,%.3f,%.3f)",
          side, gtasa_vr::Length(gtasa_vr::Subtract(tracked.position,upper.position)),
          upperLength+forearmLength, solution.clamped ? 1u : 0u,
          upper.position.x,upper.position.y,upper.position.z,
          tracked.position.x,tracked.position.y,tracked.position.z);
    }
    RotateRwBasis(upper, gtasa_vr::RotationBetween(nativeUpper,
                  gtasa_vr::Subtract(solution.elbow, upper.position)));
    RotateRwBasis(forearm, gtasa_vr::RotationBetween(nativeForearm,
                  gtasa_vr::Subtract(solution.wrist, solution.elbow)));
    forearm.position = solution.elbow;
    const Vec3 nativeFingerPosition = armOverrideMatrices_[first+3] != nullptr
        ? armOverrideMatrices_[first+3]->position
        : gtasa_vr::Add(nativeHand.position,nativeHand.forward);
    Vec3 reachForward{};
    if (!gtasa_vr::GameplayHandForward(
            upper.position,solution.wrist,vrCameraBasis_.forward,vrCameraBasis_.right,
            vrCameraBasis_.up,handIndex==0,reachForward)) {
      if (!armSolveFailureLogged_) {
        armSolveFailureLogged_=true;
        Log("arm trace: %s reach-relative wrist direction rejected",side);
      }
      return false;
    }
    gtasa_vr::Basis alignedHand{};
    if (!gtasa_vr::AlignHandForwardWithControllerRoll(
            nativeHand,nativeFingerPosition,reachForward,vrCameraBasis_.up,
            tracked.up,solution.wrist,alignedHand)) {
      if (!armSolveFailureLogged_) {
        armSolveFailureLogged_ = true;
        Log("arm trace: %s forward wrist and controller roll alignment rejected", side);
      }
      return false;
    }
    if (!handForwardRollLogged_[handIndex]) {
      handForwardRollLogged_[handIndex] = true;
      Log("arm trace: %s reach-relative wrist alignment with controller roll active", side);
    }
    SetRwBasis(hand, alignedHand);
    hand.position = solution.wrist;
    const gtasa_vr::Basis trackedHand = GetRwBasis(hand);
    for (unsigned index = first + 3; index < first + 5; ++index) {
      if (armOverrideMatrices_[index] == nullptr) continue;
      gtasa_vr::Basis rebased{};
      if (!gtasa_vr::RigidlyRebase(nativeHand, trackedHand,
                                    GetRwBasis(*armOverrideMatrices_[index]), rebased)) {
        if (!armSolveFailureLogged_) {
          armSolveFailureLogged_ = true;
          Log("arm trace: %s finger descendant rebase rejected bone-slot=%u", side, index);
        }
        return false;
      }
      SetRwMatrix(*armOverrideMatrices_[index], rebased);
    }
    return true;
  };
  if (!solveArm(0, 0, "left", armFrame_.leftHand) ||
      !solveArm(5, 1, "right", armFrame_.rightHand)) {
    for (unsigned index = 0; index < ARRAYSIZE(armOverrideMatrices_); ++index) {
      if (armOverrideMatrices_[index] != nullptr)
        *armOverrideMatrices_[index] = armOverrideOriginal_[index];
      armOverrideMatrices_[index] = nullptr;
    }
    return;
  }
  armOverridePed_ = ped;
  armOverrideActive_ = true;
  if (!armOverrideAppliedLogged_) {
    armOverrideAppliedLogged_ = true;
    Log("arm trace: controller-driven player arm matrices applied successfully");
  }
}

bool OpenXrTheater::CaptureFirstPersonBasis() {
  using FindPlayerPed = void*(__cdecl*)(int);
  using GetBonePosition = void(__thiscall*)(void*, Vec3&, unsigned int, bool);
  if (g_config.findPlayerPed == 0 || g_config.getBonePosition == 0) return false;

  void* player = reinterpret_cast<FindPlayerPed>(g_config.findPlayerPed)(0);
  if (player != nullptr) {
    const float invalid = std::numeric_limits<float>::quiet_NaN();
    Vec3 head{invalid, invalid, invalid};
    reinterpret_cast<GetBonePosition>(g_config.getBonePosition)(
        player, head, kPlayerHeadBone, true);
    gtasa_vr::Basis anchored{};
    if (gtasa_vr::HeadAnchoredBasis(vrCameraBasis_, head,
                                    kFirstPersonForwardOffset, anchored)) {
      vrCameraBasis_ = anchored;
      if (!firstPersonHeadAnchorObserved_) {
        firstPersonHeadAnchorObserved_ = true;
        Log("first-person VR camera anchored to player head bone with %.2fm forward eye offset head=(%.3f,%.3f,%.3f) eye=(%.3f,%.3f,%.3f) xrReferenceHead=(%.3f,%.3f,%.3f)",
            kFirstPersonForwardOffset,head.x,head.y,head.z,
            anchored.position.x,anchored.position.y,anchored.position.z,
            referenceHeadPosition_.x,referenceHeadPosition_.y,referenceHeadPosition_.z);
      }
      return true;
    }
  }

  if (!firstPersonHeadAnchorUnavailableObserved_) {
    firstPersonHeadAnchorUnavailableObserved_ = true;
    Log("first-person player head is unavailable; using the current GTA camera for this frame");
  }
  return false;
}

float OpenXrTheater::CalculateEyeFov(const XrFovf& fov) const {
  gtasa_vr::Frustum frustum;
  if (!gtasa_vr::MakeFrustum(fov.angleLeft, fov.angleRight, fov.angleUp, fov.angleDown, frustum))
    return baseFov_;
  return 2.0f * std::atan(frustum.horizontal) * 57.29577951308232f;
}

float OpenXrTheater::CalculateEyeAspectRatio(const XrFovf& fov) const {
  gtasa_vr::Frustum frustum;
  if (!gtasa_vr::MakeFrustum(fov.angleLeft, fov.angleRight, fov.angleUp, fov.angleDown, frustum))
    return baseAspectRatio_;
  return frustum.horizontal / frustum.vertical;
}

// These RW entrypoints and offsets are only used after the exact executable
// digest AND the render contract signatures have been validated.
void OpenXrTheater::ApplyRwProjection(void* camera) {
  if (!stereoFrameActive_ || camera == nullptr || camera != rwCamera_) return;
  const auto& f = eyeFrustums_[currentEye_];
  const XrVector2f window{f.viewWindowX, f.viewWindowY};
  const XrVector2f offset{f.viewOffsetX, f.viewOffsetY};
  using SetRwVector = void*(__cdecl*)(void*, const XrVector2f*);
  reinterpret_cast<SetRwVector>(g_config.setRwViewWindow)(camera, &window);
  reinterpret_cast<SetRwVector>(g_config.setRwViewOffset)(camera, &offset);
}

bool OpenXrTheater::ApplyEye(IDirect3DDevice9* device, uint32_t eye) {
  if (device == nullptr || eye >= 2 || !baseCameraCaptured_ || views_.size() != 2 ||
      !referencePoseCaptured_ || gameEyeWidth_ == 0 || gameHeight_ == 0) {
    return false;
  }

  currentEye_ = eye;
  const auto& pose = views_[eye].pose;
  const auto transformed = gtasa_vr::EyePose(vrCameraBasis_, referenceOrientation_, referenceHeadPosition_,
      {pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w},
      {pose.position.x, pose.position.y, pose.position.z}, kCorrectHorizontalTracking);
  if (g_traceStereoFrame && eye == 0) {
    Log("stereo trace: pose q=(%.4f,%.4f,%.4f,%.4f) forward=(%.4f,%.4f,%.4f)",
        pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w,
        transformed.forward.x, transformed.forward.y, transformed.forward.z);
  }
  GtaCameraMatrix eyeCamera = baseCamera_;
  eyeCamera.right = transformed.right;
  eyeCamera.forward = transformed.forward;
  eyeCamera.up = transformed.up;
  eyeCamera.position = transformed.position;
  std::memcpy(reinterpret_cast<void*>(g_config.cameraAddress + g_config.cameraMatrixOffset), &eyeCamera,
              sizeof(eyeCamera));
  if (baseFovCaptured_) {
    const float eyeFov = CalculateEyeFov(views_[eye].fov);
    std::memcpy(reinterpret_cast<void*>(g_config.fovAddress), &eyeFov, sizeof(eyeFov));
  }
  if (baseAspectRatioCaptured_) {
    const float eyeAspectRatio = CalculateEyeAspectRatio(views_[eye].fov);
    std::memcpy(reinterpret_cast<void*>(g_config.aspectRatioAddress), &eyeAspectRatio,
                sizeof(eyeAspectRatio));
  }
  // Culling reads RsGlobal's physical aspect, not the aspect-ratio global.
  // Override those dimensions only during the derived-value calculation.
  auto* dimensions = reinterpret_cast<int32_t*>(g_config.screenDimensions);
  const int32_t savedWidth = dimensions[0], savedHeight = dimensions[1];
  dimensions[0] = 100000;
  dimensions[1] = static_cast<int32_t>(100000.0f / CalculateEyeAspectRatio(views_[eye].fov));
  using DeriveCamera = void(__thiscall*)(void*, bool, bool);
  reinterpret_cast<DeriveCamera>(g_config.deriveCamera)(
      reinterpret_cast<void*>(g_config.cameraAddress), false, false);
  dimensions[0] = savedWidth;
  dimensions[1] = savedHeight;
  if (g_copyCameraMatrixToRwCam != nullptr)
    g_copyCameraMatrixToRwCam(reinterpret_cast<void*>(g_config.cameraAddress), true);
  ApplyRwProjection(rwCamera_);

  D3DVIEWPORT9 viewport{};
  viewport.X = 0;
  viewport.Y = 0;
  viewport.Width = gameEyeWidth_;
  viewport.Height = gameHeight_;
  viewport.MinZ = 0.0f;
  viewport.MaxZ = 1.0f;
  return SUCCEEDED(device->SetViewport(&viewport));
}

void OpenXrTheater::RestoreGameState(IDirect3DDevice9* device) {
  if (baseCameraCaptured_) {
    std::memcpy(reinterpret_cast<void*>(g_config.cameraAddress + g_config.cameraMatrixOffset), &baseCamera_,
                sizeof(baseCamera_));
  }
  if (baseFovCaptured_)
    std::memcpy(reinterpret_cast<void*>(g_config.fovAddress), &baseFov_, sizeof(baseFov_));
  if (baseAspectRatioCaptured_)
    std::memcpy(reinterpret_cast<void*>(g_config.aspectRatioAddress), &baseAspectRatio_,
                sizeof(baseAspectRatio_));
  if (baseCameraCaptured_) {
    using DeriveCamera = void(__thiscall*)(void*, bool, bool);
    reinterpret_cast<DeriveCamera>(g_config.deriveCamera)(
        reinterpret_cast<void*>(g_config.cameraAddress), false, false);
    if (g_copyCameraMatrixToRwCam != nullptr)
      g_copyCameraMatrixToRwCam(reinterpret_cast<void*>(g_config.cameraAddress), true);
    if (rwCamera_ != nullptr) {
      using SetRwVector = void*(__cdecl*)(void*, const XrVector2f*);
      reinterpret_cast<SetRwVector>(g_config.setRwViewWindow)(rwCamera_, &baseViewWindow_);
      reinterpret_cast<SetRwVector>(g_config.setRwViewOffset)(rwCamera_, &baseViewOffset_);
    }
  }
  rwCamera_ = nullptr;
  if (device != nullptr && viewportSaved_) device->SetViewport(&savedViewport_);
  baseCameraCaptured_ = false;
  baseFovCaptured_ = false;
  baseAspectRatioCaptured_ = false;
  viewportSaved_ = false;
}

bool OpenXrTheater::RenderCompleteFrame(IDirect3DDevice9* device, VoidFn renderTail) {
  if (renderTail == nullptr) return false;
  // Frontend, pause, native fades and full animated cutscenes remain intact on
  // a head-locked panel.  Opaque composition supplies the black theater around
  // the panel while GTA's own fades remain part of the captured frame.
  using FadeStatus = int(__thiscall*)(void*);
  const bool frontEndMenu = *reinterpret_cast<const uint8_t*>(g_config.frontEndMenuActive) != 0;
  const bool fullyFaded = reinterpret_cast<FadeStatus>(g_config.fadeStatus)(
      reinterpret_cast<void*>(g_config.cameraAddress)) == 2;
  const bool cutsceneRunning = *reinterpret_cast<const uint8_t*>(g_config.cutsceneRunning) != 0;
  const bool cutsceneProcessing =
      *reinterpret_cast<const uint8_t*>(g_config.cutsceneProcessing) != 0;
  const bool cinematic = gtasa_vr::UseCinematicMode(
      frontEndMenu, fullyFaded, cutsceneRunning, cutsceneProcessing);
  if (cinematic) {
    armFrame_.valid = false;
    RestoreArmOverride();
    if (!cinematicModeActive_) {
      cinematicModeActive_ = true;
      Log("cinematic mode entered: frontend=%u faded=%u cutscene=%u processing=%u",
          frontEndMenu ? 1u : 0u, fullyFaded ? 1u : 0u,
          cutsceneRunning ? 1u : 0u, cutsceneProcessing ? 1u : 0u);
    }
    renderTail();
    return false;
  }
  if (cinematicModeActive_) {
    cinematicModeActive_ = false;
    Log("cinematic mode exited; resuming first-person stereo gameplay");
  }
  g_traceStereoFrame = !g_firstStereoFrameTrace.exchange(true);
  if (g_traceStereoFrame) Log("stereo trace: first gameplay frame entered");
  if (!BeginStereoFrame(device)) {
    nativeTailFallback_ = true;
    renderTail();
    nativeTailFallback_ = false;
    return false;
  }
  if (g_traceStereoFrame) Log("stereo trace: BeginStereoFrame completed");
  CaptureBaseCamera();
  BuildArmFrameSnapshot();
  if (g_traceStereoFrame) Log("stereo trace: base camera captured rw=%p", rwCamera_);
  eyeCaptured_[0] = eyeCaptured_[1] = false;
  if (baseFovCaptured_ && baseAspectRatioCaptured_ && rwCamera_ != nullptr) {
    for (uint32_t eye = 0; eye < 2; ++eye) {
      if (g_traceStereoFrame) Log("stereo trace: applying eye=%u", eye);
      if (!ApplyEye(device, eye)) break;
      // All native drawing (weapons, effects, HUD, after-fade overlays) runs
      // with normal screen dimensions. Simulation is before this hook.
      if (g_traceStereoFrame) Log("stereo trace: rendering eye=%u", eye);
      renderTail();
      RestoreArmOverride();
      if (g_traceStereoFrame) Log("stereo trace: rendered eye=%u captured=%u", eye,
                                  eyeCaptured_[eye] ? 1u : 0u);
      if (!eyeCaptured_[eye]) break;
    }
  }
  stereoRendered_ = eyeCaptured_[0] && eyeCaptured_[1] &&
      UploadPixels(gameWidth_ * 2, gameHeight_, stereoPixels_.data());
  if (g_traceStereoFrame) Log("stereo trace: ending frame stereoRendered=%u",
                              stereoRendered_ ? 1u : 0u);
  EndStereoFrame(device);
  RestoreArmOverride();
  if (g_traceStereoFrame) Log("stereo trace: first gameplay frame completed");
  g_traceStereoFrame = false;
  return true;
}

void OpenXrTheater::CaptureRenderedEye(IDirect3DDevice9* device) {
  if (!stereoFrameActive_ || device == nullptr || eyeCaptured_[currentEye_]) return;
  IDirect3DSurface9* backBuffer = nullptr;
  D3DSURFACE_DESC desc{};
  const bool captured = SUCCEEDED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer)) &&
      backBuffer != nullptr && SUCCEEDED(backBuffer->GetDesc(&desc)) &&
      desc.Width == gameWidth_ && desc.Height == gameHeight_ && Upload(backBuffer, desc);
  if (backBuffer != nullptr) backBuffer->Release();
  if (!captured) return;
  stereoPixels_.resize(static_cast<size_t>(gameWidth_) * 2 * gameHeight_ * 4);
  const size_t pitch = static_cast<size_t>(gameWidth_) * 4;
  for (uint32_t row = 0; row < gameHeight_; ++row)
    std::memcpy(stereoPixels_.data() + row * pitch * 2 + currentEye_ * pitch,
                convertedPixels_.data() + row * pitch, pitch);
  eyeCaptured_[currentEye_] = true;
}

bool OpenXrTheater::BeginStereoFrame(IDirect3DDevice9* device) {
  if (!g_stereoRequested || stereoFrameActive_ || frameBegun_ || device == nullptr)
    return false;
  if (g_traceStereoFrame) Log("stereo trace: BeginStereoFrame initialize");
  if (!Initialize(device)) return false;
  if (g_traceStereoFrame) Log("stereo trace: OpenXR initialized");
  if (!PollEvents()) return false;
  if (g_traceStereoFrame) Log("stereo trace: OpenXR events polled");
  if (!StartSessionIfReady()) return false;
  if (g_traceStereoFrame) Log("stereo trace: OpenXR session running");
  if (FAILED(device->GetViewport(&savedViewport_))) {
    viewportSaved_ = false;
  } else {
    viewportSaved_ = true;
  }
  frameState_ = XrFrameState{XR_TYPE_FRAME_STATE};
  XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
  if (!Check(waitFrame_(session_, &waitInfo, &frameState_), "xrWaitFrame(stereo)")) return false;
  if (g_traceStereoFrame) Log("stereo trace: xrWaitFrame completed");
  XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
  if (!Check(beginFrame_(session_, &beginInfo), "xrBeginFrame(stereo)")) return false;
  if (g_traceStereoFrame) Log("stereo trace: xrBeginFrame completed");
  frameBegun_ = true;
  PollInput(frameState_.predictedDisplayTime);
  if (!frameState_.shouldRender || !LocateViews(frameState_.predictedDisplayTime)) {
    ReleasePressedInputs();
    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = frameState_.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = 0;
    endInfo.layers = nullptr;
    endFrame_(session_, &endInfo);
    frameBegun_ = false;
    return false;
  }
  if (g_traceStereoFrame) Log("stereo trace: valid views located");
  stereoFrameActive_ = true;
  stereoRendered_ = false;
  baseCameraCaptured_ = false;
  return true;
}

bool OpenXrTheater::EndStereoFrame(IDirect3DDevice9* device) {
  if (!stereoFrameActive_ || !frameBegun_) return false;
  const bool captured = stereoRendered_;

  std::vector<const XrCompositionLayerBaseHeader*> layers;
  if (captured && stereoRendered_ && RenderProjection()) {
    XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    projection.space = localSpace_;
    XrCompositionLayerProjectionView projectionViews[2] = {
        {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
        {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    for (uint32_t eye = 0; eye < 2; ++eye) {
      projectionViews[eye].pose = views_[eye].pose;
      projectionViews[eye].fov = views_[eye].fov;
      projectionViews[eye].subImage.swapchain = swapchain_;
      projectionViews[eye].subImage.imageRect.offset = {0, 0};
      projectionViews[eye].subImage.imageRect.extent = {
          static_cast<int32_t>(width_), static_cast<int32_t>(height_)};
      // OpenXR projection views and texture-array slices use the same canonical
      // left/right index. Horizontal tracking correction belongs at the camera
      // orientation seam, never in the compositor's eye mapping.
      projectionViews[eye].subImage.imageArrayIndex =
          gtasa_vr::SubmittedArrayIndex(eye, kSwapSubmittedEyes);
    }
    projection.viewCount = 2;
    projection.views = projectionViews;
    layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection));

    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = frameState_.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = static_cast<uint32_t>(layers.size());
    endInfo.layers = layers.data();
    const bool ended = Check(endFrame_(session_, &endInfo), "xrEndFrame(stereo)");
    frameBegun_ = false;
    stereoFrameActive_ = false;
    RestoreGameState(device);
    return ended;
  }

  XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
  endInfo.displayTime = frameState_.predictedDisplayTime;
  endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  endInfo.layerCount = 0;
  endInfo.layers = nullptr;
  const bool ended = Check(endFrame_(session_, &endInfo), "xrEndFrame(stereo-empty)");
  frameBegun_ = false;
  stereoFrameActive_ = false;
  RestoreGameState(device);
  return ended;
}

bool OpenXrTheater::Upload(IDirect3DSurface9* source, const D3DSURFACE_DESC& desc) {
  if (source == nullptr || desc.Width == 0 || desc.Height == 0 ||
      (desc.Format != D3DFMT_A8R8G8B8 && desc.Format != D3DFMT_X8R8G8B8 &&
       desc.Format != D3DFMT_A2R10G10B10))
    return false;

  IDirect3DDevice9* sourceDevice = nullptr;
  if (FAILED(source->GetDevice(&sourceDevice))) return false;
  if (captureDevice_ != sourceDevice) {
    if (resolveSurface_ != nullptr) resolveSurface_->Release();
    if (captureSurface_ != nullptr) captureSurface_->Release();
    if (captureDevice_ != nullptr) captureDevice_->Release();
    resolveSurface_ = nullptr;
    captureSurface_ = nullptr;
    captureDevice_ = sourceDevice;
    captureDevice_->AddRef();
  }
  const bool needsResolve = desc.MultiSampleType != D3DMULTISAMPLE_NONE;
  if (captureSurface_ == nullptr || desc.Width != captureWidth_ || desc.Height != captureHeight_ ||
      desc.Format != sourceFormat_ || (needsResolve && resolveSurface_ == nullptr) ||
      (!needsResolve && resolveSurface_ != nullptr)) {
    if (resolveSurface_ != nullptr) resolveSurface_->Release();
    if (captureSurface_ != nullptr) captureSurface_->Release();
    resolveSurface_ = nullptr;
    captureSurface_ = nullptr;
    if (FAILED(sourceDevice->CreateOffscreenPlainSurface(
            desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &captureSurface_, nullptr))) {
      sourceDevice->Release();
      return false;
    }
    if (needsResolve && FAILED(sourceDevice->CreateRenderTarget(
            desc.Width, desc.Height, desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE,
            &resolveSurface_, nullptr))) {
      captureSurface_->Release();
      captureSurface_ = nullptr;
      sourceDevice->Release();
      Log("failed to create a non-MSAA D3D9 resolve surface for the stereo capture");
      return false;
    }
    captureWidth_ = desc.Width;
    captureHeight_ = desc.Height;
    sourceFormat_ = desc.Format;
  }
  IDirect3DSurface9* sourceForReadback = source;
  if (needsResolve) {
    const HRESULT resolveResult = sourceDevice->StretchRect(
        source, nullptr, resolveSurface_, nullptr, D3DTEXF_NONE);
    if (FAILED(resolveResult)) {
      sourceDevice->Release();
      Log("D3D9 MSAA resolve failed before stereo capture: 0x%08lX",
          static_cast<unsigned long>(resolveResult));
      return false;
    }
    sourceForReadback = resolveSurface_;
  }
  const HRESULT copyResult = sourceDevice->GetRenderTargetData(sourceForReadback, captureSurface_);
  if (FAILED(copyResult)) {
    sourceDevice->Release();
    return false;
  }
  D3DLOCKED_RECT locked{};
  const HRESULT lockResult = captureSurface_->LockRect(&locked, nullptr, D3DLOCK_READONLY);
  if (FAILED(lockResult)) {
    sourceDevice->Release();
    return false;
  }

  const size_t pixelCount = static_cast<size_t>(desc.Width) * desc.Height;
  convertedPixels_.resize(pixelCount * 4);
  for (UINT y = 0; y < desc.Height; ++y) {
    const auto* sourceRow = reinterpret_cast<const uint8_t*>(locked.pBits) + y * locked.Pitch;
    auto* targetRow = convertedPixels_.data() + static_cast<size_t>(y) * desc.Width * 4;
    for (UINT x = 0; x < desc.Width; ++x) {
      if (desc.Format == D3DFMT_A2R10G10B10) {
        uint32_t packed = 0;
        std::memcpy(&packed, sourceRow + x * sizeof(packed), sizeof(packed));
        targetRow[x * 4 + 0] = static_cast<uint8_t>(((packed >> 20u) & 0x3ffu) * 255u / 1023u);
        targetRow[x * 4 + 1] = static_cast<uint8_t>(((packed >> 10u) & 0x3ffu) * 255u / 1023u);
        targetRow[x * 4 + 2] = static_cast<uint8_t>((packed & 0x3ffu) * 255u / 1023u);
        targetRow[x * 4 + 3] = static_cast<uint8_t>(((packed >> 30u) & 0x3u) * 255u / 3u);
      } else {
        const auto* pixel = sourceRow + x * 4;
        targetRow[x * 4 + 0] = pixel[2];
        targetRow[x * 4 + 1] = pixel[1];
        targetRow[x * 4 + 2] = pixel[0];
        targetRow[x * 4 + 3] = 255;
      }
    }
  }
  captureSurface_->UnlockRect();
  sourceDevice->Release();

  if (stereoFrameActive_) return true; // capture-only until both eyes are available
  return UploadPixels(desc.Width, desc.Height, convertedPixels_.data());
}

bool OpenXrTheater::UploadPixels(uint32_t width, uint32_t height, const uint8_t* pixels) {
  if (width == 0 || height == 0 || width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
      height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || pixels == nullptr) return false;
  if (uploadTexture_ == nullptr || width != uploadWidth_ || height != uploadHeight_) {
    if (uploadView_ != nullptr) uploadView_->Release();
    if (uploadTexture_ != nullptr) uploadTexture_->Release();
    uploadView_ = nullptr;
    uploadTexture_ = nullptr;
    D3D11_TEXTURE2D_DESC textureDescription{};
    textureDescription.Width = width;
    textureDescription.Height = height;
    textureDescription.MipLevels = 1;
    textureDescription.ArraySize = 1;
    textureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDescription.SampleDesc.Count = 1;
    textureDescription.Usage = D3D11_USAGE_DEFAULT;
    textureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(d3d11Device_->CreateTexture2D(&textureDescription, nullptr, &uploadTexture_)) ||
        FAILED(d3d11Device_->CreateShaderResourceView(uploadTexture_, nullptr, &uploadView_)))
      return false;
    uploadWidth_ = width;
    uploadHeight_ = height;
  }
  d3d11Context_->UpdateSubresource(uploadTexture_, 0, nullptr, pixels,
                                    width * 4, 0);
  return true;
}

bool OpenXrTheater::Render() {
  uint32_t index = 0;
  XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  if (!Check(acquireSwapchainImage_(swapchain_, &acquireInfo, &index),
             "xrAcquireSwapchainImage"))
    return false;
  XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  waitInfo.timeout = XR_INFINITE_DURATION;
  if (!Check(waitSwapchainImage_(swapchain_, &waitInfo), "xrWaitSwapchainImage")) {
    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    releaseSwapchainImage_(swapchain_, &releaseInfo);
    return false;
  }
  const size_t targetIndex = static_cast<size_t>(index) * (g_stereoRequested ? 2 : 1);
  if (targetIndex >= renderTargets_.size() || renderTargets_[targetIndex] == nullptr) {
    Log("xrAcquireSwapchainImage returned invalid image index=%u", index);
    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    releaseSwapchainImage_(swapchain_, &releaseInfo);
    return false;
  }
  ID3D11RenderTargetView* target = renderTargets_[targetIndex];
  const float clear[] = {0.02f, 0.02f, 0.02f, 1.0f};
  d3d11Context_->ClearRenderTargetView(target, clear);
  D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_),
                          0.0f, 1.0f};
  d3d11Context_->RSSetViewports(1, &viewport);
  d3d11Context_->OMSetRenderTargets(1, &target, nullptr);
  d3d11Context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  d3d11Context_->VSSetShader(vertexShader_, nullptr, 0);
  d3d11Context_->PSSetShader(pixelShader_, nullptr, 0);
  d3d11Context_->PSSetShaderResources(0, 1, &uploadView_);
  d3d11Context_->PSSetSamplers(0, 1, &sampler_);
  const float sourceRect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
  d3d11Context_->UpdateSubresource(blitConstants_, 0, nullptr, sourceRect, sizeof(sourceRect), 0);
  d3d11Context_->PSSetConstantBuffers(0, 1, &blitConstants_);
  d3d11Context_->Draw(4, 0);
  ID3D11ShaderResourceView* nullView = nullptr;
  d3d11Context_->PSSetShaderResources(0, 1, &nullView);
  XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  return Check(releaseSwapchainImage_(swapchain_, &releaseInfo), "xrReleaseSwapchainImage");
}

bool OpenXrTheater::RenderProjection() {
  uint32_t index = 0;
  XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  if (!Check(acquireSwapchainImage_(swapchain_, &acquireInfo, &index),
             "xrAcquireSwapchainImage(stereo)"))
    return false;
  XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  waitInfo.timeout = XR_INFINITE_DURATION;
  if (!Check(waitSwapchainImage_(swapchain_, &waitInfo), "xrWaitSwapchainImage(stereo)")) {
    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    releaseSwapchainImage_(swapchain_, &releaseInfo);
    return false;
  }

  float sourceRects[2][4]{};
  for (uint32_t eye = 0; eye < 2; ++eye) {
    // ApplyRwProjection already renders the exact asymmetric OpenXR frustum.
    // Copy the complete eye image so HUD/UI edges remain visible and readable.
    const auto source = gtasa_vr::FullEyeSourceRect(eye);
    sourceRects[eye][0] = source.u;
    sourceRects[eye][1] = source.v;
    sourceRects[eye][2] = source.width;
    sourceRects[eye][3] = source.height;
  }
  const size_t firstTarget = static_cast<size_t>(index) * 2;
  if (firstTarget + 1 >= renderTargets_.size()) {
    Log("xrAcquireSwapchainImage returned invalid stereo image index=%u", index);
    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    releaseSwapchainImage_(swapchain_, &releaseInfo);
    return false;
  }

  d3d11Context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  d3d11Context_->VSSetShader(vertexShader_, nullptr, 0);
  d3d11Context_->PSSetShader(pixelShader_, nullptr, 0);
  d3d11Context_->PSSetShaderResources(0, 1, &uploadView_);
  d3d11Context_->PSSetSamplers(0, 1, &sampler_);
  d3d11Context_->PSSetConstantBuffers(0, 1, &blitConstants_);
  for (uint32_t eye = 0; eye < 2; ++eye) {
    ID3D11RenderTargetView* target = renderTargets_[firstTarget + eye];
    if (target == nullptr) continue;
    const float clear[] = {0.02f, 0.02f, 0.02f, 1.0f};
    d3d11Context_->ClearRenderTargetView(target, clear);
    D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width_),
                            static_cast<float>(height_), 0.0f, 1.0f};
    d3d11Context_->RSSetViewports(1, &viewport);
    d3d11Context_->OMSetRenderTargets(1, &target, nullptr);
    d3d11Context_->UpdateSubresource(blitConstants_, 0, nullptr, sourceRects[eye],
                                      sizeof(sourceRects[eye]), 0);
    d3d11Context_->Draw(4, 0);
  }
  ID3D11ShaderResourceView* nullView = nullptr;
  d3d11Context_->PSSetShaderResources(0, 1, &nullView);
  XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  return Check(releaseSwapchainImage_(swapchain_, &releaseInfo),
               "xrReleaseSwapchainImage(stereo)");
}

bool OpenXrTheater::Submit(IDirect3DDevice9* device) {
  if (!g_bridgeEnabled.load() || g_shutdown.load()) return false;
  if (!Initialize(device)) return false;
  if (!PollEvents() || !StartSessionIfReady()) return false;

  XrFrameState frameState{XR_TYPE_FRAME_STATE};
  XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
  if (!Check(waitFrame_(session_, &waitInfo, &frameState), "xrWaitFrame")) return false;
  XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
  if (!Check(beginFrame_(session_, &beginInfo), "xrBeginFrame")) return false;
  frameBegun_ = true;

  IDirect3DSurface9* backBuffer = nullptr;
  D3DSURFACE_DESC desc{};
  PollInput(frameState.predictedDisplayTime);
  const bool captured = frameState.shouldRender &&
      SUCCEEDED(device->GetRenderTarget(0, &backBuffer)) && backBuffer != nullptr &&
      SUCCEEDED(backBuffer->GetDesc(&desc)) && Upload(backBuffer, desc);
  if (backBuffer != nullptr) backBuffer->Release();

  std::vector<const XrCompositionLayerBaseHeader*> layers;
  XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
  XrSwapchainSubImage subImage{};
  XrRect2Di rect{};
  rect.extent.width = static_cast<int32_t>(width_);
  rect.extent.height = static_cast<int32_t>(height_);
  subImage.swapchain = swapchain_;
  subImage.imageRect = rect;
  quad.space = viewSpace_;
  quad.layerFlags = 0;
  quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
  quad.subImage = subImage;
  quad.pose.orientation.w = 1.0f;
  quad.pose.position.z = -kCinematicPanelDistance;
  quad.size.width = kCinematicPanelWidth;
  quad.size.height = captured
      ? kCinematicPanelWidth * static_cast<float>(desc.Height) / desc.Width
      : kCinematicPanelWidth * 9.0f / 16.0f;
  if (captured && Render()) layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad));

  XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
  endInfo.displayTime = frameState.predictedDisplayTime;
  endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  endInfo.layerCount = static_cast<uint32_t>(layers.size());
  endInfo.layers = layers.data();
  const bool ended = Check(endFrame_(session_, &endInfo), "xrEndFrame");
  frameBegun_ = false;
  return ended;
}

void OpenXrTheater::ReleaseGraphics() {
  for (auto* target : renderTargets_)
    if (target != nullptr) target->Release();
  renderTargets_.clear();
  if (uploadView_ != nullptr) uploadView_->Release();
  if (uploadTexture_ != nullptr) uploadTexture_->Release();
  if (sampler_ != nullptr) sampler_->Release();
  if (blitConstants_ != nullptr) blitConstants_->Release();
  if (vertexShader_ != nullptr) vertexShader_->Release();
  if (pixelShader_ != nullptr) pixelShader_->Release();
  if (d3d11Context_ != nullptr) d3d11Context_->Release();
  if (d3d11Device_ != nullptr) d3d11Device_->Release();
  if (captureSurface_ != nullptr) captureSurface_->Release();
  if (resolveSurface_ != nullptr) resolveSurface_->Release();
  if (captureDevice_ != nullptr) captureDevice_->Release();
  uploadView_ = nullptr;
  uploadTexture_ = nullptr;
  sampler_ = nullptr;
  blitConstants_ = nullptr;
  vertexShader_ = nullptr;
  pixelShader_ = nullptr;
  d3d11Context_ = nullptr;
  d3d11Device_ = nullptr;
  captureSurface_ = nullptr;
  resolveSurface_ = nullptr;
  captureDevice_ = nullptr;
  captureWidth_ = 0;
  captureHeight_ = 0;
  uploadWidth_ = 0;
  uploadHeight_ = 0;
  sourceFormat_ = D3DFMT_UNKNOWN;
}

void OpenXrTheater::ReleaseOpenXrResources(bool skipSessionTermination) {
  const XrSessionState stateBeforeRelease = sessionState_;
  ReleaseInput();
  snapTurn_={};
  if (frameBegun_ && !skipSessionTermination && endFrame_ != nullptr &&
      session_ != XR_NULL_HANDLE) {
    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = 0;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = 0;
    endInfo.layers = nullptr;
    endFrame_(session_, &endInfo);
  }
  if (stereoFrameActive_)
    RestoreGameState(g_d3d9Device);
  frameBegun_ = false;
  cinematicModeActive_ = false;
  if (sessionRunning_ && !skipSessionTermination && endSession_ != nullptr &&
      session_ != XR_NULL_HANDLE &&
      stateBeforeRelease != XR_SESSION_STATE_LOSS_PENDING &&
      stateBeforeRelease != XR_SESSION_STATE_EXITING)
    endSession_(session_);
  sessionRunning_ = false;
  if (swapchain_ != XR_NULL_HANDLE && destroySwapchain_ != nullptr)
    destroySwapchain_(swapchain_);
  if (viewSpace_ != XR_NULL_HANDLE && destroySpace_ != nullptr)
    destroySpace_(viewSpace_);
  if (localSpace_ != XR_NULL_HANDLE && destroySpace_ != nullptr)
    destroySpace_(localSpace_);
  if (session_ != XR_NULL_HANDLE && destroySession_ != nullptr)
    destroySession_(session_);
  if (instance_ != XR_NULL_HANDLE && destroyInstance_ != nullptr)
    destroyInstance_(instance_);

  ReleaseGraphics();
  if (loader_ != nullptr) FreeLibrary(loader_);

  loader_ = nullptr;
  getInstanceProcAddr_ = nullptr;
  enumerateInstanceExtensionProperties_ = nullptr;
  createInstance_ = nullptr;
  getSystem_ = nullptr;
  pollEvent_ = nullptr;
  destroyInstance_ = nullptr;
  getD3D11GraphicsRequirements_ = nullptr;
  createSession_ = nullptr;
  destroySession_ = nullptr;
  beginSession_ = nullptr;
  endSession_ = nullptr;
  createReferenceSpace_ = nullptr;
  createActionSpace_ = nullptr;
  destroySpace_ = nullptr;
  locateSpace_ = nullptr;
  locateViews_ = nullptr;
  enumerateViewConfigurationViews_ = nullptr;
  enumerateSwapchainFormats_ = nullptr;
  createSwapchain_ = nullptr;
  destroySwapchain_ = nullptr;
  enumerateSwapchainImages_ = nullptr;
  acquireSwapchainImage_ = nullptr;
  waitSwapchainImage_ = nullptr;
  releaseSwapchainImage_ = nullptr;
  waitFrame_ = nullptr;
  beginFrame_ = nullptr;
  endFrame_ = nullptr;
  stringToPath_ = nullptr;
  createActionSet_ = nullptr;
  destroyActionSet_ = nullptr;
  createAction_ = nullptr;
  destroyAction_ = nullptr;
  suggestInteractionProfileBindings_ = nullptr;
  attachSessionActionSets_ = nullptr;
  syncActions_ = nullptr;
  getActionStateBoolean_ = nullptr;
  getActionStateFloat_ = nullptr;
  getActionStateVector2f_ = nullptr;
  getActionStatePose_ = nullptr;

  instance_ = XR_NULL_HANDLE;
  system_ = XR_NULL_SYSTEM_ID;
  session_ = XR_NULL_HANDLE;
  viewSpace_ = XR_NULL_HANDLE;
  localSpace_ = XR_NULL_HANDLE;
  swapchain_ = XR_NULL_HANDLE;
  swapchainFormat_ = 0;
  sessionState_ = XR_SESSION_STATE_UNKNOWN;
  initialized_ = false;
  stereoFrameActive_ = false;
  stereoRendered_ = false;
  baseCameraCaptured_ = false;
  baseFovCaptured_ = false;
  baseAspectRatioCaptured_ = false;
  viewportSaved_ = false;
  referencePoseCaptured_ = false;
  armFrame_ = {};
  armFrameSerial_ = 0;
  width_ = 0;
  height_ = 0;
  gameWidth_ = 0;
  gameHeight_ = 0;
  gameEyeWidth_ = 0;
  views_.clear();
  images_.clear();
}

bool OpenXrTheater::FailInitialization() {
  ReleaseOpenXrResources();
  constexpr ULONGLONG retryDelayMs = 2000;
  nextInitializationAttemptMs_ = GetTickCount64() + retryDelayMs;
  Log("OpenXR initialization failed; retrying in %llu ms when the runtime is available",
      retryDelayMs);
  return false;
}

void OpenXrTheater::Shutdown() {
  std::lock_guard lock(mutex_);
  ReleaseOpenXrResources();
  initializationAttempted_ = false;
  nextInitializationAttemptMs_ = 0;
}

class ScopedVrHudCluster {
 public:
  explicit ScopedVrHudCluster(gtasa_vr::HudCluster cluster) : previous_(g_vrHudCluster) {
    if (g_stereoRequested && g_theater.IsStereoFrameActive()) g_vrHudCluster = cluster;
  }
  ~ScopedVrHudCluster() { g_vrHudCluster = previous_; }
 private:
  gtasa_vr::HudCluster previous_;
};

void __cdecl HookedHudPlayerInfo() {
  ScopedVrHudCluster scope(gtasa_vr::HudCluster::PlayerStatus);
  reinterpret_cast<VoidFn>(g_hudPlayerInfoTrampoline)();
}

void __cdecl HookedHudWanted() {
  ScopedVrHudCluster scope(gtasa_vr::HudCluster::PlayerStatus);
  reinterpret_cast<VoidFn>(g_hudWantedTrampoline)();
}

void __cdecl HookedHudRadar() {
  ScopedVrHudCluster scope(gtasa_vr::HudCluster::Radar);
  reinterpret_cast<VoidFn>(g_hudRadarTrampoline)();
}

void __cdecl HookedHudVitalStats() {
  ScopedVrHudCluster scope(gtasa_vr::HudCluster::VitalStats);
  reinterpret_cast<VoidFn>(g_hudVitalStatsTrampoline)();
}

gtasa_vr::HudPoint CurrentVrHudPoint(float x, float y) {
  if (g_vrHudCluster == gtasa_vr::HudCluster::None || g_d3d9Device == nullptr) return {x,y};
  D3DVIEWPORT9 viewport{};
  if (FAILED(g_d3d9Device->GetViewport(&viewport)) || viewport.Width == 0 || viewport.Height == 0)
    return {x,y};
  const auto point = gtasa_vr::VrHudPoint(
      g_vrHudCluster, static_cast<float>(viewport.Width), static_cast<float>(viewport.Height), x, y);
  return {point.x + static_cast<float>(viewport.X), point.y + static_cast<float>(viewport.Y)};
}

void __cdecl HookedFontPrintString(float x, float y, const void* text) {
  using FontPrintString = void(__cdecl*)(float, float, const void*);
  const auto point = CurrentVrHudPoint(x,y);
  reinterpret_cast<FontPrintString>(g_fontPrintStringTrampoline)(point.x,point.y,text);
}

bool PrimitiveVertexCount(D3DPRIMITIVETYPE type, UINT primitiveCount, UINT& vertexCount) {
  switch (type) {
    case D3DPT_POINTLIST: vertexCount=primitiveCount; return true;
    case D3DPT_LINELIST:
      if (primitiveCount>UINT_MAX/2) return false;
      vertexCount=primitiveCount*2; return true;
    case D3DPT_LINESTRIP:
      if (primitiveCount==UINT_MAX) return false;
      vertexCount=primitiveCount+1; return true;
    case D3DPT_TRIANGLELIST:
      if (primitiveCount>UINT_MAX/3) return false;
      vertexCount=primitiveCount*3; return true;
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN:
      if (primitiveCount>UINT_MAX-2) return false;
      vertexCount=primitiveCount+2; return true;
    default: return false;
  }
}

bool TransformVrHudVertices(IDirect3DDevice9* device,const void* vertices,UINT vertexCount,
                            UINT stride,std::vector<uint8_t>& transformed) {
  if (g_vrHudCluster==gtasa_vr::HudCluster::None || device==nullptr || vertices==nullptr ||
      vertexCount==0 || stride<sizeof(float)*4 || vertexCount>SIZE_MAX/stride)
    return false;
  D3DVIEWPORT9 viewport{};
  if (FAILED(device->GetViewport(&viewport)) || viewport.Width==0 || viewport.Height==0) return false;
  DWORD fvf=0;
  if (SUCCEEDED(device->GetFVF(&fvf)) && fvf!=0 &&
      (fvf&D3DFVF_POSITION_MASK)!=D3DFVF_XYZRHW)
    return false;
  const size_t bytes=static_cast<size_t>(vertexCount)*stride;
  transformed.resize(bytes);
  std::memcpy(transformed.data(),vertices,bytes);
  for (UINT index=0;index<vertexCount;++index) {
    auto* vertex=transformed.data()+static_cast<size_t>(index)*stride;
    float x=0,y=0,rhw=0;
    std::memcpy(&x,vertex,sizeof(x));
    std::memcpy(&y,vertex+sizeof(float),sizeof(y));
    std::memcpy(&rhw,vertex+sizeof(float)*3,sizeof(rhw));
    if (!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(rhw)||rhw<=0.0f||
        std::abs(x)>1000000.0f||std::abs(y)>1000000.0f) {
      transformed.clear();
      return false;
    }
    const auto point=gtasa_vr::VrHudPoint(
        g_vrHudCluster,static_cast<float>(viewport.Width),static_cast<float>(viewport.Height),
        x-static_cast<float>(viewport.X),y-static_cast<float>(viewport.Y));
    x=point.x+static_cast<float>(viewport.X);
    y=point.y+static_cast<float>(viewport.Y);
    std::memcpy(vertex,&x,sizeof(x));
    std::memcpy(vertex+sizeof(float),&y,sizeof(y));
  }
  return true;
}

HRESULT STDMETHODCALLTYPE HookedDrawPrimitiveUp(IDirect3DDevice9* device,D3DPRIMITIVETYPE type,
                                                 UINT primitiveCount,const void* vertices,
                                                 UINT stride) {
  if (g_realDrawPrimitiveUp==nullptr) return D3DERR_INVALIDCALL;
  UINT vertexCount=0;
  std::vector<uint8_t> transformed;
  if (PrimitiveVertexCount(type,primitiveCount,vertexCount) &&
      TransformVrHudVertices(device,vertices,vertexCount,stride,transformed))
    vertices=transformed.data();
  return g_realDrawPrimitiveUp(device,type,primitiveCount,vertices,stride);
}

HRESULT STDMETHODCALLTYPE HookedDrawIndexedPrimitiveUp(
    IDirect3DDevice9* device,D3DPRIMITIVETYPE type,UINT minVertexIndex,UINT numVertices,
    UINT primitiveCount,const void* indexData,D3DFORMAT indexFormat,const void* vertices,
    UINT stride) {
  if (g_realDrawIndexedPrimitiveUp==nullptr) return D3DERR_INVALIDCALL;
  std::vector<uint8_t> transformed;
  if (minVertexIndex<=UINT_MAX-numVertices && TransformVrHudVertices(
      device,vertices,minVertexIndex+numVertices,stride,transformed))
    vertices=transformed.data();
  return g_realDrawIndexedPrimitiveUp(device,type,minVertexIndex,numVertices,primitiveCount,
                                      indexData,indexFormat,vertices,stride);
}

IDirect3D9* WINAPI HookedDirect3DCreate9(UINT sdkVersion) {
  if (g_realDirect3DCreate9 == nullptr) return nullptr;
  IDirect3D9* api = g_realDirect3DCreate9(sdkVersion);
  if (api == nullptr) return api;
  void** table = *reinterpret_cast<void***>(api);
  if (table == nullptr) {
    Log("failed to hook IDirect3D9::CreateDevice");
    return api;
  }
  auto* slot = reinterpret_cast<CreateDeviceFn*>(&table[16]);
  if (*slot == reinterpret_cast<CreateDeviceFn>(&HookedCreateDevice)) return api;
  CreateDeviceFn original = nullptr;
  if (!PatchPointer(slot, reinterpret_cast<CreateDeviceFn>(&HookedCreateDevice), original)) {
    Log("failed to hook IDirect3D9::CreateDevice");
    return api;
  }
  if (g_realCreateDevice == nullptr) {
    g_realCreateDevice = original;
    if (g_createDeviceImplementationTrampoline == nullptr) {
      if (InstallInlineHook(reinterpret_cast<uintptr_t>(original),
                             g_config.createDeviceExpected.data(),
                             g_config.createDeviceExpected.size(),
                             reinterpret_cast<void*>(&HookedCreateDevice),
                             &g_createDeviceImplementationTrampoline)) {
        g_realCreateDevice = reinterpret_cast<CreateDeviceFn>(
            g_createDeviceImplementationTrampoline);
        Log("hooked D3D9 CreateDevice implementation at %p; existing ASI plugins remain loaded",
            original);
      }
    }
  } else if (g_realCreateDevice != original) {
    Log("IDirect3D9::CreateDevice implementation changed between interface objects");
  }
  if (!g_d3d9Hooked.exchange(true))
    Log("hooked IDirect3D9::CreateDevice api=%p vtable=%p original=%p", api, table, original);
  else
    Log("hooked another IDirect3D9::CreateDevice interface object");

  IDirect3D9Ex* apiEx = nullptr;
  if (SUCCEEDED(api->QueryInterface(IID_IDirect3D9Ex,
                                    reinterpret_cast<void**>(&apiEx))) && apiEx != nullptr) {
    void** exTable = *reinterpret_cast<void***>(apiEx);
    auto* exSlot = reinterpret_cast<CreateDeviceExFn*>(&exTable[20]);
    if (*exSlot != reinterpret_cast<CreateDeviceExFn>(&HookedCreateDeviceEx)) {
      CreateDeviceExFn exOriginal = nullptr;
      if (PatchPointer(exSlot, reinterpret_cast<CreateDeviceExFn>(&HookedCreateDeviceEx),
                       exOriginal)) {
        if (g_realCreateDeviceEx == nullptr) g_realCreateDeviceEx = exOriginal;
        Log("hooked IDirect3D9Ex::CreateDeviceEx api=%p vtable=%p original=%p", apiEx, exTable,
            exOriginal);
      } else {
        Log("failed to hook IDirect3D9Ex::CreateDeviceEx");
      }
    }
    apiEx->Release();
  }
  return api;
}

HRESULT STDMETHODCALLTYPE HookedCreateDevice(
    IDirect3D9* self, UINT adapter, D3DDEVTYPE type, HWND window, DWORD behavior,
    D3DPRESENT_PARAMETERS* parameters, IDirect3DDevice9** device) {
  if (!g_createDeviceObserved.exchange(true))
    Log("D3D9 CreateDevice entered");
  if (g_realCreateDevice == nullptr) return D3DERR_INVALIDCALL;
  const HRESULT result = g_realCreateDevice(self, adapter, type, window, behavior, parameters,
                                            device);
  Log("D3D9 CreateDevice returned 0x%08lX device=%p", static_cast<unsigned long>(result),
      device == nullptr ? nullptr : *device);
  if (SUCCEEDED(result) && device != nullptr && *device != nullptr &&
      !g_deviceHooked.exchange(true)) {
    g_d3d9Device = *device;
    void** table = *reinterpret_cast<void***>(*device);
    const bool present = table != nullptr &&
        PatchPointer(reinterpret_cast<PresentFn*>(&table[17]),
                     reinterpret_cast<PresentFn>(&HookedPresent), g_realPresent);
    const bool reset = table != nullptr &&
        PatchPointer(reinterpret_cast<ResetFn*>(&table[16]), reinterpret_cast<ResetFn>(&HookedReset),
                     g_realReset);
    const bool drawPrimitiveUp = table != nullptr &&
        PatchPointer(reinterpret_cast<DrawPrimitiveUpFn*>(&table[83]),
                     reinterpret_cast<DrawPrimitiveUpFn>(&HookedDrawPrimitiveUp),
                     g_realDrawPrimitiveUp);
    const bool drawIndexedPrimitiveUp = table != nullptr &&
        PatchPointer(reinterpret_cast<DrawIndexedPrimitiveUpFn*>(&table[84]),
                     reinterpret_cast<DrawIndexedPrimitiveUpFn>(&HookedDrawIndexedPrimitiveUp),
                     g_realDrawIndexedPrimitiveUp);
    g_vrHudDrawHooksInstalled = drawPrimitiveUp && drawIndexedPrimitiveUp;
    if (!present || !reset) {
      g_deviceHooked.store(false);
      Log("failed to hook D3D9 device lifecycle");
    } else {
      Log("hooked D3D9 Present/Reset; existing ASI plugins remain loaded");
      Log(g_vrHudDrawHooksInstalled
          ? "hooked D3D9 immediate drawing for the VR HUD safe area"
          : "VR HUD safe area disabled: D3D9 immediate drawing hooks unavailable");
    }
  }
  return result;
}

HRESULT STDMETHODCALLTYPE HookedCreateDeviceEx(
    IDirect3D9Ex* self, UINT adapter, D3DDEVTYPE type, HWND window, DWORD behavior,
    D3DPRESENT_PARAMETERS* parameters, D3DDISPLAYMODEEX* fullscreenDisplayMode,
    IDirect3DDevice9Ex** device) {
  if (!g_createDeviceObserved.exchange(true))
    Log("D3D9Ex CreateDeviceEx entered");
  if (g_realCreateDeviceEx == nullptr) return D3DERR_INVALIDCALL;
  const HRESULT result = g_realCreateDeviceEx(self, adapter, type, window, behavior, parameters,
                                               fullscreenDisplayMode, device);
  Log("D3D9Ex CreateDeviceEx returned 0x%08lX device=%p", static_cast<unsigned long>(result),
      device == nullptr ? nullptr : *device);
  if (SUCCEEDED(result) && device != nullptr && *device != nullptr &&
      !g_deviceHooked.exchange(true)) {
    g_d3d9Device = *device;
    void** table = *reinterpret_cast<void***>(*device);
    const bool present = table != nullptr &&
        PatchPointer(reinterpret_cast<PresentFn*>(&table[17]),
                     reinterpret_cast<PresentFn>(&HookedPresent), g_realPresent);
    const bool reset = table != nullptr &&
        PatchPointer(reinterpret_cast<ResetFn*>(&table[16]), reinterpret_cast<ResetFn>(&HookedReset),
                     g_realReset);
    const bool drawPrimitiveUp = table != nullptr &&
        PatchPointer(reinterpret_cast<DrawPrimitiveUpFn*>(&table[83]),
                     reinterpret_cast<DrawPrimitiveUpFn>(&HookedDrawPrimitiveUp),
                     g_realDrawPrimitiveUp);
    const bool drawIndexedPrimitiveUp = table != nullptr &&
        PatchPointer(reinterpret_cast<DrawIndexedPrimitiveUpFn*>(&table[84]),
                     reinterpret_cast<DrawIndexedPrimitiveUpFn>(&HookedDrawIndexedPrimitiveUp),
                     g_realDrawIndexedPrimitiveUp);
    g_vrHudDrawHooksInstalled = drawPrimitiveUp && drawIndexedPrimitiveUp;
    if (!present || !reset) {
      g_deviceHooked.store(false);
      Log("failed to hook D3D9Ex device lifecycle");
    } else {
      Log("hooked D3D9Ex Present/Reset; existing ASI plugins remain loaded");
      Log(g_vrHudDrawHooksInstalled
          ? "hooked D3D9Ex immediate drawing for the VR HUD safe area"
          : "VR HUD safe area disabled: D3D9Ex immediate drawing hooks unavailable");
    }
  }
  return result;
}

HRESULT STDMETHODCALLTYPE HookedReset(IDirect3DDevice9* device,
                                       D3DPRESENT_PARAMETERS* parameters) {
  g_theater.Shutdown();
  return g_realReset == nullptr ? D3DERR_INVALIDCALL : g_realReset(device, parameters);
}

bool FrontEndMenuIsActive() {
  return g_config.frontEndMenuActive != 0 &&
      *reinterpret_cast<const uint8_t*>(g_config.frontEndMenuActive) != 0;
}

HRESULT STDMETHODCALLTYPE HookedPresent(IDirect3DDevice9* device, const RECT* sourceRect,
                                        const RECT* destinationRect, HWND destinationWindow,
                                        const RGNDATA* dirtyRegion) {
  if (!g_presentObserved.exchange(true))
    Log("D3D9 Present entered");
  static thread_local bool reentrant = false;
  if (!reentrant && g_bridgeEnabled.load()) {
    reentrant = true;
    if (g_stereoRequested && g_theater.IsStereoFrameActive()) {
      g_theater.CaptureRenderedEye(device);
      reentrant = false;
      return D3D_OK;
    } else if (g_stereoRequested && !g_theater.IsNativeTailFallback()) {
      static bool hooksAttempted = false;
      if (!hooksAttempted && !IsStereoSeamHookDisabled()) {
        hooksAttempted = true;
        if (!InstallStereoHooks()) {
          Log("stereo conversion disabled: full-frame hooks could not be installed");
          g_bridgeEnabled.store(false);
          reentrant = false;
          return g_realPresent(device, sourceRect, destinationRect, destinationWindow, dirtyRegion);
        }
      }
      // ActiveMovie owns D3D Present during startup movies, before GTA has
      // initialized its normal frame loop. The frontend flag becomes active
      // only after that phase and lets the initial menu enter VR as a quad;
      // gameplay remains gated on the first verified RenderWare tail.
      if (g_renderTailObserved.load() || FrontEndMenuIsActive()) g_theater.Submit(device);
    } else if (!g_stereoRequested) {
      g_theater.Submit(device);
    }
    reentrant = false;
  }
  return g_realPresent == nullptr
      ? D3DERR_INVALIDCALL
      : g_realPresent(device, sourceRect, destinationRect, destinationWindow, dirtyRegion);
}

// Idle has already advanced simulation and popped ESI at the configured render tail.
// Its remaining render-only tail owns eight bytes of scratch stack and RETs
// after ADD ESP,8. These inverse shims preserve the original caller's stack.
__declspec(naked) void __cdecl CallOriginalRenderTail() {
  __asm {
    sub esp, 8
    jmp dword ptr [g_renderTailTrampoline]
  }
}

void __cdecl HookedRenderFrame() {
  if (!g_renderTailObserved.exchange(true))
    Log("GTA RenderWare tail observed; OpenXR startup is now allowed");
  if (!g_bridgeEnabled.load() || !g_stereoHooksInstalled || g_shutdown.load() ||
      g_d3d9Device == nullptr) {
    CallOriginalRenderTail();
    return;
  }
  const bool replayed = g_theater.RenderCompleteFrame(g_d3d9Device, &CallOriginalRenderTail);
  // Menus and failed XR starts already presented their ordinary game frame.
  if (replayed && g_realPresent != nullptr)
    g_realPresent(g_d3d9Device, nullptr, nullptr, nullptr, nullptr);
}

__declspec(naked) void __cdecl HookedRenderTail() {
  __asm {
    add esp, 8
    jmp HookedRenderFrame
  }
}

void __cdecl HookedCameraSize(void* camera, void* rect, float viewWindow, float aspect) {
  using CameraSize = void(__cdecl*)(void*, void*, float, float);
  if (g_cameraSizeTrampoline != nullptr)
    reinterpret_cast<CameraSize>(g_cameraSizeTrampoline)(camera, rect, viewWindow, aspect);
  g_theater.ApplyRwProjection(camera);
}

void __fastcall HookedPedPreRender(void* ped, void*) {
  using PedPreRender = void(__thiscall*)(void*);
  if (g_pedPreRenderTrampoline != nullptr)
    reinterpret_cast<PedPreRender>(g_pedPreRenderTrampoline)(ped);
  g_theater.AfterPedPreRender(ped);
}

bool InstallVrHudHooks() {
  if (g_vrHudFunctionHooksInstalled) return true;
  if (!g_vrHudDrawHooksInstalled) return false;
  struct HookSpec {
    uintptr_t address;
    const std::vector<uint8_t>* expected;
    void* replacement;
    void** trampoline;
  };
  const HookSpec hooks[] = {
      {g_config.hudPlayerInfo,&g_config.hudPlayerInfoExpected,
       reinterpret_cast<void*>(&HookedHudPlayerInfo),&g_hudPlayerInfoTrampoline},
      {g_config.hudWanted,&g_config.hudWantedExpected,
       reinterpret_cast<void*>(&HookedHudWanted),&g_hudWantedTrampoline},
      {g_config.hudRadar,&g_config.hudRadarExpected,
       reinterpret_cast<void*>(&HookedHudRadar),&g_hudRadarTrampoline},
      {g_config.hudVitalStats,&g_config.hudVitalStatsExpected,
       reinterpret_cast<void*>(&HookedHudVitalStats),&g_hudVitalStatsTrampoline},
      {g_config.fontPrintString,&g_config.fontPrintStringExpected,
       reinterpret_cast<void*>(&HookedFontPrintString),&g_fontPrintStringTrampoline},
  };
  for (const auto& hook:hooks) {
    if (hook.address==0 || hook.expected==nullptr || hook.expected->size()<5 ||
        std::memcmp(reinterpret_cast<const void*>(hook.address),hook.expected->data(),
                    hook.expected->size())!=0) {
      Log("VR HUD safe area disabled: hook contract conflict at 0x%08Ix",hook.address);
      return false;
    }
  }
  size_t installed=0;
  for (;installed<std::size(hooks);++installed) {
    const auto& hook=hooks[installed];
    if (!InstallInlineHook(hook.address,hook.expected->data(),hook.expected->size(),
                           hook.replacement,hook.trampoline))
      break;
  }
  if (installed!=std::size(hooks)) {
    while (installed>0) {
      --installed;
      const auto& hook=hooks[installed];
      RemoveInlineHook(hook.address,hook.expected->data(),hook.expected->size(),hook.trampoline);
    }
    Log("VR HUD safe area disabled: targeted HUD hooks could not be installed");
    return false;
  }
  g_vrHudFunctionHooksInstalled=true;
  Log("VR HUD safe area active: radar/vitals upper-left, player status/wanted upper-right");
  return true;
}

bool InstallStereoHooks() {
  if (!g_stereoRequested || g_stereoHooksInstalled) return true;
  if (g_config.gameSha256 != "a559aa772fd136379155efa71f00c47aad34bbfeae6196b0fe1047d0645cbd26")
    return false;
  // Hooks are installed by Present on the game render thread, after this
  // frame's camera/drawing calls have returned, never by the loader worker.
  if (std::memcmp(reinterpret_cast<void*>(g_config.renderTailReturn),
                  g_config.renderTailReturnExpected.data(),
                  g_config.renderTailReturnExpected.size()) != 0 ||
      std::memcmp(reinterpret_cast<void*>(g_config.renderTail),
                  g_config.renderTailExpected.data(),
                  g_config.renderTailExpected.size()) != 0 ||
      std::memcmp(reinterpret_cast<void*>(g_config.cameraSize),
                  g_config.cameraSizeExpected.data(),
                  g_config.cameraSizeExpected.size()) != 0 ||
      std::memcmp(reinterpret_cast<void*>(g_config.pedPreRender),
                  g_config.pedPreRenderExpected.data(),
                  g_config.pedPreRenderExpected.size()) != 0) {
    Log("full-frame stereo contract conflicts with this executable/mod set; refusing hook");
    return false;
  }
  if (!InstallInlineHook(g_config.pedPreRender, g_config.pedPreRenderExpected.data(),
                         g_config.pedPreRenderExpected.size(),
                         reinterpret_cast<void*>(&HookedPedPreRender),
                         &g_pedPreRenderTrampoline))
    return false;
  if (!InstallInlineHook(g_config.cameraSize, g_config.cameraSizeExpected.data(),
                          g_config.cameraSizeExpected.size(),
                          reinterpret_cast<void*>(&HookedCameraSize), &g_cameraSizeTrampoline)) {
    RemoveInlineHook(g_config.pedPreRender, g_config.pedPreRenderExpected.data(),
                     g_config.pedPreRenderExpected.size(), &g_pedPreRenderTrampoline);
    return false;
  }
  if (!InstallInlineHook(g_config.renderTail, g_config.renderTailExpected.data(),
                          g_config.renderTailExpected.size(),
                          reinterpret_cast<void*>(&HookedRenderTail), &g_renderTailTrampoline)) {
    RemoveInlineHook(g_config.cameraSize, g_config.cameraSizeExpected.data(),
                     g_config.cameraSizeExpected.size(), &g_cameraSizeTrampoline);
    RemoveInlineHook(g_config.pedPreRender, g_config.pedPreRenderExpected.data(),
                     g_config.pedPreRenderExpected.size(), &g_pedPreRenderTrampoline);
    return false;
  }
  g_stereoHooksInstalled = true;
  if (!InstallVrHudHooks())
    Log("continuing stereo without VR HUD relocation; crosshair and cinematic UI remain unchanged");
  Log("full-frame stereo and player-arm hooks installed: simulation/IK snapshot once, complete drawing per eye");
  return true;
}

bool HookD3D9Import() {
  auto* image = reinterpret_cast<uint8_t*>(GetModuleHandleA(nullptr));
  if (image == nullptr) return false;
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != kExpectedMachine)
    return false;

  const size_t imageSize = nt->OptionalHeader.SizeOfImage;
  auto imagePointer = [&](uint32_t value, size_t size, bool isRva) -> uint8_t* {
    if (isRva) {
      if (value > imageSize || size > imageSize - value) return nullptr;
      return image + value;
    }
    if (value < g_config.imageBase || value - g_config.imageBase > imageSize ||
        size > imageSize - (value - g_config.imageBase)) {
      return nullptr;
    }
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(value));
  };

  auto hookThunkTable = [&](uint32_t moduleNameValue, uint32_t nameTableValue,
                            uint32_t addressTableValue, bool isRva,
                            const char* tableKind) -> bool {
    const char* moduleName = reinterpret_cast<const char*>(
        imagePointer(moduleNameValue, sizeof(char), isRva));
    auto* names = reinterpret_cast<IMAGE_THUNK_DATA32*>(
        imagePointer(nameTableValue, sizeof(IMAGE_THUNK_DATA32), isRva));
    auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA32*>(
        imagePointer(addressTableValue, sizeof(IMAGE_THUNK_DATA32), isRva));
    if (moduleName == nullptr || names == nullptr || addresses == nullptr ||
        _stricmp(moduleName, "d3d9.dll") != 0) {
      return false;
    }
    const size_t maxThunkCount = imageSize / sizeof(IMAGE_THUNK_DATA32);
    for (size_t index = 0; index < maxThunkCount; ++index) {
      const auto& nameThunk = names[index];
      if (nameThunk.u1.AddressOfData == 0) break;
      if (IMAGE_SNAP_BY_ORDINAL32(nameThunk.u1.Ordinal)) continue;
      auto* import = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(imagePointer(
          nameThunk.u1.AddressOfData, sizeof(IMAGE_IMPORT_BY_NAME), isRva));
      if (import == nullptr ||
          std::strcmp(reinterpret_cast<const char*>(import->Name), "Direct3DCreate9") != 0)
        continue;

      if (std::strcmp(tableKind, "delayed") == 0) {
        // The exact-build thunk is data-driven in the adjacent native profile.
        // Patching only the delayed IAT is insufficient: this linker-generated
        // thunk resolves the import and jumps to the resolved function before
        // the IAT entry is consulted again.
        if (!InstallInlineHook(g_config.d3d9DelayThunk,
                               g_config.d3d9DelayThunkExpected.data(),
                               g_config.d3d9DelayThunkExpected.size(),
                               reinterpret_cast<void*>(&HookedDirect3DCreate9),
                               &g_direct3DCreate9Trampoline)) {
          Log("failed to hook the GTA SA D3D9 delay-load thunk");
          return false;
        }
        g_realDirect3DCreate9 =
            reinterpret_cast<Direct3DCreate9Fn>(g_direct3DCreate9Trampoline);
        g_d3d9ImportHooked.store(true);
        Log("hooked gta_sa.exe D3D9 delayed import thunk at 0x%08Ix without replacing any game or mod file",
            g_config.d3d9DelayThunk);
        return true;
      }

      auto* slot = &addresses[index].u1.Function;
      if (!PatchPointer(reinterpret_cast<Direct3DCreate9Fn*>(slot),
                        &HookedDirect3DCreate9, g_realDirect3DCreate9)) {
        return false;
      }
      g_d3d9ImportHooked.store(true);
      Log("hooked gta_sa.exe D3D9 %s import without replacing any game or mod file",
          tableKind);
      return true;
    }
    return false;
  };

  const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (directory.VirtualAddress != 0) {
    auto* imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
        imagePointer(directory.VirtualAddress, sizeof(IMAGE_IMPORT_DESCRIPTOR), true));
    if (imports != nullptr) {
      const size_t maxImportCount = imageSize / sizeof(IMAGE_IMPORT_DESCRIPTOR);
      for (size_t index = 0; index < maxImportCount && imports[index].Name != 0; ++index) {
        const auto& import = imports[index];
        if (hookThunkTable(import.Name, import.OriginalFirstThunk, import.FirstThunk, true,
                           "normal"))
          return true;
      }
    }
  }

  struct DelayImportDescriptor {
    uint32_t attributes;
    uint32_t dllName;
    uint32_t moduleHandle;
    uint32_t addressTable;
    uint32_t nameTable;
    uint32_t boundAddressTable;
    uint32_t unloadAddressTable;
    uint32_t timestamp;
  };
  static_assert(sizeof(DelayImportDescriptor) == 0x20);
  const auto& delayDirectory =
      nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
  if (delayDirectory.VirtualAddress != 0) {
    auto* delays = reinterpret_cast<DelayImportDescriptor*>(
        imagePointer(delayDirectory.VirtualAddress, sizeof(DelayImportDescriptor), true));
    if (delays != nullptr) {
      const size_t maxDelayCount = imageSize / sizeof(DelayImportDescriptor);
      for (size_t index = 0; index < maxDelayCount && delays[index].dllName != 0; ++index) {
        const auto& delay = delays[index];
        const bool isRva = (delay.attributes & 1u) != 0;
        if (hookThunkTable(delay.dllName, delay.nameTable, delay.addressTable, isRva,
                           "delayed"))
          return true;
      }
    }
  }
  return false;
}

DWORD WINAPI BridgeThread(void*) {
  char runtimeManifest[MAX_PATH]{};
  const DWORD runtimeLength = GetEnvironmentVariableA(
      "XR_RUNTIME_JSON", runtimeManifest, sizeof(runtimeManifest));
  Log("bridge worker start pid=%lu XR_RUNTIME_JSON=%s", GetCurrentProcessId(),
      runtimeLength > 0 && runtimeLength < sizeof(runtimeManifest)
          ? runtimeManifest : "(unset)");
  if (IsBridgeDisabled()) {
    Log("bridge disabled by VRCLIENT_GTASA_DISABLE; game and existing mods are untouched");
    return 0;
  }
  if (!g_configLoaded && !LoadGtaSaNativeConfig()) return 0;
  g_copyCameraMatrixToRwCam =
      reinterpret_cast<CopyCameraMatrixFn>(g_config.copyCameraMatrixToRwCam);
  if (!IsExpectedGameImage()) return 0;
  g_stereoRequested = IsStereoRequested();
  g_inputRequested = g_stereoRequested && IsInputRequested();
  g_firstPersonRequested = g_stereoRequested && IsFirstPersonRequested();
  ConfigureTurnMode();
  if (g_inputRequested && !InstallPadUpdateHook()) return 0;
  if (!g_d3d9ImportHooked.load() && !HookD3D9Import()) {
    Log("Direct3DCreate9 import was not found; bridge is disabled");
    return 0;
  }
  if (g_stereoRequested && IsStereoSeamHookDisabled()) {
    Log("stereo scene seams disabled by VRCLIENT_GTASA_SEAMS; bridge will run without stereo conversion");
  }
  g_bridgeEnabled.store(true);
  Log("bridge active: %s%s%s; existing ASI plugins remain loaded",
      g_stereoRequested ? "experimental x86 stereo scene conversion" : "flat theater",
      g_inputRequested ? "; OpenXR native CPad input" : "",
      g_firstPersonRequested ? "; head-anchored first-person camera" : "");
  return 0;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_bridgeModule = module;
    DisableThreadLibraryCalls(module);
    // Keep DLL_PROCESS_ATTACH loader-lock safe. Config I/O, executable hashing,
    // VirtualProtect patching, and all OpenXR work run on the worker after the
    // loader has released its lock. The worker is deliberately started before
    // enabling the bridge so a race with the game's first D3D9 device cannot
    // expose partially initialized state.
    HANDLE thread = CreateThread(nullptr, 0, &BridgeThread, nullptr, 0, nullptr);
    if (thread != nullptr) CloseHandle(thread);
  } else if (reason == DLL_PROCESS_DETACH) {
    g_bridgeEnabled.store(false);
    g_shutdown.store(true);
  }
  return TRUE;
}

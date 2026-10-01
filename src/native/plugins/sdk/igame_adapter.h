#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  define VRCLIENT_ADAPTER_CALL __cdecl
#  if defined(VRCLIENT_ADAPTER_EXPORTS)
#    define VRCLIENT_ADAPTER_API __declspec(dllexport)
#  else
#    define VRCLIENT_ADAPTER_API
#  endif
#else
#  define VRCLIENT_ADAPTER_CALL
#  define VRCLIENT_ADAPTER_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define VRCLIENT_ADAPTER_ABI_VERSION 1u
#define VRCLIENT_ADAPTER_API_VERSION_MAJOR 1u
#define VRCLIENT_ADAPTER_API_VERSION_MINOR 0u
#define VRCLIENT_ADAPTER_API_VERSION_PATCH 0u

#define VRCLIENT_ADAPTER_EXPORT_GET_ABI "vrclient_get_adapter_abi"
#define VRCLIENT_ADAPTER_EXPORT_GET_METADATA "vrclient_get_adapter_metadata"
#define VRCLIENT_ADAPTER_EXPORT_CREATE "vrclient_create_adapter"
#define VRCLIENT_ADAPTER_EXPORT_DESTROY "vrclient_destroy_adapter"

typedef uint32_t VrAdapterBool;

typedef enum VrAdapterResult {
  VR_ADAPTER_OK = 0,
  VR_ADAPTER_SKIPPED = 1,
  VR_ADAPTER_ERROR_INVALID_ARGUMENT = -1,
  VR_ADAPTER_ERROR_UNSUPPORTED_API = -2,
  VR_ADAPTER_ERROR_UNSUPPORTED_TARGET = -3,
  VR_ADAPTER_ERROR_RECOVERABLE = -4,
  VR_ADAPTER_ERROR_FATAL = -5,
  VR_ADAPTER_ERROR_RELOAD_UNSAFE = -6
} VrAdapterResult;

typedef enum VrAdapterCapabilityFlags {
  VR_ADAPTER_CAP_NONE = 0,
  VR_ADAPTER_CAP_FRAME_TICK = 1u << 0u,
  VR_ADAPTER_CAP_CONFIG_RELOAD = 1u << 1u,
  VR_ADAPTER_CAP_FAST_RESTART = 1u << 2u,
  VR_ADAPTER_CAP_HOT_RELOAD_SAFE = 1u << 3u
} VrAdapterCapabilityFlags;

typedef enum VrAdapterCallbackFlags {
  VR_ADAPTER_CALLBACK_NONE = 0,
  VR_ADAPTER_CALLBACK_HOT_PATH = 1u << 0u,
  VR_ADAPTER_CALLBACK_MAY_ALLOCATE = 1u << 1u,
  VR_ADAPTER_CALLBACK_MAY_BLOCK = 1u << 2u
} VrAdapterCallbackFlags;

typedef struct VrAdapterApiVersion {
  uint32_t size;
  uint32_t major;
  uint32_t minor;
  uint32_t patch;
} VrAdapterApiVersion;

typedef struct VrAdapterBuildId {
  uint32_t size;
  const char* game_id;
  const char* build_id;
} VrAdapterBuildId;

typedef struct VrAdapterMetadata {
  uint32_t size;
  uint32_t abi_version;
  const char* adapter_id;
  const char* adapter_version;
  const char* display_name;
  const char* const* supported_game_ids;
  uint32_t supported_game_id_count;
  const VrAdapterBuildId* supported_builds;
  uint32_t supported_build_count;
  VrAdapterApiVersion required_host_api_min;
  VrAdapterApiVersion required_host_api_max;
  uint32_t capabilities;
  uint32_t reserved;
} VrAdapterMetadata;

typedef struct VrAdapterService {
  uint32_t size;
  uint32_t service_id;
  uint32_t service_version;
  void* service;
} VrAdapterService;

typedef void (VRCLIENT_ADAPTER_CALL *VrAdapterDiagnosticFn)(
    void* user_data,
    const char* event_name,
    const char* key,
    const char* value);

typedef struct VrAdapterDiagnosticsService {
  uint32_t size;
  uint32_t version;
  VrAdapterDiagnosticFn log_event;
  void* user_data;
} VrAdapterDiagnosticsService;

typedef struct VrAdapterPoseTimingService {
  uint32_t size;
  uint32_t version;
  uint64_t frame_index;
  int64_t predicted_display_time_ns;
  double predicted_display_period_seconds;
} VrAdapterPoseTimingService;

typedef struct VrAdapterConfigService {
  uint32_t size;
  uint32_t version;
  const char* adapter_config_path;
  const char* game_config_path;
} VrAdapterConfigService;

typedef struct VrAdapterHostServices {
  uint32_t size;
  VrAdapterApiVersion host_api_version;
  const VrAdapterService* services;
  uint32_t service_count;
} VrAdapterHostServices;

typedef struct VrAdapterTargetIdentity {
  uint32_t size;
  const char* game_id;
  const char* build_id;
  const char* executable_path;
  const char* source;
} VrAdapterTargetIdentity;

typedef struct VrAdapterContext {
  uint32_t size;
  const VrAdapterHostServices* host_services;
  VrAdapterTargetIdentity target;
  void* host_user_data;
} VrAdapterContext;

typedef struct VrAdapterFrameInfo {
  uint32_t size;
  uint64_t frame_index;
  int64_t predicted_display_time_ns;
  double predicted_display_period_seconds;
  double delta_seconds;
  uint32_t callback_flags;
} VrAdapterFrameInfo;

typedef struct VrGameAdapter VrGameAdapter;

typedef VrAdapterResult (VRCLIENT_ADAPTER_CALL *VrAdapterValidateFn)(
    VrGameAdapter* adapter,
    const VrAdapterContext* context);
typedef VrAdapterResult (VRCLIENT_ADAPTER_CALL *VrAdapterInitFn)(
    VrGameAdapter* adapter,
    const VrAdapterContext* context);
typedef VrAdapterResult (VRCLIENT_ADAPTER_CALL *VrAdapterTickFn)(
    VrGameAdapter* adapter,
    const VrAdapterContext* context,
    const VrAdapterFrameInfo* frame);
typedef VrAdapterResult (VRCLIENT_ADAPTER_CALL *VrAdapterSimpleFn)(
    VrGameAdapter* adapter,
    const VrAdapterContext* context);

struct VrGameAdapter {
  uint32_t size;
  void* adapter_data;
  VrAdapterValidateFn validate;
  VrAdapterInitFn init;
  VrAdapterTickFn tick;
  VrAdapterSimpleFn suspend;
  VrAdapterSimpleFn resume;
  VrAdapterSimpleFn reload_config;
  VrAdapterSimpleFn shutdown;
};

typedef uint32_t (VRCLIENT_ADAPTER_CALL *VrClientGetAdapterAbiFn)(void);
typedef const VrAdapterMetadata* (VRCLIENT_ADAPTER_CALL *VrClientGetAdapterMetadataFn)(void);
typedef VrAdapterResult (VRCLIENT_ADAPTER_CALL *VrClientCreateAdapterFn)(
    const VrAdapterHostServices* services,
    VrGameAdapter** adapter);
typedef void (VRCLIENT_ADAPTER_CALL *VrClientDestroyAdapterFn)(VrGameAdapter* adapter);

#ifdef __cplusplus
}

namespace vrclient::plugins::sdk {
using IGameAdapter = ::VrGameAdapter;
}  // namespace vrclient::plugins::sdk
#endif

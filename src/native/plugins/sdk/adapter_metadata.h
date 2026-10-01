#pragma once

#include "plugins/sdk/igame_adapter.h"

#ifdef __cplusplus
namespace vrclient::plugins::sdk {

constexpr VrAdapterApiVersion kHostApiVersion{
    sizeof(VrAdapterApiVersion),
    VRCLIENT_ADAPTER_API_VERSION_MAJOR,
    VRCLIENT_ADAPTER_API_VERSION_MINOR,
    VRCLIENT_ADAPTER_API_VERSION_PATCH};

inline bool apiVersionInRange(
    const VrAdapterApiVersion& host,
    const VrAdapterApiVersion& minimum,
    const VrAdapterApiVersion& maximum) {
  if (host.major < minimum.major || host.major > maximum.major) {
    return false;
  }
  if (host.major == minimum.major && host.minor < minimum.minor) {
    return false;
  }
  if (host.major == maximum.major && host.minor > maximum.minor) {
    return false;
  }
  return true;
}

inline bool metadataHasAbiShape(const VrAdapterMetadata* metadata) {
  return metadata != nullptr &&
      metadata->size >= sizeof(VrAdapterMetadata) &&
      metadata->abi_version == VRCLIENT_ADAPTER_ABI_VERSION &&
      metadata->adapter_id != nullptr &&
      metadata->adapter_version != nullptr &&
      metadata->supported_game_ids != nullptr &&
      metadata->supported_game_id_count > 0 &&
      metadata->supported_builds != nullptr &&
      metadata->supported_build_count > 0;
}

}  // namespace vrclient::plugins::sdk
#endif

#include "plugins/host/plugin_library.h"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

#include <string>
#include <utility>

namespace vrclient::plugins::host {
namespace {

bool hasUnsafePathComponent(const std::filesystem::path& path) {
  for (const auto& part : path) {
    if (part == "..") {
      return true;
    }
  }
  return false;
}

bool isNetworkPath(const std::filesystem::path& path) {
  const std::string text = path.generic_string();
  return text.rfind("//", 0) == 0 || text.rfind("\\\\", 0) == 0;
}

bool isSafePluginPath(const std::filesystem::path& path) {
  return !path.empty() && !hasUnsafePathComponent(path) && !isNetworkPath(path);
}

std::shared_ptr<void> openNativeLibrary(
    const std::filesystem::path& plugin_path,
    std::string& reason_code) {
#if defined(_WIN32)
  HMODULE module = LoadLibraryExW(
      plugin_path.wstring().c_str(),
      nullptr,
      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if (module == nullptr) {
    reason_code = "load_library_failed";
    return {};
  }
  return std::shared_ptr<void>(
      module,
      [](void* handle) {
        if (handle != nullptr) {
          FreeLibrary(static_cast<HMODULE>(handle));
        }
      });
#else
  void* handle = dlopen(plugin_path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) {
    reason_code = "load_library_failed";
    return {};
  }
  return std::shared_ptr<void>(
      handle,
      [](void* native_handle) {
        if (native_handle != nullptr) {
          dlclose(native_handle);
        }
      });
#endif
}

template <typename Function>
Function resolveSymbol(void* native_handle, const char* symbol_name) {
#if defined(_WIN32)
  return reinterpret_cast<Function>(
      GetProcAddress(static_cast<HMODULE>(native_handle), symbol_name));
#else
  return reinterpret_cast<Function>(dlsym(native_handle, symbol_name));
#endif
}

PluginHostResult libraryFailure(
    PluginHostStatus status,
    std::string reason_code,
    std::string message,
    const PluginLoadRequest& request) {
  PluginHostResult out;
  out.status = status;
  out.reason_code = std::move(reason_code);
  out.message = std::move(message);
  out.diagnostics_fields = {
      {"plugin_path", request.plugin_path.lexically_normal().generic_string()},
      {"reason_code", out.reason_code},
      {"target_game_id", request.target_game_id},
      {"target_build_id", request.target_build_id},
  };
  return out;
}

}  // namespace

bool resolvePluginExports(
    const std::filesystem::path& plugin_path,
    LoadedPluginLibrary& library,
    std::string& reason_code) {
  if (!isSafePluginPath(plugin_path)) {
    reason_code = "unsafe_plugin_path";
    return false;
  }

  LoadedPluginLibrary resolved;
  resolved.plugin_path = plugin_path.lexically_normal();
  resolved.native_handle = openNativeLibrary(resolved.plugin_path, reason_code);
  if (!resolved.native_handle) {
    return false;
  }

  void* native_handle = resolved.native_handle.get();
  resolved.exports.get_abi = resolveSymbol<VrClientGetAdapterAbiFn>(
      native_handle,
      VRCLIENT_ADAPTER_EXPORT_GET_ABI);
  resolved.exports.get_metadata = resolveSymbol<VrClientGetAdapterMetadataFn>(
      native_handle,
      VRCLIENT_ADAPTER_EXPORT_GET_METADATA);
  resolved.exports.create_adapter = resolveSymbol<VrClientCreateAdapterFn>(
      native_handle,
      VRCLIENT_ADAPTER_EXPORT_CREATE);
  resolved.exports.destroy_adapter = resolveSymbol<VrClientDestroyAdapterFn>(
      native_handle,
      VRCLIENT_ADAPTER_EXPORT_DESTROY);

  if (!resolved.exports.get_abi || !resolved.exports.get_metadata ||
      !resolved.exports.create_adapter || !resolved.exports.destroy_adapter) {
    reason_code = "missing_exported_symbol";
    return false;
  }

  library = std::move(resolved);
  reason_code = "exports_resolved";
  return true;
}

PluginHostResult loadPluginFromLibrary(
    const PluginLoadRequest& request,
    diagnostics::AsyncLogger* logger) {
  LoadedPluginLibrary library;
  std::string reason_code;
  if (!resolvePluginExports(request.plugin_path, library, reason_code)) {
    const bool unsafe = reason_code == "unsafe_plugin_path";
    return libraryFailure(
        unsafe ? PluginHostStatus::UnsafePath : PluginHostStatus::MissingExport,
        reason_code.empty() ? "load_library_failed" : reason_code,
        unsafe ? "plugin path is not a safe explicit local path"
               : "plugin library could not be loaded or resolved",
        request);
  }

  PluginLoadRequest resolved_request = request;
  resolved_request.plugin_path = library.plugin_path;
  resolved_request.exports = library.exports;

  PluginHostResult out = loadPlugin(resolved_request, logger);
  if (out.status == PluginHostStatus::Loaded) {
    out.adapter.library_handle = library.native_handle;
  }
  return out;
}

}  // namespace vrclient::plugins::host

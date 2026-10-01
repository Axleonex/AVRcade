#pragma once

#include "plugins/host/plugin_host.h"

#include <filesystem>
#include <memory>
#include <string>

namespace vrclient::plugins::host {

struct LoadedPluginLibrary {
  std::filesystem::path plugin_path;
  PluginExports exports;
  std::shared_ptr<void> native_handle;
};

bool resolvePluginExports(
    const std::filesystem::path& plugin_path,
    LoadedPluginLibrary& library,
    std::string& reason_code);

PluginHostResult loadPluginFromLibrary(
    const PluginLoadRequest& request,
    diagnostics::AsyncLogger* logger = nullptr);

}  // namespace vrclient::plugins::host

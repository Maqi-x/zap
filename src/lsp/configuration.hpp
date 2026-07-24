#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace zap::lsp {

struct RuntimePathConfiguration {
  std::optional<std::filesystem::path> coreDir;
  std::optional<std::filesystem::path> stdlibDir;
  std::vector<std::string> errors;
};

RuntimePathConfiguration loadRuntimePathConfiguration(
    const std::filesystem::path &workspaceRoot,
    const std::optional<std::string> &initializationCorePath,
    const std::optional<std::string> &initializationStdlibPath);

} // namespace zap::lsp

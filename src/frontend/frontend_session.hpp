#pragma once

#include "frontend/module_loader.hpp"
#include "sema/module_info.hpp"
#include "utils/diagnostics.hpp"
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace zap::frontend {

struct FrontendSessionConfig {
  RuntimePaths runtimePaths;
  ImportMap importMap;
  bool includePrelude = true;
  bool allowEntryErrors = false;
};

struct FrontendProject {
  std::map<std::string, std::unique_ptr<sema::ModuleInfo>> modules;
  std::vector<Diagnostic> diagnostics;
  std::vector<std::string> errors;
  bool loaded = false;
};

using SourceLoader = std::function<std::optional<std::string>(
    const std::filesystem::path &canonicalPath)>;

class FrontendSession {
public:
  FrontendSession(FrontendSessionConfig config, SourceLoader sourceLoader);

  FrontendProject load(const std::filesystem::path &entryPath);

private:
  FrontendSessionConfig config_;
  SourceLoader sourceLoader_;

  bool loadModule(const std::filesystem::path &modulePath,
                  const std::string &entryModuleId, FrontendProject &project,
                  std::unordered_map<std::string, bool> &visiting);
};

} // namespace zap::frontend

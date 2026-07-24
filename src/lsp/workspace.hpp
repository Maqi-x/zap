#pragma once

#include "driver/args/argparse.hpp"
#include "frontend/module_loader.hpp"
#include "lsp/source_manager.hpp"
#include "sema/module_info.hpp"
#include "workspace_types.hpp"
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>

namespace zap::lsp {

class Workspace {
  SourceManager sourceManager_;
  std::set<std::string> publishedDiagnosticUris_;
  zap::frontend::RuntimePaths runtimePaths_;

  void appendDiagnostics(AnalysisResult &result,
                         const std::vector<zap::Diagnostic> &diagnostics,
                         const std::string &fallbackUri) const;
  void clearStaleDiagnostics(AnalysisResult &result);

public:
  Workspace();
  std::vector<std::string>
  configure(const std::filesystem::path &workspaceRoot,
            const std::optional<std::string> &corePath,
            const std::optional<std::string> &stdlibPath);
  const SourceSnapshot *document(const std::string &uri) const;
  void open(const std::string &uri, std::filesystem::path path,
            std::string text, int64_t version);
  void update(const std::string &uri, std::string text, int64_t version);
  void close(const std::string &uri);
  bool contains(const std::string &uri) const;
  std::optional<ProjectState> loadProject(const std::string &uri,
                                          bool allowEntryErrors = false);
  std::optional<std::string> sourceForUri(const std::string &uri);
  AnalysisResult analyze(const std::string &uri);
};

} // namespace zap::lsp

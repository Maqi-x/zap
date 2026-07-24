#include "lsp/workspace.hpp"

#include "frontend/frontend_session.hpp"
#include "lsp/configuration.hpp"
#include "lsp/protocol_utils.hpp"
#include "sema/binder.hpp"
#include <utility>
#include <vector>

namespace zap::lsp {

Workspace::Workspace()
    : runtimePaths_{
          std::filesystem::path(), std::filesystem::path(ZAPC_CORE_DIR),
          std::filesystem::path(ZAPC_STDLIB_DIR), std::filesystem::path(),
          zap::frontend::EnvironmentOverrides::Ignore} {}

std::vector<std::string>
Workspace::configure(const std::filesystem::path &workspaceRoot,
                     const std::optional<std::string> &corePath,
                     const std::optional<std::string> &stdlibPath) {
  auto configuration =
      loadRuntimePathConfiguration(workspaceRoot, corePath, stdlibPath);
  if (configuration.coreDir) {
    runtimePaths_.coreDirOverride = std::move(*configuration.coreDir);
  }
  if (configuration.stdlibDir) {
    runtimePaths_.stdlibDirOverride = std::move(*configuration.stdlibDir);
  }
  return configuration.errors;
}

const SourceSnapshot *Workspace::document(const std::string &uri) const {
  return sourceManager_.document(uri);
}

void Workspace::open(const std::string &uri, std::filesystem::path path,
                     std::string text, int64_t version) {
  sourceManager_.open(uri, std::move(path), std::move(text), version);
}

void Workspace::update(const std::string &uri, std::string text,
                       int64_t version) {
  sourceManager_.update(uri, std::move(text), version);
}

void Workspace::close(const std::string &uri) { sourceManager_.close(uri); }

bool Workspace::contains(const std::string &uri) const {
  return sourceManager_.contains(uri);
}

std::optional<std::string> Workspace::sourceForUri(const std::string &uri) {
  auto source = sourceManager_.sourceForUri(uri);
  return source ? std::optional<std::string>((*source)->text) : std::nullopt;
}

void Workspace::appendDiagnostics(
    AnalysisResult &result, const std::vector<zap::Diagnostic> &diagnostics,
    const std::string &fallbackUri) const {
  for (const auto &diagnostic : diagnostics) {
    std::string uri = fallbackUri;
    if (!diagnostic.fileName.empty()) {
      uri = sourceManager_.uriForPath(diagnostic.fileName);
    }
    result.diagnosticsByUri[uri].push_back(diagnostic);
  }
}

void Workspace::clearStaleDiagnostics(AnalysisResult &result) {
  std::set<std::string> currentUris;
  for (const auto &[uri, _] : result.diagnosticsByUri) {
    currentUris.insert(uri);
  }
  for (const auto &uri : publishedDiagnosticUris_) {
    if (currentUris.count(uri) == 0) {
      result.diagnosticsByUri[uri] = {};
    }
  }
  publishedDiagnosticUris_ = std::move(currentUris);
}

std::optional<ProjectState> Workspace::loadProject(const std::string &uri,
                                                   bool allowEntryErrors) {
  const auto *document = this->document(uri);
  if (!document) {
    return std::nullopt;
  }

  auto flags = findAndReadFlags(document->path);

  ProjectState state;
  zap::frontend::FrontendSession session(
      {runtimePaths_, flags.importMap, true, allowEntryErrors},
      [this](const std::filesystem::path &path) -> std::optional<std::string> {
        auto source = sourceManager_.sourceForPath(path);
        return source ? std::optional<std::string>((*source)->text)
                      : std::nullopt;
      });
  auto project = session.load(document->path);
  if (!project.loaded) {
    appendDiagnostics(state.analysis, project.diagnostics, uri);
    if (state.analysis.diagnosticsByUri.find(uri) ==
        state.analysis.diagnosticsByUri.end()) {
      state.analysis.diagnosticsByUri[uri] = {};
    }
    return state;
  }
  session.bind(project);
  appendDiagnostics(state.analysis, project.diagnostics, uri);
  state.semanticInfo = std::move(project.semanticInfo);
  state.moduleMap = std::move(project.modules);

  for (const auto &[moduleId, _] : state.moduleMap) {
    state.uriByModuleId[moduleId] = sourceManager_.uriForPath(moduleId);
  }

  return state;
}

AnalysisResult Workspace::analyze(const std::string &uri) {
  AnalysisResult result;
  const auto *document = this->document(uri);
  if (!document) {
    return result;
  }

  auto flags = findAndReadFlags(document->path);

  zap::frontend::FrontendSession session(
      {runtimePaths_, flags.importMap},
      [this](const std::filesystem::path &path) -> std::optional<std::string> {
        auto source = sourceManager_.sourceForPath(path);
        return source ? std::optional<std::string>((*source)->text)
                      : std::nullopt;
      });
  auto project = session.load(document->path);
  if (!project.loaded) {
    appendDiagnostics(result, project.diagnostics, uri);
    if (result.diagnosticsByUri.find(uri) == result.diagnosticsByUri.end()) {
      result.diagnosticsByUri[uri] = {};
    }
    clearStaleDiagnostics(result);
    return result;
  }
  session.bind(project);
  appendDiagnostics(result, project.diagnostics, uri);
  clearStaleDiagnostics(result);
  return result;
}

} // namespace zap::lsp

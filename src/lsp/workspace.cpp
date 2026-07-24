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
  appendDiagnostics(state.analysis, project.diagnostics, uri);
  state.moduleMap = std::move(project.modules);
  if (!project.loaded) {
    if (state.analysis.diagnosticsByUri.find(uri) ==
        state.analysis.diagnosticsByUri.end()) {
      state.analysis.diagnosticsByUri[uri] = {};
    }
    return state;
  }

  for (const auto &[moduleId, _] : state.moduleMap) {
    state.uriByModuleId[moduleId] = sourceManager_.uriForPath(moduleId);
  }

  auto entrySource = sourceManager_.sourceForPath(document->path);
  if (entrySource) {
    auto entryId = document->path.string();
    auto entryModuleIt = state.moduleMap.find(entryId);
    if (entryModuleIt != state.moduleMap.end()) {
      entryModuleIt->second->isEntry = true;
    }

    zap::DiagnosticEngine diagnostics((*entrySource)->text,
                                      document->path.string());
    std::vector<sema::ModuleInfo *> modules;
    modules.reserve(state.moduleMap.size());
    for (auto &[_, modulePtr] : state.moduleMap) {
      diagnostics.registerSource(modulePtr->sourceName, modulePtr->sourceText);
      modules.push_back(modulePtr.get());
    }

    sema::Binder binder(diagnostics, true, &state.semanticInfo);
    auto boundAst = binder.bind(std::move(modules));
    (void)boundAst;
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
  appendDiagnostics(result, project.diagnostics, uri);
  if (!project.loaded) {
    if (result.diagnosticsByUri.find(uri) == result.diagnosticsByUri.end()) {
      result.diagnosticsByUri[uri] = {};
    }
    clearStaleDiagnostics(result);
    return result;
  }

  std::string entryId = document->path.string();
  auto entrySource = sourceManager_.sourceForPath(document->path);
  if (!entrySource) {
    result.diagnosticsByUri[uri] = {};
    clearStaleDiagnostics(result);
    return result;
  }

  auto entryModuleIt = project.modules.find(entryId);
  if (entryModuleIt == project.modules.end()) {
    clearStaleDiagnostics(result);
    return result;
  }

  entryModuleIt->second->isEntry = true;

  zap::DiagnosticEngine diagnostics((*entrySource)->text,
                                    document->path.string());
  std::vector<sema::ModuleInfo> modules;
  modules.reserve(project.modules.size());
  for (auto &[_, modulePtr] : project.modules) {
    diagnostics.registerSource(modulePtr->sourceName, modulePtr->sourceText);
    modules.push_back(std::move(*modulePtr));
  }

  sema::Binder binder(diagnostics);
  auto boundAst = binder.bind(modules);
  (void)boundAst;

  appendDiagnostics(result, diagnostics.diagnostics(), uri);
  clearStaleDiagnostics(result);
  return result;
}

} // namespace zap::lsp

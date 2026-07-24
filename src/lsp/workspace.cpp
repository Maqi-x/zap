#include "lsp/workspace.hpp"

#include "lexer/lexer.hpp"
#include "lsp/configuration.hpp"
#include "lsp/protocol_utils.hpp"
#include "parser/parser.hpp"
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

bool Workspace::loadModuleGraph(
    const std::filesystem::path &modulePath,
    std::map<std::string, std::unique_ptr<sema::ModuleInfo>> &modules,
    std::set<std::string> &visiting, AnalysisResult &result,
    const std::string &entryUri, const zap::args::ImportMap &importMap,
    bool allowEntryErrors) {
  std::filesystem::path canonicalPath =
      std::filesystem::weakly_canonical(modulePath);
  std::string moduleId = canonicalPath.string();
  const auto *entryDocument = document(entryUri);
  if (!entryDocument) {
    return false;
  }
  std::string entryModuleId = entryDocument->path.string();
  bool isEntryModule = moduleId == entryModuleId;
  if (modules.find(moduleId) != modules.end()) {
    return true;
  }
  if (visiting.count(moduleId)) {
    return false;
  }

  visiting.insert(moduleId);

  auto source = sourceManager_.sourceForPath(canonicalPath);
  if (!source) {
    visiting.erase(moduleId);
    return false;
  }

  zap::DiagnosticEngine diagnostics((*source)->text, canonicalPath.string());
  Lexer lex(diagnostics);
  auto tokens = lex.tokenize((*source)->text);
  zap::Parser parser(tokens, diagnostics);
  auto ast = parser.parse();

  appendDiagnostics(result, diagnostics.diagnostics(),
                    sourceManager_.uriForPath(canonicalPath));

  if (!ast ||
      (diagnostics.hadErrors() && !(allowEntryErrors && isEntryModule))) {
    visiting.erase(moduleId);
    return false;
  }

  auto module = std::make_unique<sema::ModuleInfo>();
  module->moduleId = moduleId;
  module->moduleName = canonicalPath.stem().string();
  module->linkPath = zap::frontend::computeLogicalModulePath(
      canonicalPath, runtimePaths_, importMap);
  module->sourceName = canonicalPath.string();
  module->sourceText = (*source)->text;
  module->root = std::move(ast);

  zap::frontend::injectImplicitPreludeImportIfNeeded(*module, true);

  for (const auto &child : module->root->children) {
    auto importNode = dynamic_cast<ImportNode *>(child.get());
    if (!importNode) {
      continue;
    }

    std::vector<std::filesystem::path> importTargets;
    if (!zap::frontend::resolveImportTargets(canonicalPath, *importNode,
                                             importTargets, importMap,
                                             runtimePaths_)) {
      continue;
    }

    module->imports.push_back(
        zap::frontend::makeResolvedImport(*importNode, importTargets));
  }

  for (const auto &import : module->imports) {
    for (const auto &targetId : import.targetModuleIds) {
      if (!loadModuleGraph(targetId, modules, visiting, result, entryUri,
                           importMap, allowEntryErrors)) {
        visiting.erase(moduleId);
        return false;
      }
    }
  }

  visiting.erase(moduleId);
  modules[moduleId] = std::move(module);
  return true;
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
  std::set<std::string> visiting;
  if (!loadModuleGraph(document->path, state.moduleMap, visiting,
                       state.analysis, uri, flags.importMap,
                       allowEntryErrors)) {
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

  std::map<std::string, std::unique_ptr<sema::ModuleInfo>> moduleMap;
  std::set<std::string> visiting;
  if (!loadModuleGraph(document->path, moduleMap, visiting, result, uri,
                       flags.importMap, false)) {
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

  auto entryModuleIt = moduleMap.find(entryId);
  if (entryModuleIt == moduleMap.end()) {
    clearStaleDiagnostics(result);
    return result;
  }

  entryModuleIt->second->isEntry = true;

  zap::DiagnosticEngine diagnostics((*entrySource)->text,
                                    document->path.string());
  std::vector<sema::ModuleInfo> modules;
  modules.reserve(moduleMap.size());
  for (auto &[_, modulePtr] : moduleMap) {
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

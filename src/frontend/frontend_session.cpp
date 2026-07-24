#include "frontend/frontend_session.hpp"

#include "ast/import_node.hpp"
#include "lexer/lexer.hpp"
#include "parser/parser.hpp"

namespace zap::frontend {

FrontendSession::FrontendSession(FrontendSessionConfig config,
                                 SourceLoader sourceLoader)
    : config_(std::move(config)), sourceLoader_(std::move(sourceLoader)) {}

FrontendProject FrontendSession::load(const std::filesystem::path &entryPath) {
  FrontendProject project;
  const auto canonicalEntry = std::filesystem::weakly_canonical(entryPath);
  std::unordered_map<std::string, bool> visiting;
  project.loaded = loadModule(canonicalEntry, canonicalEntry.string(), project,
                              visiting);
  if (project.loaded) {
    auto entry = project.modules.find(canonicalEntry.string());
    if (entry != project.modules.end()) {
      entry->second->isEntry = true;
    }
  }
  return project;
}

bool FrontendSession::loadModule(
    const std::filesystem::path &modulePath, const std::string &entryModuleId,
    FrontendProject &project, std::unordered_map<std::string, bool> &visiting) {
  const auto canonicalPath = std::filesystem::weakly_canonical(modulePath);
  const auto moduleId = canonicalPath.string();
  if (project.modules.count(moduleId) != 0) {
    return true;
  }
  if (visiting[moduleId]) {
    project.errors.push_back("cyclic import detected involving " + moduleId);
    return false;
  }

  auto source = sourceLoader_(canonicalPath);
  if (!source) {
    project.errors.push_back("couldn't open source file: " + moduleId);
    return false;
  }

  visiting[moduleId] = true;
  DiagnosticEngine diagnostics(*source, moduleId);
  Lexer lexer(diagnostics);
  Parser parser(lexer.tokenize(*source), diagnostics);
  auto root = parser.parse();
  const auto &moduleDiagnostics = diagnostics.diagnostics();
  project.diagnostics.insert(project.diagnostics.end(), moduleDiagnostics.begin(),
                             moduleDiagnostics.end());
  const bool isEntry = moduleId == entryModuleId;
  if (!root || (diagnostics.hadErrors() &&
                !(config_.allowEntryErrors && isEntry))) {
    visiting.erase(moduleId);
    return false;
  }

  auto module = std::make_unique<sema::ModuleInfo>();
  module->moduleId = moduleId;
  module->moduleName = canonicalPath.stem().string();
  module->linkPath =
      computeLogicalModulePath(canonicalPath, config_.runtimePaths, config_.importMap);
  module->sourceName = moduleId;
  module->sourceText = std::move(*source);
  module->root = std::move(root);
  injectImplicitPreludeImportIfNeeded(*module, config_.includePrelude);

  for (const auto &child : module->root->children) {
    auto *importNode = dynamic_cast<ImportNode *>(child.get());
    if (!importNode) {
      continue;
    }
    std::vector<std::filesystem::path> targets;
    std::string error;
    if (!resolveImportTargets(canonicalPath, *importNode, targets,
                              config_.importMap, config_.runtimePaths, &error)) {
      project.errors.push_back(std::move(error));
      visiting.erase(moduleId);
      return false;
    }
    module->imports.push_back(makeResolvedImport(*importNode, targets));
  }

  for (const auto &import : module->imports) {
    for (const auto &target : import.targetModuleIds) {
      if (!loadModule(target, entryModuleId, project, visiting)) {
        visiting.erase(moduleId);
        return false;
      }
    }
  }

  visiting.erase(moduleId);
  project.modules[moduleId] = std::move(module);
  return true;
}

} // namespace zap::frontend

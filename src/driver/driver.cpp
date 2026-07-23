#include "driver/driver.hpp"
#include "ast/import_node.hpp"
#include "codegen/llvm_codegen.hpp"
#include "driver/process.hpp"
#include "frontend/module_loader.hpp"
#include "ir/ir_generator.hpp"
#include "ir/ownership_lowering.hpp"
#include "ir/sink_last_use.hpp"
#include "ir/zir_verifier.hpp"
#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "sema/binder.hpp"
#include "sema/bound_nodes.hpp"
#include "sema/module_info.hpp"
#include "utils/diagnostics.hpp"
#include "utils/stream.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>
#include <map>
#include <optional>
#include <set>
#include <string_view>

namespace zap {
bool compileSourceZIR(sema::BoundRootNode &node, std::ostream &ofoutput,
                      bool verifyOwnershipObligations);
bool compileSourceLLVMFromZIR(sema::BoundRootNode &node, std::string &output,
                              const std::string &targetTriple,
                              bool freestanding,
                              bool verifyOwnershipObligations);
std::unique_ptr<zir::Module> generateZIRModule(sema::BoundRootNode &node,
                                               bool verifyOwnershipObligations);
bool compileObjectFromZIR(sema::BoundRootNode &node,
                          const std::string &output_path,
                          int optimization_level,
                          const std::string &targetTriple, bool freestanding,
                          bool verifyOwnershipObligations);
bool compileAssemblyFromZIR(sema::BoundRootNode &node,
                            const std::string &output_path,
                            int optimization_level,
                            const std::string &targetTriple, bool freestanding,
                            bool verifyOwnershipObligations);
namespace {

std::optional<sema::TargetInfo>
targetInfoForTriple(const std::string &requestedTriple) {
  auto normalized = requestedTriple.empty()
                        ? llvm::sys::getDefaultTargetTriple()
                        : llvm::Triple::normalize(requestedTriple);
  llvm::Triple triple(normalized);
  if (triple.isArch32Bit()) {
    return sema::TargetInfo{32};
  }
  if (triple.isArch64Bit()) {
    return sema::TargetInfo{64};
  }
  return std::nullopt;
}

std::filesystem::path
executableObjectPath(const std::filesystem::path &executablePath,
                     const std::filesystem::path &sourcePath) {
  std::error_code ec;
  auto normalizedSource = std::filesystem::absolute(sourcePath, ec);
  if (ec) {
    normalizedSource = sourcePath.lexically_normal();
  }

  auto objectPath = executablePath;
  objectPath += "." +
                std::to_string(std::hash<std::string>{}(
                    normalizedSource.generic_string())) +
                ".o";
  return objectPath;
}

bool emitRequestedTextOutputs(driver &drv, sema::BoundRootNode &node,
                              const std::filesystem::path &base_output_path) {
  bool direct_output =
      !drv.is_implicit_output() && (drv.emits_llvm_text() != drv.emits_zir());

  if (drv.emits_zir()) {
    auto zir_path = direct_output
                        ? base_output_path
                        : std::filesystem::path(base_output_path)
                              .replace_extension(driver::format_fileextension(
                                  args::OutputType::ZIR));
    std::ofstream zir_output(zir_path, std::ios::binary);
    if (!zir_output) {
      driver::reportError("couldn't open the provided file: ", zir_path,
                          "\nreason: ", strerror(errno));
      return true;
    }
    if (compileSourceZIR(node, zir_output,
                         drv.verifies_ownership_obligations())) {
      return true;
    }
  }

  if (drv.emits_llvm_text()) {
    auto llvm_path = direct_output
                         ? base_output_path
                         : std::filesystem::path(base_output_path)
                               .replace_extension(driver::format_fileextension(
                                   args::OutputType::TEXT_LLVM));
    std::string llvmIr;
    if (compileSourceLLVMFromZIR(node, llvmIr, drv.get_target_triple(),
                                 drv.is_freestanding(),
                                 drv.verifies_ownership_obligations())) {
      return true;
    }

    std::ofstream llvm_output(llvm_path, std::ios::binary);
    if (!llvm_output) {
      driver::reportError("couldn't open the provided file: ", llvm_path,
                          "\nreason: ", strerror(errno));
      return true;
    }
    llvm_output << llvmIr;
  }

  return false;
}

std::filesystem::path g_executable_path;

frontend::RuntimePaths runtimePaths() {
  return frontend::RuntimePaths{g_executable_path,
                                std::filesystem::path(ZAPC_CORE_DIR),
                                std::filesystem::path(ZAPC_STDLIB_DIR),
                                std::filesystem::path(ZAPC_STDLIB_PATH)};
}

bool readSourceFile(const std::filesystem::path &path, std::string &content) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    driver::reportError("couldn't open the provided file: ", path,
                        "\nreason: ", strerror(errno));
    return true;
  }

  auto size = file.tellg();
  content.assign(static_cast<size_t>(std::max<std::streamsize>(size, 0)), '\0');

  if (size == 0) {
    err() << "warning: provided file is empty: " << path << '\n';
  } else {
    file.seekg(0);
    file.read(content.data(), size);
  }

  return false;
}

bool loadModuleGraph(
    const std::filesystem::path &entryPath,
    std::map<std::string, std::unique_ptr<sema::ModuleInfo>> &modules,
    std::set<std::string> &visiting, bool incPrelude,
    const std::unordered_map<std::string, std::string> &importMap) {
  auto canonicalPath = std::filesystem::weakly_canonical(entryPath);
  auto moduleId = canonicalPath.string();

  if (modules.find(moduleId) != modules.end()) {
    return false;
  }
  if (visiting.count(moduleId)) {
    driver::reportError("cyclic import detected involving ", canonicalPath);
    return true;
  }

  visiting.insert(moduleId);

  std::string source;
  if (readSourceFile(canonicalPath, source)) {
    return true;
  }

  DiagnosticEngine diagnostics(source, canonicalPath.string());
  Lexer lex(diagnostics);
  auto tokens = lex.tokenize(source);
  Parser parser(tokens, diagnostics);
  auto ast = parser.parse();

  if (diagnostics.hadErrors() || !ast) {
    diagnostics.printText(err());
    return true;
  }

  diagnostics.printText(err());

  auto module = std::make_unique<sema::ModuleInfo>();
  module->moduleId = moduleId;
  module->moduleName = canonicalPath.stem().string();
  module->linkPath = frontend::computeLogicalModulePath(
      canonicalPath, runtimePaths(), importMap);
  module->sourceName = canonicalPath.string();
  module->sourceText = source;
  module->root = std::move(ast);
  frontend::injectImplicitPreludeImportIfNeeded(*module, incPrelude);

  for (const auto &child : module->root->children) {
    auto importNode = dynamic_cast<ImportNode *>(child.get());
    if (!importNode) {
      continue;
    }

    std::vector<std::filesystem::path> importTargets;
    std::string importError;
    if (!frontend::resolveImportTargets(canonicalPath, *importNode,
                                        importTargets, importMap,
                                        runtimePaths(), &importError)) {
      driver::reportError(importError);
      return true;
    }

    module->imports.push_back(
        frontend::makeResolvedImport(*importNode, importTargets));
  }

  for (const auto &import : module->imports) {
    for (const auto &targetId : import.targetModuleIds) {
      if (loadModuleGraph(targetId, modules, visiting, incPrelude, importMap)) {
        return true;
      }
    }
  }

  visiting.erase(moduleId);
  modules[moduleId] = std::move(module);
  return false;
}

} // namespace

driver::driver() = default;

void driver::setExecutablePath(std::filesystem::path path) {
  executable_path = std::move(path);
  g_executable_path = executable_path;
}

bool compileLoadedModules(driver &drv, const std::filesystem::path &entryPath) {
  std::map<std::string, std::unique_ptr<sema::ModuleInfo>> moduleMap;
  std::set<std::string> visiting;
  if (loadModuleGraph(entryPath, moduleMap, visiting,
                      drv.cmdArgs.incStdlib && drv.cmdArgs.incPrelude,
                      drv.cmdArgs.importMap)) {
    return true;
  }

  auto entryId = std::filesystem::weakly_canonical(entryPath).string();
  if (moduleMap.find(entryId) == moduleMap.end()) {
    driver::reportError("failed to load entry module: ", entryPath);
    return true;
  }
  moduleMap[entryId]->isEntry = true;

  std::string entrySource;
  if (readSourceFile(entryPath, entrySource)) {
    return true;
  }
  DiagnosticEngine diagnostics(entrySource, entryPath.string());

  std::vector<sema::ModuleInfo> modules;
  modules.reserve(moduleMap.size());
  for (auto &[_, module] : moduleMap) {
    diagnostics.registerSource(module->sourceName, module->sourceText);
    modules.push_back(std::move(*module));
  }

  auto targetInfo = targetInfoForTriple(drv.get_target_triple());
  if (!targetInfo) {
    driver::reportError("unsupported target word size for '",
                        drv.get_target_triple(), "'");
    return true;
  }
  sema::Binder binder(diagnostics, true, nullptr, *targetInfo);
  auto boundAst = binder.bind(modules);
  diagnostics.printText(err());

  if (!boundAst) {
    driver::reportError(entryPath, ": semantic analysis failed");
    return true;
  }

  if (drv.binary_output()) {
    std::filesystem::path out_path;

    if (drv.get_output_type() == args::OutputType::EXEC) {
      out_path = executableObjectPath(drv.get_output(), entryPath);
      drv.cleanups.emplace_back(out_path);
    } else if (drv.get_output_type() == args::OutputType::OBJECT) {
      if (drv.is_implicit_output()) {
        out_path = entryPath.string() + ".o";
      } else {
        out_path = drv.get_output();
      }
    }

    if (compileObjectFromZIR(*boundAst, out_path.string(),
                             static_cast<int>(drv.cmdArgs.optLevel),
                             drv.cmdArgs.targetTriple, drv.cmdArgs.freestanding,
                             drv.cmdArgs.verifyOwnershipObligations)) {
      return true;
    }

    drv.cmdArgs.objects.emplace_back(std::move(out_path));
  } else if (drv.get_output_type() == args::OutputType::ASM) {
    std::filesystem::path out_path =
        drv.is_implicit_output() ? entryPath : drv.get_output();
    out_path.replace_extension(
        driver::format_fileextension(args::OutputType::ASM));
    if (compileAssemblyFromZIR(
            *boundAst, out_path.string(),
            static_cast<int>(drv.cmdArgs.optLevel), drv.cmdArgs.targetTriple,
            drv.cmdArgs.freestanding, drv.cmdArgs.verifyOwnershipObligations)) {
      return true;
    }
  } else if (drv.emits_text_output()) {
    std::filesystem::path out_path =
        drv.is_implicit_output() ? entryPath : drv.get_output();
    if (emitRequestedTextOutputs(drv, *boundAst, out_path)) {
      return true;
    }
  } else {
    return true;
  }

  return false;
}

args::ParseResult driver::parseArgs(int argc, char **argv) {
  auto result = args::parse(argc, argv, cmdArgs);
  if (result == args::ParseResult::Success && cmdArgs.printStdlibPath) {
    out() << frontend::stdlibRootPath(runtimePaths()) << '\n';
    return args::ParseResult::SkipCompilation;
  }
  if (result == args::ParseResult::Success && cmdArgs.printCorePath) {
    out() << frontend::coreRootPath(runtimePaths()) << '\n';
    return args::ParseResult::SkipCompilation;
  }
  return result;
}

bool driver::splitInputs() {
  for (const std::string_view &input : get_inputs()) {
    std::filesystem::path input_path = input;
    std::string ext = input_path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".zp") {
      cmdArgs.sources.emplace_back(std::move(input_path));
    } else if (ext == ".a" || ext == ".o") {
      cmdArgs.objects.emplace_back(std::move(input_path));
    } else {
      reportError("unknown input type: ", input);
      return true;
    }
  }

  return false;
}

bool driver::verifyOutput() {
  const args::OutputType &emit_type = get_output_type();

  bool per_file_emit =
      emits_text_output() || (emit_type != args::OutputType::EXEC);

  if (per_file_emit && get_inputs().size() > 1 && !is_implicit_output()) {
    reportError("cannot specify -o with multiple input files");
    return true;
  }

  if (per_file_emit && !cmdArgs.objects.empty()) {
    reportError(
        "cannot use object files or archives with the selected output mode");
    return true;
  }

  if (!format_supported()) {
    reportError("chosen file output mode is not yet supported in this version");
    return true;
  }

  if (cmdArgs.freestanding && needs_linking()) {
    reportError("cannot link executable in freestanding mode; use -c, -S, or "
                "-emit-llvm and link with an OS-specific linker script");
    return true;
  }

  return false;
}

bool verifyFile(const std::filesystem::path &input) {
  if (!std::filesystem::exists(input)) {
    driver::reportError("provided file doesn't exist: ", input);
    return true;
  } else if (!std::filesystem::is_regular_file(input)) {
    driver::reportError("provided file isn't a regular file: ", input);
    return true;
  }
  return false;
}

bool driver::verifySources() {
  for (const std::filesystem::path &input : cmdArgs.sources) {
    if (verifyFile(input))
      return true;
  }
  for (const std::filesystem::path &input : cmdArgs.objects) {
    if (verifyFile(input))
      return true;
  }
  return false;
}

std::unique_ptr<zir::Module>
generateZIRModule(sema::BoundRootNode &node, bool verifyOwnershipObligations) {
  zir::BoundIRGenerator irGen;
  auto module = irGen.generate(node);
  if (!module) {
    return nullptr;
  }
  zir::optimizeSinkArgumentMoves(*module);
  zir::lowerDeadOwnedResults(*module);
  auto verification = zir::ZirVerifier().verify(*module);
  if (!verification) {
    throw std::runtime_error("ZIR verification failed:\n" +
                             verification.format());
  }
  if (verifyOwnershipObligations) {
    auto obligations = zir::ZirVerifier().verifyOwnershipObligations(*module);
    if (!obligations) {
      throw std::runtime_error("ownership obligation verification failed:\n" +
                               obligations.format());
    }
  }
  return module;
}

bool compileSourceZIR(sema::BoundRootNode &node, std::ostream &ofoutput,
                      bool verifyOwnershipObligations) {
  try {
    auto mod = generateZIRModule(node, verifyOwnershipObligations);
    if (mod) {
      ofoutput << mod->toString();
    } else {
      driver::reportError("failed to generate ZIR");
      return true;
    }
  } catch (const std::exception &ex) {
    driver::reportError("ZIR generation failed: ", ex.what());
    return true;
  }
  return false;
}

bool compileSourceLLVMFromZIR(sema::BoundRootNode &node, std::string &output,
                              const std::string &targetTriple,
                              bool freestanding,
                              bool verifyOwnershipObligations) {
  try {
    auto mod = generateZIRModule(node, verifyOwnershipObligations);
    if (!mod) {
      driver::reportError("failed to generate ZIR");
      return true;
    }

    codegen::LLVMCodeGen llvmGen(targetTriple, freestanding);
    llvmGen.generate(*mod);
    if (!llvmGen.verifyModule(llvm::errs())) {
      return true;
    }

    llvm::raw_string_ostream rs(output);
    llvmGen.printIR(rs);
    rs.flush();
  } catch (const std::exception &ex) {
    driver::reportError("LLVM text generation failed: ", ex.what());
    return true;
  }
  return false;
}

bool compileObjectFromZIR(sema::BoundRootNode &node,
                          const std::string &output_path,
                          int optimization_level,
                          const std::string &targetTriple, bool freestanding,
                          bool verifyOwnershipObligations) {
  try {
    auto mod = generateZIRModule(node, verifyOwnershipObligations);
    if (!mod) {
      driver::reportError("failed to generate ZIR");
      return true;
    }

    codegen::LLVMCodeGen llvmGen(targetTriple, freestanding);
    llvmGen.generate(*mod);
    if (!llvmGen.emitObjectFile(output_path, optimization_level)) {
      driver::reportError("object file emission failed");
      return true;
    }
  } catch (const std::exception &ex) {
    driver::reportError("Object generation failed: ", ex.what());
    return true;
  }
  return false;
}

bool compileAssemblyFromZIR(sema::BoundRootNode &node,
                            const std::string &output_path,
                            int optimization_level,
                            const std::string &targetTriple, bool freestanding,
                            bool verifyOwnershipObligations) {
  try {
    auto mod = generateZIRModule(node, verifyOwnershipObligations);
    if (!mod) {
      driver::reportError("failed to generate ZIR");
      return true;
    }

    codegen::LLVMCodeGen llvmGen(targetTriple, freestanding);
    llvmGen.generate(*mod);
    if (!llvmGen.emitAssemblyFile(output_path, optimization_level)) {
      driver::reportError("assembly file emission failed");
      return true;
    }
  } catch (const std::exception &ex) {
    driver::reportError("Assembly generation failed: ", ex.what());
    return true;
  }
  return false;
}

bool driver::compileSourceFile(const std::string &source,
                               const std::string &source_name) {
  zap::DiagnosticEngine diagnostics(source, source_name);
  Lexer lex(diagnostics);

  auto tokens = lex.tokenize(source);

  zap::Parser parser(tokens, diagnostics);
  auto ast = parser.parse();

  if (diagnostics.hadErrors()) {
    diagnostics.printText(err());
    return true;
  }

  if (!ast) {
    diagnostics.printText(err());
    reportError(source_name, ": failed parsing the provided file");
    return true;
  }

  auto targetInfo = targetInfoForTriple(cmdArgs.targetTriple);
  if (!targetInfo) {
    reportError("unsupported target word size for '", cmdArgs.targetTriple,
                "'");
    return true;
  }
  sema::Binder binder(diagnostics, true, nullptr, *targetInfo);
  auto boundAst = binder.bind(*ast);
  diagnostics.printText(err());

  if (!boundAst) {
    reportError(source_name, ": semantic analysis failed");
    return true;
  }

  if (binary_output()) {
    std::filesystem::path out_path;

    if (cmdArgs.output.type == args::OutputType::EXEC) {
      out_path = executableObjectPath(cmdArgs.output.path, source_name);
      cleanups.emplace_back(out_path);
    } else if (cmdArgs.output.type == args::OutputType::OBJECT) {
      if (cmdArgs.output.implicit) {
        out_path = source_name + ".o";
      } else {
        out_path = cmdArgs.output.path;
      }
    }

    if (compileObjectFromZIR(*boundAst, out_path.string(),
                             static_cast<int>(cmdArgs.optLevel),
                             cmdArgs.targetTriple, cmdArgs.freestanding,
                             cmdArgs.verifyOwnershipObligations)) {
      return true;
    }

    cmdArgs.objects.emplace_back(std::move(out_path));
  } else if (cmdArgs.output.type == args::OutputType::ASM) {
    std::filesystem::path out_path = cmdArgs.output.implicit
                                         ? std::filesystem::path(source_name)
                                         : cmdArgs.output.path;
    out_path.replace_extension(
        driver::format_fileextension(args::OutputType::ASM));
    if (compileAssemblyFromZIR(*boundAst, out_path.string(),
                               static_cast<int>(cmdArgs.optLevel),
                               cmdArgs.targetTriple, cmdArgs.freestanding,
                               cmdArgs.verifyOwnershipObligations)) {
      return true;
    }
  } else if (emits_text_output()) {
    std::filesystem::path out_path = cmdArgs.output.implicit
                                         ? std::filesystem::path(source_name)
                                         : cmdArgs.output.path;
    if (emitRequestedTextOutputs(*this, *boundAst, out_path)) {
      return true;
    }
  } else {
    return true;
  }

  return false;
}

bool driver::compile() {
  for (const std::filesystem::path &input : cmdArgs.sources) {
    if (compileLoadedModules(*this, input))
      return true;
  }

  return false;
}

bool driver::link() {
  if (!needs_linking())
    return false;

  const std::filesystem::path linker = "/usr/bin/cc";
  std::vector<std::string> arguments;

  if (cmdArgs.incStdlib) {
    auto paths = frontend::RuntimePaths{
        executable_path, std::filesystem::path(ZAPC_CORE_DIR),
        std::filesystem::path(ZAPC_STDLIB_DIR),
        std::filesystem::path(ZAPC_STDLIB_PATH)};
    arguments.push_back(frontend::stdlibObjectPath(paths).string());
  } else {
    arguments.emplace_back("-nostdlib");
  }

  for (const auto &obj : cmdArgs.objects) {
    arguments.push_back(obj.string());
  }

  for (const auto &arg : cmdArgs.linkerArgs) {
    arguments.push_back(arg);
  }

  arguments.emplace_back("-lm");
  arguments.emplace_back("-o");
  arguments.push_back(cmdArgs.output.path.string());

  const auto result = process::execute(linker, arguments);
  if (result.succeeded()) {
    return false;
  }
  if (result.termination == process::Termination::LaunchFailed) {
    reportError("failed to start linker '", linker,
                "': ", result.error.message());
  } else if (result.termination == process::Termination::Signaled) {
    reportError("linker terminated by signal: ", result.code);
  } else {
    reportError("linking failed with exit code: ", result.code);
  }
  return true;
}

bool driver::cleanup() {
  bool errs = false;

  for (const std::filesystem::path &f : cleanups) {
    std::error_code ec;
    std::filesystem::remove(f, ec);
    if (ec) {
      errs = true;
      reportWarning("failed to remove: ", f, "\nreason: ", ec.message());
    }
  }

  return errs;
}

} // namespace zap

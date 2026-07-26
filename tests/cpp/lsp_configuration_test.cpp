#include "lsp/configuration.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

void writeFile(const std::filesystem::path &path, const std::string &contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << contents;
}

bool containsError(const zap::lsp::RuntimePathConfiguration &configuration,
                   std::string_view fragment) {
  for (const auto &error : configuration.errors) {
    if (error.find(fragment) != std::string::npos) {
      return true;
    }
  }
  return false;
}

struct TemporaryDirectory {
  std::filesystem::path path;

  TemporaryDirectory() {
    const auto suffix =
        std::chrono::high_resolution_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path() /
           ("zap-lsp-configuration-" + std::to_string(suffix));
    std::filesystem::create_directories(path);
  }

  ~TemporaryDirectory() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};

} // namespace

int main() {
  TemporaryDirectory temporary;
  const auto installRoot = temporary.path / "custom-zap";
  writeFile(installRoot / "core" / "core.zp", "");
  writeFile(installRoot / "std" / "prelude.zp", "");
  writeFile(temporary.path / "zaplsp.json",
            R"({"zapRoot":"custom-zap","corePath":"core","stdlibPath":"std"})");

  auto relative = zap::lsp::loadRuntimePathConfiguration(
      temporary.path, std::nullopt, std::nullopt);
  require(relative.errors.empty(), "valid relative configuration was rejected");
  require(relative.coreDir == installRoot / "core",
          "corePath was not resolved relative to zapRoot");
  require(relative.stdlibDir == installRoot / "std",
          "stdlibPath was not resolved relative to zapRoot");

  const auto overrideRoot = temporary.path / "override";
  writeFile(overrideRoot / "core" / "core.zp", "");
  writeFile(overrideRoot / "std" / "prelude.zp", "");
  auto overridden = zap::lsp::loadRuntimePathConfiguration(
      temporary.path, (overrideRoot / "core").string(),
      (overrideRoot / "std").string());
  require(overridden.errors.empty(), "valid initialization override failed");
  require(overridden.coreDir == overrideRoot / "core",
          "initialization corePath did not override zaplsp.json");
  require(overridden.stdlibDir == overrideRoot / "std",
          "initialization stdlibPath did not override zaplsp.json");

  writeFile(temporary.path / "zaplsp.json", R"({"stdLibPath":"std"})");
  auto invalid = zap::lsp::loadRuntimePathConfiguration(
      temporary.path, std::nullopt, std::nullopt);
  require(containsError(invalid, "Unknown property 'stdLibPath'"),
          "unknown configuration property was not rejected");
  require(containsError(invalid, "must define zapRoot"),
          "incomplete configuration was not rejected");
}

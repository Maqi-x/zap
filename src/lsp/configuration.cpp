#include "lsp/configuration.hpp"

#include "lsp.hpp"
#include <fstream>
#include <sstream>
#include <string_view>

namespace zap::lsp {

namespace {

constexpr std::string_view configFileName = "zaplsp.json";

bool isKnownProperty(std::string_view property) {
  return property == "$schema" || property == "zapRoot" ||
         property == "corePath" || property == "stdlibPath";
}

std::filesystem::path resolvePath(const std::filesystem::path &base,
                                  const std::string &value) {
  std::filesystem::path path(value);
  if (path.is_relative()) {
    path = base / path;
  }
  return std::filesystem::absolute(path).lexically_normal();
}

bool isLibraryDirectory(const std::filesystem::path &directory,
                        std::string_view marker) {
  std::error_code ec;
  return std::filesystem::is_directory(directory, ec) && !ec &&
         std::filesystem::is_regular_file(directory / marker, ec) && !ec;
}

void validatePath(const char *property, std::string_view source,
                  const std::filesystem::path &directory,
                  std::string_view marker, std::vector<std::string> &errors) {
  if (isLibraryDirectory(directory, marker)) {
    return;
  }
  errors.push_back("Invalid " + std::string(property) + " in " +
                   std::string(source) + ": " + directory.string() +
                   " does not contain " + std::string(marker));
}

std::optional<std::string>
optionalStringProperty(const JsonObject &object, std::string_view property,
                       std::vector<std::string> &errors) {
  const auto &properties = object.getAsObject();
  auto propertyIt = properties.find(std::string(property));
  if (propertyIt == properties.end()) {
    return std::nullopt;
  }
  const JsonObject *value = &propertyIt->second;
  if (!value->isString() || value->getAsString().empty()) {
    errors.push_back("Property '" + std::string(property) +
                     "' in zaplsp.json must be a non-empty string");
    return std::nullopt;
  }
  return value->getAsString();
}

} // namespace

RuntimePathConfiguration loadRuntimePathConfiguration(
    const std::filesystem::path &workspaceRoot,
    const std::optional<std::string> &initializationCorePath,
    const std::optional<std::string> &initializationStdlibPath) {
  RuntimePathConfiguration result;
  const auto configPath = workspaceRoot / configFileName;
  std::optional<std::string> configCorePath;
  std::optional<std::string> configStdlibPath;
  std::filesystem::path configBase = workspaceRoot;

  std::error_code ec;
  if (std::filesystem::is_regular_file(configPath, ec) && !ec) {
    std::ifstream input(configPath, std::ios::binary);
    std::ostringstream contents;
    contents << input.rdbuf();
    JsonObject config = JsonParser::parse(contents.str());
    if (!input.good() && !input.eof()) {
      result.errors.push_back("Could not read " + configPath.string());
    } else if (!config.isObject()) {
      result.errors.push_back(configPath.string() +
                              " must contain a JSON object");
    } else {
      for (const auto &[property, _] : config.getAsObject()) {
        if (!isKnownProperty(property)) {
          result.errors.push_back("Unknown property '" + property +
                                  "' in zaplsp.json");
        }
      }
      auto zapRoot = optionalStringProperty(config, "zapRoot", result.errors);
      if (zapRoot) {
        configBase = resolvePath(workspaceRoot, *zapRoot);
      }
      configCorePath =
          optionalStringProperty(config, "corePath", result.errors);
      configStdlibPath =
          optionalStringProperty(config, "stdlibPath", result.errors);
      if (zapRoot) {
        configCorePath = configCorePath.value_or("core");
        configStdlibPath = configStdlibPath.value_or("std");
      } else if (!configCorePath || !configStdlibPath) {
        result.errors.push_back(
            "zaplsp.json must define zapRoot or both corePath and stdlibPath");
      }
    }
  }

  const auto corePath =
      initializationCorePath ? initializationCorePath : configCorePath;
  const auto stdlibPath =
      initializationStdlibPath ? initializationStdlibPath : configStdlibPath;

  if (corePath) {
    const auto base = initializationCorePath ? workspaceRoot : configBase;
    result.coreDir = resolvePath(base, *corePath);
    validatePath("corePath",
                 initializationCorePath ? "initializationOptions"
                                        : "zaplsp.json",
                 *result.coreDir, "core.zp", result.errors);
  }
  if (stdlibPath) {
    const auto base = initializationStdlibPath ? workspaceRoot : configBase;
    result.stdlibDir = resolvePath(base, *stdlibPath);
    validatePath("stdlibPath",
                 initializationStdlibPath ? "initializationOptions"
                                          : "zaplsp.json",
                 *result.stdlibDir, "prelude.zp", result.errors);
  }

  return result;
}

} // namespace zap::lsp

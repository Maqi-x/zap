#include "lsp/source_manager.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {

void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

} // namespace

int main() {
  zap::lsp::SourceManager sources;
  const std::string uri = "file:///workspace/main.zp";
  const auto path = std::filesystem::temp_directory_path() / "main.zp";

  sources.open(uri, path, "first", 1);
  auto first = sources.sourceForUri(uri);
  require(first.has_value(), "opened source was not available");
  require((*first)->version == 1 && (*first)->text == "first",
          "opened source snapshot has incorrect content");

  sources.update(uri, "second", 2);
  auto second = sources.sourceForUri(uri);
  require(second.has_value(), "updated source was not available");
  require((*second)->id != (*first)->id,
          "updating a document must create a new SourceId");
  require((*second)->version == 2 && (*second)->text == "second",
          "updated source snapshot has incorrect content");
  require((*first)->version == 1 && (*first)->text == "first",
          "previous source snapshot was mutated");

  sources.close(uri);
  require(!sources.contains(uri), "closed source is still open");
}

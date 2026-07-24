#pragma once

#include "lsp/lsp.hpp"
#include <cstdint>
#include <optional>
#include <string>

namespace zap::lsp {

struct TextDocumentPosition {
  std::string uri;
  int64_t line = 0;
  int64_t character = 0;
};

std::optional<TextDocumentPosition>
decodeTextDocumentPosition(const JsonObject &request);

} // namespace zap::lsp

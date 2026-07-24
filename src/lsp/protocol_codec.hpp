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

struct OpenDocumentParams {
  std::string uri;
  std::string text;
  int64_t version = 0;
};

struct ChangeDocumentParams {
  std::string uri;
  std::string text;
  int64_t version = 0;
};

std::optional<TextDocumentPosition>
decodeTextDocumentPosition(const JsonObject &request);
std::optional<OpenDocumentParams> decodeOpenDocument(const JsonObject &request);
std::optional<ChangeDocumentParams>
decodeChangeDocument(const JsonObject &request);
std::optional<std::string> decodeCloseDocument(const JsonObject &request);

} // namespace zap::lsp

#include "lsp/protocol_codec.hpp"

#include "lsp/protocol_utils.hpp"

namespace zap::lsp {

std::optional<TextDocumentPosition>
decodeTextDocumentPosition(const JsonObject &request) {
  auto uri = getStringField(request, {"params", "textDocument", "uri"});
  auto line = getIntegerField(request, {"params", "position", "line"});
  auto character =
      getIntegerField(request, {"params", "position", "character"});
  if (!uri || !line || !character || *line < 0 || *character < 0) {
    return std::nullopt;
  }
  return TextDocumentPosition{std::move(*uri), *line, *character};
}

} // namespace zap::lsp

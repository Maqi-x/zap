#include "lsp/document_request.hpp"

#include "lsp/position_codec.hpp"
#include "lsp/protocol_codec.hpp"

namespace zap::lsp {

std::optional<DocumentRequestContext>
documentRequestContext(Workspace &workspace, const JsonObject &request) {
  auto position = decodeTextDocumentPosition(request);
  if (!position) {
    return std::nullopt;
  }
  auto query = workspace.query(position->uri);
  if (!query) {
    return std::nullopt;
  }
  const size_t offset =
      offsetFromPosition(query->document->text, position->line, position->character);
  return DocumentRequestContext{std::move(position->uri), std::move(*query), offset};
}

} // namespace zap::lsp

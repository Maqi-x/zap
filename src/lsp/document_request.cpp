#include "lsp/document_request.hpp"

#include "lsp/position_codec.hpp"
#include "lsp/protocol_utils.hpp"

namespace zap::lsp {

std::optional<DocumentRequestContext>
documentRequestContext(Workspace &workspace, const JsonObject &request) {
  auto uri = getStringField(request, {"params", "textDocument", "uri"});
  auto line = getIntegerField(request, {"params", "position", "line"});
  auto character =
      getIntegerField(request, {"params", "position", "character"});
  if (!uri || !line || !character) {
    return std::nullopt;
  }
  auto query = workspace.query(*uri);
  if (!query) {
    return std::nullopt;
  }
  const size_t offset =
      offsetFromPosition(query->document->text, *line, *character);
  return DocumentRequestContext{std::move(*uri), std::move(*query), offset};
}

} // namespace zap::lsp

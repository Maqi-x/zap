#pragma once

#include "lsp/lsp.hpp"
#include "lsp/workspace.hpp"
#include <cstddef>
#include <optional>
#include <string>

namespace zap::lsp {

struct DocumentRequestContext {
  std::string uri;
  SemanticQuery query;
  size_t offset = 0;
};

std::optional<DocumentRequestContext>
documentRequestContext(Workspace &workspace, const JsonObject &request);

} // namespace zap::lsp

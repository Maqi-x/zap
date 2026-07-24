#pragma once

#include "lsp.hpp"
#include "workspace_types.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace zap::lsp {

JsonObject makeResponse(const JsonObject *id, JsonObject result);
JsonObject makeNotification(std::string method, JsonObject params);
JsonObject makeRange(std::string_view source, size_t startOffset,
                     size_t endOffset);
JsonObject makeLocation(const std::string &uri, std::string_view source,
                        const SourceSpan &span);
JsonObject makeCompletionItem(const LspSymbol &symbol,
                              const std::string &detail = "");
JsonObject makeHover(const HoverInfo &hover);
JsonObject makeSignatureHelp(const std::vector<LspSignature> &signatures,
                             int64_t activeSignature, int64_t activeParameter);
JsonObject
makePublishDiagnostics(std::string uri,
                       const std::vector<zap::Diagnostic> &diagnostics);
void publishAnalysis(Server &server, const AnalysisResult &result);

} // namespace zap::lsp

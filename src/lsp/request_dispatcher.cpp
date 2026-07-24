#include "lsp/request_dispatcher.hpp"
#include "lsp.hpp"
#include "lsp/document_request.hpp"
#include "lsp/language_features.hpp"
#include "lsp/position_codec.hpp"
#include "lsp/protocol_codec.hpp"
#include "lsp/protocol_messages.hpp"
#include "lsp/protocol_utils.hpp"
#include "lsp/workspace.hpp"
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>

using namespace zap::lsp;

int zap::lsp::runRequestDispatcher() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  Server server;
  Workspace workspace;
  bool running = true;
  bool shutdownRequested = false;
  std::string line;

  while (running) {
    std::string message = server.processMessage(line);
    if (message.empty()) {
      break;
    }

    JsonRPC rpc(message);
    const JsonObject &request = rpc.object();
    auto method = getStringField(request, {"method"});
    const JsonObject *id = getField(request, "id");

    if (!method) {
      if (id) {
        server.sendMessage(makeErrorResponse(id, JsonRPC::InvalidRequest,
                                             "Missing method"));
      }
      server.send();
      continue;
    }

    if (*method == "initialize") {
      shutdownRequested = false;

      std::filesystem::path workspaceRoot = std::filesystem::current_path();
      auto params = decodeInitialize(request);
      if (params && params->rootUri) {
        if (auto rootPath = uriToPath(*params->rootUri)) {
          workspaceRoot = std::move(*rootPath);
        }
      } else if (params && params->rootPath) {
        workspaceRoot = std::move(*params->rootPath);
      }

      auto configurationErrors = workspace.configure(
          workspaceRoot,
          params ? params->corePath : std::nullopt,
          params ? params->stdlibPath : std::nullopt);

      JsonObject::Object syncOptions;
      syncOptions.emplace("openClose", JsonObject(true));
      syncOptions.emplace("change", JsonObject(int64_t(1)));

      JsonObject::Object capabilities;
      capabilities.emplace("textDocumentSync",
                           JsonObject(std::move(syncOptions)));
      capabilities.emplace("definitionProvider", JsonObject(true));
      capabilities.emplace("hoverProvider", JsonObject(true));

      JsonObject::Object completionOptions;
      completionOptions.emplace("resolveProvider", JsonObject(false));
      completionOptions.emplace(
          "triggerCharacters",
          JsonObject(JsonObject::List{
              JsonObject("."), JsonObject("_"), JsonObject("a"),
              JsonObject("b"), JsonObject("c"), JsonObject("d"),
              JsonObject("e"), JsonObject("f"), JsonObject("g"),
              JsonObject("h"), JsonObject("i"), JsonObject("j"),
              JsonObject("k"), JsonObject("l"), JsonObject("m"),
              JsonObject("n"), JsonObject("o"), JsonObject("p"),
              JsonObject("q"), JsonObject("r"), JsonObject("s"),
              JsonObject("t"), JsonObject("u"), JsonObject("v"),
              JsonObject("w"), JsonObject("x"), JsonObject("y"),
              JsonObject("z"), JsonObject("A"), JsonObject("B"),
              JsonObject("C"), JsonObject("D"), JsonObject("E"),
              JsonObject("F"), JsonObject("G"), JsonObject("H"),
              JsonObject("I"), JsonObject("J"), JsonObject("K"),
              JsonObject("L"), JsonObject("M"), JsonObject("N"),
              JsonObject("O"), JsonObject("P"), JsonObject("Q"),
              JsonObject("R"), JsonObject("S"), JsonObject("T"),
              JsonObject("U"), JsonObject("V"), JsonObject("W"),
              JsonObject("X"), JsonObject("Y"), JsonObject("Z")}));
      capabilities.emplace("completionProvider",
                           JsonObject(std::move(completionOptions)));

      JsonObject::Object signatureHelpOptions;
      signatureHelpOptions.emplace(
          "triggerCharacters",
          JsonObject(JsonObject::List{JsonObject("("), JsonObject(",")}));
      capabilities.emplace("signatureHelpProvider",
                           JsonObject(std::move(signatureHelpOptions)));

      JsonObject::Object serverInfo;
      serverInfo.emplace("name", JsonObject("zap-lsp"));

      JsonObject::Object result;
      result.emplace("capabilities", JsonObject(std::move(capabilities)));
      result.emplace("serverInfo", JsonObject(std::move(serverInfo)));

      server.sendMessage(makeResponse(id, JsonObject(std::move(result))));
      for (const auto &error : configurationErrors) {
        server.logMessage(Server::MessageType::Error, error);
      }
    } else if (*method == "initialized") {
      continue;
    } else if (*method == "shutdown") {
      shutdownRequested = true;
      server.sendMessage(makeResponse(id, JsonObject(nullptr)));
    } else if (*method == "exit") {
      running = false;
    } else if (*method == "textDocument/didOpen") {
      if (auto params = decodeOpenDocument(request)) {
        auto path = uriToPath(params->uri);
        if (path) {
          workspace.open(params->uri, *path, std::move(params->text),
                         params->version);
          publishAnalysis(server, workspace.analyze(params->uri));
        }
      }
    } else if (*method == "textDocument/didChange") {
      if (auto params = decodeChangeDocument(request);
          params && workspace.contains(params->uri)) {
        workspace.update(params->uri, std::move(params->text), params->version);
        publishAnalysis(server, workspace.analyze(params->uri));
      }
    } else if (*method == "textDocument/didClose") {
      if (auto uri = decodeCloseDocument(request)) {
        workspace.close(*uri);
        server.sendMessage(makePublishDiagnostics(*uri, {}));
      }
    } else if (*method == "textDocument/completion") {
      if (id) {
        JsonObject::List items;
        if (auto context = documentRequestContext(workspace, request)) {
          items = makeCompletionItems(context->uri, context->query.document->text,
                                      *context->query.project, context->offset);
          server.sendMessage(makeResponse(id, JsonObject(std::move(items))));
        } else {
          server.sendMessage(makeErrorResponse(id, JsonRPC::InvalidParams,
                                               "Invalid document position"));
        }
      }
    } else if (*method == "textDocument/definition") {
      if (id) {
        JsonObject result(nullptr);
        if (auto context = documentRequestContext(workspace, request)) {
            auto symbol = resolveDefinition(context->query.document->text, context->uri,
                                            *context->query.project, context->offset);
            if (symbol) {
              auto source = workspace.sourceForUri(symbol->uri);
              if (source) {
                result = makeLocation(symbol->uri, *source, symbol->span);
              }
          }
          server.sendMessage(makeResponse(id, std::move(result)));
        } else {
          server.sendMessage(makeErrorResponse(id, JsonRPC::InvalidParams,
                                               "Invalid document position"));
        }
      }
    } else if (*method == "textDocument/hover") {
      if (id) {
        JsonObject result(nullptr);
        if (auto context = documentRequestContext(workspace, request)) {
            auto hover = resolveHover(context->query.document->text, context->uri,
                                      *context->query.project, context->offset);
            if (hover) {
              result = makeHover(*hover);
          }
          server.sendMessage(makeResponse(id, std::move(result)));
        } else {
          server.sendMessage(makeErrorResponse(id, JsonRPC::InvalidParams,
                                               "Invalid document position"));
        }
      }
    } else if (*method == "textDocument/signatureHelp") {
      if (id) {
        JsonObject result(nullptr);
        if (auto context = documentRequestContext(workspace, request)) {
            int64_t activeParameter = 0;
            auto signatures = resolveSignatures(context->query.document->text, context->uri,
                                                *context->query.project, context->offset,
                                                activeParameter);
            if (!signatures.empty()) {
              int64_t activeSignature =
                  chooseActiveSignature(signatures, activeParameter);
              result = makeSignatureHelp(signatures, activeSignature,
                                         activeParameter);
          }
          server.sendMessage(makeResponse(id, std::move(result)));
        } else {
          server.sendMessage(makeErrorResponse(id, JsonRPC::InvalidParams,
                                               "Invalid document position"));
        }
      }
    } else {
      if (id) {
        server.sendMessage(makeErrorResponse(id, JsonRPC::MethodNotFound,
                                             "Method not found"));
      }
    }

    server.send();
  }

  return shutdownRequested ? 0 : 1;
}

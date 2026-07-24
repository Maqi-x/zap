#pragma once

#include "../ast/node.hpp"
#include "../ir/type.hpp"
#include "symbol.hpp"
#include <memory>
#include <unordered_map>

namespace sema {

struct SemanticInfo {
  std::unordered_map<const Node *, std::shared_ptr<Symbol>> symbolsByNode;
  std::unordered_map<const Symbol *, const Node *> declarationsBySymbol;
  std::unordered_map<const Node *, std::shared_ptr<zir::Type>> typesByNode;

  void recordSymbol(const Node *node, std::shared_ptr<Symbol> symbol) {
    if (node && symbol) {
      symbolsByNode[node] = std::move(symbol);
    }
  }

  void recordDeclaration(const Node *node, const std::shared_ptr<Symbol> &symbol) {
    if (node && symbol) {
      symbolsByNode[node] = symbol;
      declarationsBySymbol[symbol.get()] = node;
    }
  }

  std::shared_ptr<Symbol>
  declarationNamed(const std::string &name, const std::string &moduleName) const {
    for (const auto &[node, symbol] : symbolsByNode) {
      if (symbol && symbol->name == name && symbol->moduleName == moduleName &&
          declarationFor(symbol) == node) {
        return symbol;
      }
    }
    return nullptr;
  }

  const Node *declarationFor(const std::shared_ptr<Symbol> &symbol) const {
    if (!symbol) {
      return nullptr;
    }
    auto it = declarationsBySymbol.find(symbol.get());
    return it == declarationsBySymbol.end() ? nullptr : it->second;
  }

  void recordType(const Node *node, std::shared_ptr<zir::Type> type) {
    if (node && type) {
      typesByNode[node] = std::move(type);
    }
  }

  std::shared_ptr<Symbol> symbolFor(const Node *node) const {
    auto it = symbolsByNode.find(node);
    return it == symbolsByNode.end() ? nullptr : it->second;
  }

  std::shared_ptr<zir::Type> typeFor(const Node *node) const {
    auto it = typesByNode.find(node);
    return it == typesByNode.end() ? nullptr : it->second;
  }
};

} // namespace sema

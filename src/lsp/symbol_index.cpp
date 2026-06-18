#include "lsp/symbol_index.hpp"

#include "ast/nodes.hpp"

namespace zap::lsp {
namespace {

bool containsOffset(const SourceSpan &span, size_t offset) {
  return offset >= span.offset && offset <= span.offset + span.length;
}

bool startsBefore(const SourceSpan &span, size_t offset) {
  return span.offset <= offset;
}

void addSymbol(std::vector<LspSymbol> &symbols, const std::string &uri,
               const std::string &name, const SourceSpan &span, int64_t kind,
               Visibility visibility = Visibility::Private) {
  if (name.empty()) {
    return;
  }
  symbols.push_back(LspSymbol{name, uri, span, kind, visibility});
}

void collectFromBody(const BodyNode *body, size_t offset,
                     const std::string &uri, std::vector<LspSymbol> &symbols);

void collectFromExpression(const ExpressionNode *expr, size_t offset,
                           const std::string &uri,
                           std::vector<LspSymbol> &symbols) {
  if (!expr || !containsOffset(expr->span, offset)) {
    return;
  }

  if (auto body = dynamic_cast<const BodyNode *>(expr)) {
    collectFromBody(body, offset, uri, symbols);
    return;
  }
  if (auto fallback = dynamic_cast<const FallbackExpr *>(expr)) {
    collectFromExpression(fallback->expression_.get(), offset, uri, symbols);
    collectFromExpression(fallback->fallback_.get(), offset, uri, symbols);
    return;
  }
  if (auto handled = dynamic_cast<const FailableHandleExpr *>(expr)) {
    collectFromExpression(handled->expression_.get(), offset, uri, symbols);
    if (handled->handler_ && containsOffset(handled->handler_->span, offset)) {
      addSymbol(symbols, uri, handled->errorName_, handled->span, 6);
      collectFromBody(handled->handler_.get(), offset, uri, symbols);
    }
    return;
  }
  if (auto tryExpr = dynamic_cast<const TryExpr *>(expr)) {
    collectFromExpression(tryExpr->expression_.get(), offset, uri, symbols);
    return;
  }
  if (auto call = dynamic_cast<const FunCall *>(expr)) {
    collectFromExpression(call->callee_.get(), offset, uri, symbols);
    for (const auto &arg : call->params_) {
      if (arg) {
        collectFromExpression(arg->value.get(), offset, uri, symbols);
      }
    }
  }
}

void collectFromStatement(const Node *node, size_t offset,
                          const std::string &uri,
                          std::vector<LspSymbol> &symbols) {
  if (!node || !startsBefore(node->span, offset)) {
    return;
  }

  if (auto var = dynamic_cast<const VarDecl *>(node)) {
    if (var->span.offset <= offset) {
      addSymbol(symbols, uri, var->name_, var->span, 6, var->visibility_);
    }
    collectFromExpression(var->initializer_.get(), offset, uri, symbols);
    return;
  }
  if (auto cnst = dynamic_cast<const ConstDecl *>(node)) {
    if (cnst->span.offset <= offset) {
      addSymbol(symbols, uri, cnst->name_, cnst->span, 21, cnst->visibility_);
    }
    collectFromExpression(cnst->initializer_.get(), offset, uri, symbols);
    return;
  }
  if (auto body = dynamic_cast<const BodyNode *>(node)) {
    collectFromBody(body, offset, uri, symbols);
    return;
  }
  if (auto ifNode = dynamic_cast<const IfNode *>(node)) {
    if (!containsOffset(ifNode->span, offset)) {
      return;
    }
    collectFromExpression(ifNode->condition_.get(), offset, uri, symbols);
    collectFromBody(ifNode->thenBody_.get(), offset, uri, symbols);
    collectFromBody(ifNode->elseBody_.get(), offset, uri, symbols);
    return;
  }
  if (auto ifType = dynamic_cast<const IfTypeNode *>(node)) {
    if (!containsOffset(ifType->span, offset)) {
      return;
    }
    if (ifType->matchType_ && ifType->matchType_->span.offset <= offset) {
      addSymbol(symbols, uri, ifType->parameterName_, ifType->span, 22);
    }
    collectFromBody(ifType->thenBody_.get(), offset, uri, symbols);
    collectFromBody(ifType->elseBody_.get(), offset, uri, symbols);
    return;
  }
  if (auto whileNode = dynamic_cast<const WhileNode *>(node)) {
    if (!containsOffset(whileNode->span, offset)) {
      return;
    }
    collectFromExpression(whileNode->condition_.get(), offset, uri, symbols);
    collectFromBody(whileNode->body_.get(), offset, uri, symbols);
    return;
  }
  if (auto forNode = dynamic_cast<const ForNode *>(node)) {
    if (!containsOffset(forNode->span, offset)) {
      return;
    }
    if (forNode->initializer_) {
      collectFromStatement(forNode->initializer_.get(), offset, uri, symbols);
    }
    collectFromExpression(forNode->condition_.get(), offset, uri, symbols);
    collectFromStatement(forNode->increment_.get(), offset, uri, symbols);
    collectFromBody(forNode->body_.get(), offset, uri, symbols);
    return;
  }
  if (auto forIn = dynamic_cast<const ForInNode *>(node)) {
    if (!containsOffset(forIn->span, offset)) {
      return;
    }
    collectFromExpression(forIn->iterable_.get(), offset, uri, symbols);
    if (forIn->body_ && startsBefore(forIn->body_->span, offset)) {
      addSymbol(symbols, uri, forIn->itemName_, forIn->span, 6);
      addSymbol(symbols, uri, forIn->indexName_, forIn->span, 6);
      collectFromBody(forIn->body_.get(), offset, uri, symbols);
    }
    return;
  }
  if (auto unsafeBlock = dynamic_cast<const UnsafeBlockNode *>(node)) {
    collectFromBody(unsafeBlock, offset, uri, symbols);
    return;
  }
  if (auto assign = dynamic_cast<const AssignNode *>(node)) {
    collectFromExpression(assign->target_.get(), offset, uri, symbols);
    collectFromExpression(assign->expr_.get(), offset, uri, symbols);
    return;
  }
  if (auto call = dynamic_cast<const FunCall *>(node)) {
    collectFromExpression(call, offset, uri, symbols);
    return;
  }
  if (auto ret = dynamic_cast<const ReturnNode *>(node)) {
    collectFromExpression(ret->returnValue.get(), offset, uri, symbols);
    return;
  }
  if (auto fail = dynamic_cast<const FailNode *>(node)) {
    collectFromExpression(fail->errorValue_.get(), offset, uri, symbols);
  }
}

void collectFromBody(const BodyNode *body, size_t offset,
                     const std::string &uri, std::vector<LspSymbol> &symbols) {
  if (!body) {
    return;
  }

  for (const auto &statement : body->statements) {
    if (!statement || statement->span.offset > offset) {
      break;
    }
    collectFromStatement(statement.get(), offset, uri, symbols);
  }
  collectFromExpression(body->result.get(), offset, uri, symbols);
}

void collectFunctionLocals(const FunDecl *fun, size_t offset,
                           const std::string &uri,
                           std::vector<LspSymbol> &symbols) {
  if (!fun || !containsOffset(fun->span, offset)) {
    return;
  }
  for (const auto &param : fun->params_) {
    if (param && param->span.offset <= offset) {
      addSymbol(symbols, uri, param->name, param->span, 6);
    }
  }
  collectFromBody(fun->body_.get(), offset, uri, symbols);
}

const TypeNode *findTypeInExpression(const ExpressionNode *expr, size_t offset,
                                     std::string_view name);

const TypeNode *findTypeInBody(const BodyNode *body, size_t offset,
                               std::string_view name);

const TypeNode *findTypeInStatement(const Node *node, size_t offset,
                                    std::string_view name) {
  if (!node || !startsBefore(node->span, offset)) {
    return nullptr;
  }

  if (auto var = dynamic_cast<const VarDecl *>(node)) {
    if (var->name_ == name && var->span.offset <= offset) {
      return var->type_.get();
    }
    return findTypeInExpression(var->initializer_.get(), offset, name);
  }
  if (auto cnst = dynamic_cast<const ConstDecl *>(node)) {
    if (cnst->name_ == name && cnst->span.offset <= offset) {
      return cnst->type_.get();
    }
    return findTypeInExpression(cnst->initializer_.get(), offset, name);
  }
  if (auto body = dynamic_cast<const BodyNode *>(node)) {
    return findTypeInBody(body, offset, name);
  }
  if (auto ifNode = dynamic_cast<const IfNode *>(node)) {
    if (!containsOffset(ifNode->span, offset)) {
      return nullptr;
    }
    if (auto found =
            findTypeInExpression(ifNode->condition_.get(), offset, name)) {
      return found;
    }
    if (ifNode->thenBody_ && startsBefore(ifNode->thenBody_->span, offset)) {
      return findTypeInBody(ifNode->thenBody_.get(), offset, name);
    }
    if (ifNode->elseBody_ && startsBefore(ifNode->elseBody_->span, offset)) {
      return findTypeInBody(ifNode->elseBody_.get(), offset, name);
    }
    return nullptr;
  }
  if (auto ifType = dynamic_cast<const IfTypeNode *>(node)) {
    if (!containsOffset(ifType->span, offset)) {
      return nullptr;
    }
    if (ifType->parameterName_ == name && ifType->matchType_ &&
        ifType->matchType_->span.offset <= offset) {
      return ifType->matchType_.get();
    }
    if (ifType->thenBody_ && startsBefore(ifType->thenBody_->span, offset)) {
      return findTypeInBody(ifType->thenBody_.get(), offset, name);
    }
    if (ifType->elseBody_ && startsBefore(ifType->elseBody_->span, offset)) {
      return findTypeInBody(ifType->elseBody_.get(), offset, name);
    }
    return nullptr;
  }
  if (auto whileNode = dynamic_cast<const WhileNode *>(node)) {
    if (!containsOffset(whileNode->span, offset)) {
      return nullptr;
    }
    if (auto found =
            findTypeInExpression(whileNode->condition_.get(), offset, name)) {
      return found;
    }
    if (whileNode->body_ && startsBefore(whileNode->body_->span, offset)) {
      return findTypeInBody(whileNode->body_.get(), offset, name);
    }
    return nullptr;
  }
  if (auto forNode = dynamic_cast<const ForNode *>(node)) {
    if (!containsOffset(forNode->span, offset)) {
      return nullptr;
    }
    if (auto found =
            findTypeInStatement(forNode->initializer_.get(), offset, name)) {
      return found;
    }
    if (auto found =
            findTypeInExpression(forNode->condition_.get(), offset, name)) {
      return found;
    }
    if (auto found =
            findTypeInStatement(forNode->increment_.get(), offset, name)) {
      return found;
    }
    if (forNode->body_ && startsBefore(forNode->body_->span, offset)) {
      return findTypeInBody(forNode->body_.get(), offset, name);
    }
    return nullptr;
  }
  if (auto forIn = dynamic_cast<const ForInNode *>(node)) {
    if (!containsOffset(forIn->span, offset)) {
      return nullptr;
    }
    return findTypeInExpression(forIn->iterable_.get(), offset, name);
  }
  if (auto unsafeBlock = dynamic_cast<const UnsafeBlockNode *>(node)) {
    return findTypeInBody(unsafeBlock, offset, name);
  }
  if (auto assign = dynamic_cast<const AssignNode *>(node)) {
    if (auto found =
            findTypeInExpression(assign->target_.get(), offset, name)) {
      return found;
    }
    return findTypeInExpression(assign->expr_.get(), offset, name);
  }
  if (auto call = dynamic_cast<const FunCall *>(node)) {
    return findTypeInExpression(call, offset, name);
  }
  if (auto ret = dynamic_cast<const ReturnNode *>(node)) {
    return findTypeInExpression(ret->returnValue.get(), offset, name);
  }
  if (auto fail = dynamic_cast<const FailNode *>(node)) {
    return findTypeInExpression(fail->errorValue_.get(), offset, name);
  }
  return nullptr;
}

const TypeNode *findTypeInExpression(const ExpressionNode *expr, size_t offset,
                                     std::string_view name) {
  if (!expr || !containsOffset(expr->span, offset)) {
    return nullptr;
  }
  if (auto body = dynamic_cast<const BodyNode *>(expr)) {
    return findTypeInBody(body, offset, name);
  }
  if (auto fallback = dynamic_cast<const FallbackExpr *>(expr)) {
    if (auto found =
            findTypeInExpression(fallback->expression_.get(), offset, name)) {
      return found;
    }
    return findTypeInExpression(fallback->fallback_.get(), offset, name);
  }
  if (auto handled = dynamic_cast<const FailableHandleExpr *>(expr)) {
    if (auto found =
            findTypeInExpression(handled->expression_.get(), offset, name)) {
      return found;
    }
    if (handled->handler_ && startsBefore(handled->handler_->span, offset)) {
      return findTypeInBody(handled->handler_.get(), offset, name);
    }
    return nullptr;
  }
  if (auto tryExpr = dynamic_cast<const TryExpr *>(expr)) {
    return findTypeInExpression(tryExpr->expression_.get(), offset, name);
  }
  if (auto call = dynamic_cast<const FunCall *>(expr)) {
    if (auto found = findTypeInExpression(call->callee_.get(), offset, name)) {
      return found;
    }
    for (const auto &arg : call->params_) {
      if (arg) {
        if (auto found = findTypeInExpression(arg->value.get(), offset, name)) {
          return found;
        }
      }
    }
  }
  return nullptr;
}

const TypeNode *findTypeInBody(const BodyNode *body, size_t offset,
                               std::string_view name) {
  if (!body) {
    return nullptr;
  }

  const TypeNode *found = nullptr;
  for (const auto &statement : body->statements) {
    if (!statement || statement->span.offset > offset) {
      break;
    }
    if (auto current = findTypeInStatement(statement.get(), offset, name)) {
      found = current;
    }
  }
  if (body->result && body->result->span.offset <= offset) {
    if (auto current = findTypeInExpression(body->result.get(), offset, name)) {
      found = current;
    }
  }
  return found;
}

} // namespace

std::vector<LspSymbol> collectLocalSymbols(const RootNode &root, size_t offset,
                                           const std::string &uri) {
  std::vector<LspSymbol> symbols;
  for (const auto &child : root.children) {
    if (!child || !containsOffset(child->span, offset)) {
      continue;
    }
    if (auto fun = dynamic_cast<const FunDecl *>(child.get())) {
      collectFunctionLocals(fun, offset, uri, symbols);
      break;
    }
    if (auto cls = dynamic_cast<const ClassDecl *>(child.get())) {
      for (const auto &field : cls->fields_) {
        if (field && field->span.offset <= offset) {
          addSymbol(symbols, uri, field->name, field->span, 8,
                    field->visibility_);
        }
      }
      for (const auto &method : cls->methods_) {
        collectFunctionLocals(method.get(), offset, uri, symbols);
      }
      break;
    }
  }
  return symbols;
}

const TypeNode *findVisibleTypeNode(const RootNode &root, size_t offset,
                                    std::string_view name) {
  for (const auto &child : root.children) {
    if (!child) {
      continue;
    }
    if (auto var = dynamic_cast<const VarDecl *>(child.get())) {
      if (var->name_ == name && var->span.offset <= offset) {
        return var->type_.get();
      }
      continue;
    }
    if (auto cnst = dynamic_cast<const ConstDecl *>(child.get())) {
      if (cnst->name_ == name && cnst->span.offset <= offset) {
        return cnst->type_.get();
      }
      continue;
    }
    if (!containsOffset(child->span, offset)) {
      continue;
    }
    if (auto fun = dynamic_cast<const FunDecl *>(child.get())) {
      for (const auto &param : fun->params_) {
        if (param && param->name == name && param->span.offset <= offset) {
          return param->type.get();
        }
      }
      return findTypeInBody(fun->body_.get(), offset, name);
    }
    if (auto cls = dynamic_cast<const ClassDecl *>(child.get())) {
      for (const auto &field : cls->fields_) {
        if (field && field->name == name && field->span.offset <= offset) {
          return field->type.get();
        }
      }
      for (const auto &method : cls->methods_) {
        if (!method || !containsOffset(method->span, offset)) {
          continue;
        }
        for (const auto &param : method->params_) {
          if (param && param->name == name && param->span.offset <= offset) {
            return param->type.get();
          }
        }
        return findTypeInBody(method->body_.get(), offset, name);
      }
    }
  }
  return nullptr;
}

} // namespace zap::lsp

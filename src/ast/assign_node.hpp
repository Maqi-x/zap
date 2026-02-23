#pragma once
#include "expr_node.hpp"
#include "statement_node.hpp"
#include <string>

#include "visitor.hpp"

class AssignNode : public StatementNode {
public:
  std::string target_;
  std::unique_ptr<ExpressionNode> expr_;
  AssignNode() noexcept(std::is_nothrow_default_constructible<std::string>::value) = default;

  AssignNode(std::string target, std::unique_ptr<ExpressionNode> expr)
      : target_(target), expr_(std::move(expr)) {}

  void accept(Visitor &v) override { v.visit(*this); }
};
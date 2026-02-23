#pragma once
#include "../visitor.hpp"

class ConstFloat : public ExpressionNode {
public:
  float value_;
  ConstFloat() noexcept = default;
  ConstFloat(float value) noexcept : value_(value) {}

  void accept(Visitor &v) override { v.visit(*this); }
};
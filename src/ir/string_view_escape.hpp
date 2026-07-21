#pragma once

#include "function.hpp"

#include <memory>
#include <unordered_set>

namespace zir {

class StringViewEscapeAnalysis {
public:
  bool isFunctionLocalView(const std::shared_ptr<Value> &value) const;
  bool isFunctionLocalStorage(const std::shared_ptr<Value> &value) const;

private:
  std::unordered_set<const Value *> localViews_;
  std::unordered_set<const Value *> localStorage_;

  friend StringViewEscapeAnalysis
  analyzeStringViewEscapes(const Function &function);
};

StringViewEscapeAnalysis analyzeStringViewEscapes(const Function &function);

} // namespace zir

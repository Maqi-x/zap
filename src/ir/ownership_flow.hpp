#pragma once

#include "function.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace zir {

class Module;

enum class OwnershipFlowState : unsigned char {
  Unavailable = 0,
  Available = 1 << 0,
  Consumed = 1 << 1,
  Mixed = (1 << 0) | (1 << 1),
};

struct OwnershipTransferViolation {
  const BasicBlock *block;
  size_t instructionIndex;
  std::shared_ptr<Value> value;
  std::string operation;
};

class OwnershipFlowAnalysis {
public:
  using BlockEdges =
      std::unordered_map<const BasicBlock *, std::vector<const BasicBlock *>>;

  OwnershipFlowAnalysis(const Module &module, const Function &function,
                        const BlockEdges &predecessors,
                        const BlockEdges &successors,
                        const std::unordered_set<const BasicBlock *> &reachable);

  std::vector<OwnershipTransferViolation> analyze();
  OwnershipFlowState stateOnEdge(const BasicBlock &source,
                                 const BasicBlock &destination,
                                 const std::shared_ptr<Value> &value) const;

private:
  using OwnershipStates = std::unordered_map<const Value *, unsigned char>;
  using OwnershipEdgeStates =
      std::unordered_map<const BasicBlock *,
                         std::unordered_map<const BasicBlock *, OwnershipStates>>;

  const Module &module_;
  const Function &function_;
  BlockEdges predecessors_;
  BlockEdges successors_;
  std::unordered_set<const BasicBlock *> reachable_;
  OwnershipEdgeStates edgeStates_;
};

} // namespace zir

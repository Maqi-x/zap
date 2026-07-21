#include "control_flow_graph.hpp"

#include <deque>

namespace zir {

ControlFlowGraph::ControlFlowGraph(const Function &function) {
  for (const auto &blockOwner : function.getBlocks()) {
    if (!blockOwner) {
      continue;
    }
    blocks_.emplace(blockOwner->label, blockOwner.get());
    predecessors_.try_emplace(blockOwner.get());
    successors_.try_emplace(blockOwner.get());
  }
  for (const auto &blockOwner : function.getBlocks()) {
    if (!blockOwner) {
      continue;
    }
    const auto &block = *blockOwner;
    auto addEdge = [&](const std::string &label) {
      const auto *target = findBlock(label);
      if (!target) {
        return;
      }
      successors_[&block].push_back(target);
      predecessors_[target].push_back(&block);
    };
    for (const auto &instruction : block.getInstructions()) {
      if (!instruction) {
        continue;
      }
      if (instruction->getOpCode() == OpCode::Br) {
        addEdge(static_cast<const BranchInst &>(*instruction).getTarget());
      } else if (instruction->getOpCode() == OpCode::CondBr) {
        const auto &branch = static_cast<const CondBranchInst &>(*instruction);
        addEdge(branch.getTrueLabel());
        addEdge(branch.getFalseLabel());
      }
    }
  }
  if (function.getBlocks().empty() || !function.getBlocks().front()) {
    return;
  }
  std::deque<const BasicBlock *> worklist{function.getBlocks().front().get()};
  reachable_.insert(worklist.front());
  while (!worklist.empty()) {
    const auto *block = worklist.front();
    worklist.pop_front();
    for (const auto *successor : successors_.at(block)) {
      if (reachable_.insert(successor).second) {
        worklist.push_back(successor);
      }
    }
  }
}

const BasicBlock *ControlFlowGraph::findBlock(const std::string &label) const {
  const auto block = blocks_.find(label);
  return block == blocks_.end() ? nullptr : block->second;
}

} // namespace zir

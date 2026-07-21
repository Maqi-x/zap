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
  for (const auto *block : reachable_) {
    dominators_[block] = reachable_;
  }
  const auto *entry = function.getBlocks().front().get();
  dominators_[entry] = {entry};
  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto *block : reachable_) {
      if (block == entry) {
        continue;
      }
      std::unordered_set<const BasicBlock *> dominators;
      bool firstPredecessor = true;
      for (const auto *predecessor : predecessors_.at(block)) {
        if (!isReachable(*predecessor)) {
          continue;
        }
        if (firstPredecessor) {
          dominators = dominators_.at(predecessor);
          firstPredecessor = false;
          continue;
        }
        for (auto it = dominators.begin(); it != dominators.end();) {
          if (dominators_.at(predecessor).count(*it) == 0) {
            it = dominators.erase(it);
          } else {
            ++it;
          }
        }
      }
      dominators.insert(block);
      if (dominators_[block] != dominators) {
        dominators_[block] = std::move(dominators);
        changed = true;
      }
    }
  }
}

const BasicBlock *ControlFlowGraph::findBlock(const std::string &label) const {
  const auto block = blocks_.find(label);
  return block == blocks_.end() ? nullptr : block->second;
}

bool ControlFlowGraph::isReachable(const BasicBlock &block) const {
  return reachable_.count(&block) != 0;
}

bool ControlFlowGraph::dominates(const BasicBlock &dominator,
                                 const BasicBlock &block) const {
  const auto dominators = dominators_.find(&block);
  return dominators != dominators_.end() &&
         dominators->second.count(&dominator) != 0;
}

} // namespace zir

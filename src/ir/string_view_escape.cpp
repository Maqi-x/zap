#include "string_view_escape.hpp"

#include "control_flow_graph.hpp"
#include "string_type.hpp"

#include <unordered_map>

namespace zir {
namespace {

using StorageSet = std::unordered_set<const Value *>;

bool isLocalView(const StringViewEscapeAnalysis &analysis,
                 const std::shared_ptr<Value> &value) {
  return analysis.isFunctionLocalView(value);
}

} // namespace

bool StringViewEscapeAnalysis::isFunctionLocalView(
    const std::shared_ptr<Value> &value) const {
  return value && localViews_.count(value.get()) != 0;
}

bool StringViewEscapeAnalysis::isFunctionLocalStorage(
    const std::shared_ptr<Value> &value) const {
  return value && localStorage_.count(value.get()) != 0;
}

StringViewEscapeAnalysis analyzeStringViewEscapes(const Function &function) {
  StringViewEscapeAnalysis result;
  std::unordered_set<const Value *> keptAliveStrings;

  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto &blockOwner : function.getBlocks()) {
      if (!blockOwner) {
        continue;
      }
      for (const auto &instruction : blockOwner->getInstructions()) {
        if (!instruction) {
          continue;
        }
        if (instruction->getOpCode() == OpCode::Alloca) {
          const auto &alloca = static_cast<const AllocaInst &>(*instruction);
          changed =
              result.localStorage_.insert(alloca.getResult().get()).second ||
              changed;
        } else if (instruction->getOpCode() == OpCode::GetElementPtr) {
          const auto &gep =
              static_cast<const GetElementPtrInst &>(*instruction);
          if (result.isFunctionLocalStorage(gep.getPointer())) {
            changed =
                result.localStorage_.insert(gep.getResult().get()).second ||
                changed;
          }
        } else if (instruction->getOpCode() == OpCode::KeepAlive) {
          keptAliveStrings.insert(
              static_cast<const KeepAliveInst &>(*instruction)
                  .getValue()
                  .get());
        }
      }
    }
  }

  ControlFlowGraph cfg(function);
  std::unordered_map<const BasicBlock *, StorageSet> entryStates;
  std::unordered_map<const BasicBlock *, StorageSet> exitStates;
  changed = true;
  while (changed) {
    changed = false;
    for (const auto &blockOwner : function.getBlocks()) {
      if (!blockOwner || !cfg.isReachable(*blockOwner)) {
        continue;
      }
      const auto &block = *blockOwner;
      StorageSet storage;
      for (const auto *predecessor : cfg.predecessors().at(&block)) {
        const auto &predecessorExit = exitStates[predecessor];
        storage.insert(predecessorExit.begin(), predecessorExit.end());
      }
      if (entryStates[&block] != storage) {
        entryStates[&block] = storage;
        changed = true;
      }

      for (const auto &instruction : block.getInstructions()) {
        if (!instruction) {
          continue;
        }
        switch (instruction->getOpCode()) {
        case OpCode::Cast: {
          const auto &cast = static_cast<const CastInst &>(*instruction);
          if (cast.getResult() &&
              isIntrinsicStringViewType(cast.getResult()->getType()) &&
              (keptAliveStrings.count(cast.getSource().get()) != 0 ||
               isLocalView(result, cast.getSource()))) {
            changed =
                result.localViews_.insert(cast.getResult().get()).second ||
                changed;
          }
          break;
        }
        case OpCode::Phi: {
          const auto &phi = static_cast<const PhiInst &>(*instruction);
          if (!phi.getResult() ||
              !isIntrinsicStringViewType(phi.getResult()->getType())) {
            break;
          }
          for (const auto &[_, incoming] : phi.getIncoming()) {
            if (isLocalView(result, incoming)) {
              changed =
                  result.localViews_.insert(phi.getResult().get()).second ||
                  changed;
              break;
            }
          }
          break;
        }
        case OpCode::Store: {
          const auto &store = static_cast<const StoreInst &>(*instruction);
          if (!result.isFunctionLocalStorage(store.getDestination())) {
            break;
          }
          if (isLocalView(result, store.getSource())) {
            storage.insert(store.getDestination().get());
          } else if (store.getSource() &&
                     isIntrinsicStringViewType(store.getSource()->getType())) {
            storage.erase(store.getDestination().get());
          }
          break;
        }
        case OpCode::Load: {
          const auto &load = static_cast<const LoadInst &>(*instruction);
          if (load.getResult() &&
              isIntrinsicStringViewType(load.getResult()->getType()) &&
              storage.count(load.getSource().get()) != 0) {
            changed =
                result.localViews_.insert(load.getResult().get()).second ||
                changed;
          }
          break;
        }
        default:
          break;
        }
      }
      if (exitStates[&block] != storage) {
        exitStates[&block] = std::move(storage);
        changed = true;
      }
    }
  }
  return result;
}

} // namespace zir

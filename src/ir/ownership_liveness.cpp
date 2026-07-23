#include "ownership_liveness.hpp"

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

namespace zir {
namespace {

using ValueSet = std::unordered_set<const Value *>;
using BorrowOwners = std::unordered_map<const Value *, ValueSet>;
using StorageState = std::unordered_map<const Value *, ValueSet>;
using StorageStates = std::unordered_map<const BasicBlock *, StorageState>;

struct LocalStorageProvenance {
  ValueSet localStorage;
  StorageStates exitStates;
  std::unordered_map<const BasicBlock *, ValueSet> entryLoads;
};

bool tracksOwnership(const std::shared_ptr<Value> &value) {
  return value && value->getOwnership() == ValueOwnership::Owned &&
         containsManagedValues(value->getType());
}

void addUse(ValueSet &values, const std::shared_ptr<Value> &value,
            const BorrowOwners &borrowOwners) {
  if (tracksOwnership(value)) {
    values.insert(value.get());
  }
  if (!value) {
    return;
  }
  const auto owners = borrowOwners.find(value.get());
  if (owners != borrowOwners.end()) {
    values.insert(owners->second.begin(), owners->second.end());
  }
}

void addInstructionUses(ValueSet &values, const Instruction &instruction,
                        const BorrowOwners &borrowOwners) {
  switch (instruction.getOpCode()) {
  case OpCode::Alloca:
  case OpCode::Alloc:
  case OpCode::Br:
  case OpCode::Phi:
    return;
  case OpCode::Load:
    addUse(values, static_cast<const LoadInst &>(instruction).getSource(),
           borrowOwners);
    return;
  case OpCode::Store: {
    const auto &store = static_cast<const StoreInst &>(instruction);
    addUse(values, store.getSource(), borrowOwners);
    addUse(values, store.getDestination(), borrowOwners);
    return;
  }
  case OpCode::Add:
  case OpCode::Sub:
  case OpCode::Mul:
  case OpCode::SDiv:
  case OpCode::UDiv:
  case OpCode::SRem:
  case OpCode::URem:
  case OpCode::Shl:
  case OpCode::LShr:
  case OpCode::AShr:
  case OpCode::BitAnd:
  case OpCode::BitOr:
  case OpCode::BitXor: {
    const auto &binary = static_cast<const BinaryInst &>(instruction);
    addUse(values, binary.getLhs(), borrowOwners);
    addUse(values, binary.getRhs(), borrowOwners);
    return;
  }
  case OpCode::Cmp: {
    const auto &comparison = static_cast<const CmpInst &>(instruction);
    addUse(values, comparison.getLhs(), borrowOwners);
    addUse(values, comparison.getRhs(), borrowOwners);
    return;
  }
  case OpCode::CondBr:
    addUse(values,
           static_cast<const CondBranchInst &>(instruction).getCondition(),
           borrowOwners);
    return;
  case OpCode::Ret:
    addUse(values, static_cast<const ReturnInst &>(instruction).getValue(),
           borrowOwners);
    return;
  case OpCode::Call: {
    const auto &call = static_cast<const CallInst &>(instruction);
    addUse(values, call.getCalleeValue(), borrowOwners);
    for (const auto &argument : call.getArguments()) {
      addUse(values, argument, borrowOwners);
    }
    addUse(values, call.getVariadicPack(), borrowOwners);
    return;
  }
  case OpCode::Retain:
    addUse(values, static_cast<const RetainInst &>(instruction).getValue(),
           borrowOwners);
    return;
  case OpCode::Move:
    addUse(values, static_cast<const MoveInst &>(instruction).getSource(),
           borrowOwners);
    return;
  case OpCode::Borrow:
    addUse(values, static_cast<const BorrowInst &>(instruction).getOwner(),
           borrowOwners);
    return;
  case OpCode::Destroy:
    addUse(values, static_cast<const DestroyInst &>(instruction).getValue(),
           borrowOwners);
    return;
  case OpCode::Release:
    addUse(values, static_cast<const ReleaseInst &>(instruction).getValue(),
           borrowOwners);
    return;
  case OpCode::GetElementPtr: {
    const auto &gep = static_cast<const GetElementPtrInst &>(instruction);
    addUse(values, gep.getPointer(), borrowOwners);
    addUse(values, gep.getIndexValue(), borrowOwners);
    return;
  }
  case OpCode::Cast:
    addUse(values, static_cast<const CastInst &>(instruction).getSource(),
           borrowOwners);
    return;
  case OpCode::WeakLock:
    addUse(values,
           static_cast<const WeakLockInst &>(instruction).getWeakValue(),
           borrowOwners);
    return;
  case OpCode::WeakAlive:
    addUse(values,
           static_cast<const WeakAliveInst &>(instruction).getWeakValue(),
           borrowOwners);
    return;
  case OpCode::InlineAsm: {
    const auto &inlineAsm = static_cast<const InlineAsmInst &>(instruction);
    for (const auto &operand : inlineAsm.getOutputs()) {
      addUse(values, operand.value, borrowOwners);
    }
    for (const auto &operand : inlineAsm.getInputs()) {
      addUse(values, operand.value, borrowOwners);
    }
    return;
  }
  }
}

bool instructionUsesValue(const Instruction &instruction, const Value *value,
                          const BorrowOwners &borrowOwners) {
  if (!value) {
    return false;
  }
  ValueSet uses;
  addInstructionUses(uses, instruction, borrowOwners);
  return uses.count(value) != 0;
}

bool addBorrowOwners(BorrowOwners &owners, const Value *destination,
                     const ValueSet &sources) {
  if (!destination || sources.empty()) {
    return false;
  }
  auto &destinationOwners = owners[destination];
  const size_t previousSize = destinationOwners.size();
  destinationOwners.insert(sources.begin(), sources.end());
  return destinationOwners.size() != previousSize;
}

bool addBorrowOwnersFromValue(BorrowOwners &owners, const Value *destination,
                              const std::shared_ptr<Value> &source) {
  if (!destination || !source) {
    return false;
  }
  const auto sourceOwners = owners.find(source.get());
  return sourceOwners != owners.end() &&
         addBorrowOwners(owners, destination, sourceOwners->second);
}

BorrowOwners collectBorrowOwners(const Function &function) {
  BorrowOwners owners;
  ValueSet localStorage;
  bool storageChanged = true;
  while (storageChanged) {
    storageChanged = false;
    for (const auto &blockOwner : function.getBlocks()) {
      if (!blockOwner) {
        continue;
      }
      for (const auto &instruction : blockOwner->getInstructions()) {
        if (!instruction) {
          continue;
        }
        if (instruction->getOpCode() == OpCode::Alloca) {
          storageChanged =
              localStorage
                  .insert(static_cast<const AllocaInst &>(*instruction)
                              .getResult()
                              .get())
                  .second ||
              storageChanged;
        } else if (instruction->getOpCode() == OpCode::GetElementPtr) {
          const auto &gep =
              static_cast<const GetElementPtrInst &>(*instruction);
          if (localStorage.count(gep.getPointer().get()) != 0) {
            storageChanged =
                localStorage.insert(gep.getResult().get()).second ||
                storageChanged;
          }
        }
      }
    }
  }

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
        switch (instruction->getOpCode()) {
        case OpCode::Borrow: {
          const auto &borrow = static_cast<const BorrowInst &>(*instruction);
          if (borrow.getResult() && tracksOwnership(borrow.getOwner())) {
            changed = addBorrowOwners(owners, borrow.getResult().get(),
                                      {borrow.getOwner().get()}) ||
                      changed;
          }
          break;
        }
        case OpCode::Phi: {
          const auto &phi = static_cast<const PhiInst &>(*instruction);
          if (!phi.getResult()) {
            break;
          }
          for (const auto &[_, incoming] : phi.getIncoming()) {
            changed = addBorrowOwnersFromValue(owners, phi.getResult().get(),
                                               incoming) ||
                      changed;
          }
          break;
        }
        case OpCode::Store: {
          const auto &store = static_cast<const StoreInst &>(*instruction);
          if (store.getDestination() &&
              localStorage.count(store.getDestination().get()) != 0) {
            changed =
                addBorrowOwnersFromValue(owners, store.getDestination().get(),
                                         store.getSource()) ||
                changed;
          }
          break;
        }
        case OpCode::Load: {
          const auto &load = static_cast<const LoadInst &>(*instruction);
          if (load.getResult()) {
            changed = addBorrowOwnersFromValue(owners, load.getResult().get(),
                                               load.getSource()) ||
                      changed;
          }
          break;
        }
        case OpCode::Cast: {
          const auto &cast = static_cast<const CastInst &>(*instruction);
          if (cast.getResult() && cast.getSource() &&
              cast.getResult()->getType()->getIntrinsicKind() ==
                  IntrinsicTypeKind::StringView &&
              cast.getSource()->getType()->getIntrinsicKind() ==
                  IntrinsicTypeKind::StringView) {
            changed = addBorrowOwnersFromValue(owners, cast.getResult().get(),
                                               cast.getSource()) ||
                      changed;
          }
          break;
        }
        default:
          break;
        }
      }
    }
  }
  return owners;
}

ValueSet collectLocalStorage(const Function &function) {
  ValueSet localStorage;
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
          changed = localStorage
                        .insert(static_cast<const AllocaInst &>(*instruction)
                                    .getResult()
                                    .get())
                        .second ||
                    changed;
        } else if (instruction->getOpCode() == OpCode::GetElementPtr) {
          const auto &gep =
              static_cast<const GetElementPtrInst &>(*instruction);
          if (localStorage.count(gep.getPointer().get()) != 0) {
            changed =
                localStorage.insert(gep.getResult().get()).second || changed;
          }
        }
      }
    }
  }
  return localStorage;
}

void unionStorageStates(StorageState &destination, const StorageState &source) {
  for (const auto &[slot, owners] : source) {
    destination[slot].insert(owners.begin(), owners.end());
  }
}

LocalStorageProvenance analyzeLocalStorageProvenance(
    const Function &function, const BorrowOwners &borrowOwners,
    const std::unordered_map<const BasicBlock *,
                             std::vector<const BasicBlock *>> &predecessors) {
  LocalStorageProvenance result;
  result.localStorage = collectLocalStorage(function);

  for (const auto &blockOwner : function.getBlocks()) {
    if (!blockOwner) {
      continue;
    }
    ValueSet writtenStorage;
    for (const auto &instruction : blockOwner->getInstructions()) {
      if (!instruction) {
        continue;
      }
      if (instruction->getOpCode() == OpCode::Store) {
        const auto &store = static_cast<const StoreInst &>(*instruction);
        if (store.getDestination() &&
            result.localStorage.count(store.getDestination().get()) != 0) {
          writtenStorage.insert(store.getDestination().get());
        }
      } else if (instruction->getOpCode() == OpCode::Load) {
        const auto &load = static_cast<const LoadInst &>(*instruction);
        if (load.getResult() && load.getSource() &&
            result.localStorage.count(load.getSource().get()) != 0 &&
            writtenStorage.count(load.getSource().get()) == 0) {
          result.entryLoads[blockOwner.get()].insert(load.getResult().get());
        }
      }
    }
  }

  StorageStates entryStates;
  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto &blockOwner : function.getBlocks()) {
      if (!blockOwner) {
        continue;
      }
      StorageState entry;
      const auto predecessorIt = predecessors.find(blockOwner.get());
      if (predecessorIt != predecessors.end()) {
        for (const auto *predecessor : predecessorIt->second) {
          unionStorageStates(entry, result.exitStates[predecessor]);
        }
      }
      if (entryStates[blockOwner.get()] != entry) {
        entryStates[blockOwner.get()] = entry;
        changed = true;
      }

      StorageState state = std::move(entry);
      for (const auto &instruction : blockOwner->getInstructions()) {
        if (!instruction || instruction->getOpCode() != OpCode::Store) {
          continue;
        }
        const auto &store = static_cast<const StoreInst &>(*instruction);
        if (!store.getDestination() ||
            result.localStorage.count(store.getDestination().get()) == 0) {
          continue;
        }
        ValueSet owners;
        if (store.getSource()) {
          const auto ownerIt = borrowOwners.find(store.getSource().get());
          if (ownerIt != borrowOwners.end()) {
            owners = ownerIt->second;
          }
        }
        if (state[store.getDestination().get()] != owners) {
          state[store.getDestination().get()] = std::move(owners);
        }
      }
      if (result.exitStates[blockOwner.get()] != state) {
        result.exitStates[blockOwner.get()] = std::move(state);
        changed = true;
      }
    }
  }
  return result;
}

std::shared_ptr<Value> instructionResult(const Instruction &instruction) {
  switch (instruction.getOpCode()) {
  case OpCode::Alloca:
    return static_cast<const AllocaInst &>(instruction).getResult();
  case OpCode::Load:
    return static_cast<const LoadInst &>(instruction).getResult();
  case OpCode::Add:
  case OpCode::Sub:
  case OpCode::Mul:
  case OpCode::SDiv:
  case OpCode::UDiv:
  case OpCode::SRem:
  case OpCode::URem:
  case OpCode::Shl:
  case OpCode::LShr:
  case OpCode::AShr:
  case OpCode::BitAnd:
  case OpCode::BitOr:
  case OpCode::BitXor:
    return static_cast<const BinaryInst &>(instruction).getResult();
  case OpCode::Cmp:
    return static_cast<const CmpInst &>(instruction).getResult();
  case OpCode::Call:
    return static_cast<const CallInst &>(instruction).getResult();
  case OpCode::Alloc:
    return static_cast<const AllocInst &>(instruction).getResult();
  case OpCode::GetElementPtr:
    return static_cast<const GetElementPtrInst &>(instruction).getResult();
  case OpCode::Phi:
    return static_cast<const PhiInst &>(instruction).getResult();
  case OpCode::Cast:
    return static_cast<const CastInst &>(instruction).getResult();
  case OpCode::Move:
    return static_cast<const MoveInst &>(instruction).getResult();
  case OpCode::Borrow:
    return static_cast<const BorrowInst &>(instruction).getResult();
  case OpCode::WeakLock:
    return static_cast<const WeakLockInst &>(instruction).getResult();
  case OpCode::WeakAlive:
    return static_cast<const WeakAliveInst &>(instruction).getResult();
  case OpCode::Store:
  case OpCode::Br:
  case OpCode::CondBr:
  case OpCode::Ret:
  case OpCode::Retain:
  case OpCode::Destroy:
  case OpCode::Release:
  case OpCode::InlineAsm:
    return nullptr;
  }
  return nullptr;
}

void unionInto(ValueSet &destination, const ValueSet &source) {
  destination.insert(source.begin(), source.end());
}

void replacePhiResultWithIncoming(ValueSet &live,
                                  const std::shared_ptr<Value> &result,
                                  const std::shared_ptr<Value> &incoming,
                                  const BorrowOwners &borrowOwners) {
  if (!result) {
    return;
  }

  if (tracksOwnership(result)) {
    live.erase(result.get());
  }
  const auto owners = borrowOwners.find(result.get());
  if (owners != borrowOwners.end()) {
    for (const auto *owner : owners->second) {
      live.erase(owner);
    }
  }
  addUse(live, incoming, borrowOwners);
}

void replaceLoadResultWithStorage(ValueSet &live, const LoadInst &load,
                                  const StorageState &storage,
                                  const BorrowOwners &borrowOwners) {
  if (!load.getResult() || !load.getSource()) {
    return;
  }
  const auto knownOwners = borrowOwners.find(load.getResult().get());
  if (knownOwners == borrowOwners.end()) {
    return;
  }
  for (const auto *owner : knownOwners->second) {
    live.erase(owner);
  }
  const auto actualOwners = storage.find(load.getSource().get());
  if (actualOwners != storage.end()) {
    live.insert(actualOwners->second.begin(), actualOwners->second.end());
  }
}

} // namespace

bool OwnershipLiveness::isLiveAtBlockEntry(
    const BasicBlock &block, const std::shared_ptr<Value> &value) const {
  const auto states = entryStates_.find(&block);
  return value && states != entryStates_.end() &&
         states->second.count(value.get()) != 0;
}

bool OwnershipLiveness::isLiveAfter(const BasicBlock &block,
                                    size_t instructionIndex,
                                    const std::shared_ptr<Value> &value) const {
  const auto blockStates = afterStates_.find(&block);
  if (!value || blockStates == afterStates_.end()) {
    return false;
  }
  const auto states = blockStates->second.find(instructionIndex);
  return states != blockStates->second.end() &&
         states->second.count(value.get()) != 0;
}

bool OwnershipLiveness::isLastUse(const BasicBlock &block,
                                  size_t instructionIndex,
                                  const std::shared_ptr<Value> &value) const {
  if (!value || instructionIndex >= block.getInstructions().size() ||
      !block.getInstructions()[instructionIndex]) {
    return false;
  }
  return instructionUsesValue(*block.getInstructions()[instructionIndex],
                              value.get(), borrowOwners_) &&
         !isLiveAfter(block, instructionIndex, value);
}

bool OwnershipLiveness::isLiveOnEdge(
    const BasicBlock &source, const BasicBlock &destination,
    const std::shared_ptr<Value> &value) const {
  const auto sourceStates = edgeStates_.find(&source);
  if (!value || sourceStates == edgeStates_.end()) {
    return false;
  }
  const auto states = sourceStates->second.find(&destination);
  return states != sourceStates->second.end() &&
         states->second.count(value.get()) != 0;
}

OwnershipLiveness analyzeOwnershipLiveness(const Function &function) {
  OwnershipLiveness result;
  result.borrowOwners_ = collectBorrowOwners(function);
  std::unordered_map<std::string, const BasicBlock *> blocks;
  std::unordered_map<const BasicBlock *, std::vector<const BasicBlock *>>
      successors;

  for (const auto &blockOwner : function.getBlocks()) {
    if (blockOwner) {
      blocks.emplace(blockOwner->label, blockOwner.get());
      successors.emplace(blockOwner.get(), std::vector<const BasicBlock *>{});
    }
  }

  std::unordered_map<const BasicBlock *, std::vector<const BasicBlock *>>
      predecessors;
  for (const auto &[source, targets] : successors) {
    for (const auto *target : targets) {
      predecessors[target].push_back(source);
    }
  }
  const auto localStorage = analyzeLocalStorageProvenance(
      function, result.borrowOwners_, predecessors);
  for (const auto &blockOwner : function.getBlocks()) {
    if (!blockOwner || blockOwner->getInstructions().empty()) {
      continue;
    }
    const auto &terminator = *blockOwner->getInstructions().back();
    auto addSuccessor = [&](const std::string &label) {
      const auto target = blocks.find(label);
      if (target != blocks.end()) {
        successors[blockOwner.get()].push_back(target->second);
      }
    };
    switch (terminator.getOpCode()) {
    case OpCode::Br:
      addSuccessor(static_cast<const BranchInst &>(terminator).getTarget());
      break;
    case OpCode::CondBr: {
      const auto &branch = static_cast<const CondBranchInst &>(terminator);
      addSuccessor(branch.getTrueLabel());
      addSuccessor(branch.getFalseLabel());
      break;
    }
    default:
      break;
    }
  }

  bool changed = true;
  while (changed) {
    changed = false;
    for (auto blockIt = function.getBlocks().rbegin();
         blockIt != function.getBlocks().rend(); ++blockIt) {
      if (!*blockIt) {
        continue;
      }
      const auto &block = **blockIt;
      ValueSet liveOut;
      for (const auto *successor : successors[&block]) {
        ValueSet edgeState = result.entryStates_[successor];
        for (const auto &instruction : successor->getInstructions()) {
          if (!instruction || instruction->getOpCode() != OpCode::Phi) {
            continue;
          }
          const auto &phi = static_cast<const PhiInst &>(*instruction);
          for (const auto &[label, value] : phi.getIncoming()) {
            if (label == block.label) {
              replacePhiResultWithIncoming(edgeState, phi.getResult(), value,
                                           result.borrowOwners_);
              break;
            }
          }
        }
        const auto entryLoads = localStorage.entryLoads.find(successor);
        if (entryLoads != localStorage.entryLoads.end()) {
          const auto storage = localStorage.exitStates.find(&block);
          if (storage != localStorage.exitStates.end()) {
            for (const auto &instruction : successor->getInstructions()) {
              if (!instruction || instruction->getOpCode() != OpCode::Load ||
                  entryLoads->second.count(
                      static_cast<const LoadInst &>(*instruction)
                          .getResult()
                          .get()) == 0) {
                continue;
              }
              replaceLoadResultWithStorage(
                  edgeState, static_cast<const LoadInst &>(*instruction),
                  storage->second, result.borrowOwners_);
            }
          }
        }
        if (result.edgeStates_[&block][successor] != edgeState) {
          result.edgeStates_[&block][successor] = edgeState;
          changed = true;
        }
        unionInto(liveOut, edgeState);
      }

      ValueSet live = std::move(liveOut);
      OwnershipLiveness::InstructionStates afterStates;
      for (size_t i = block.getInstructions().size(); i-- > 0;) {
        const auto &instruction = block.getInstructions()[i];
        if (!instruction) {
          continue;
        }
        afterStates.emplace(i, live);
        if (const auto value = instructionResult(*instruction);
            tracksOwnership(value)) {
          live.erase(value.get());
        }
        addInstructionUses(live, *instruction, result.borrowOwners_);
      }
      if (result.entryStates_[&block] != live) {
        result.entryStates_[&block] = std::move(live);
        changed = true;
      }
      result.afterStates_[&block] = std::move(afterStates);
    }
  }
  return result;
}

} // namespace zir

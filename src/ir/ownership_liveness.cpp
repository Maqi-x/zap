#include "ownership_liveness.hpp"

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

namespace zir {
namespace {

using ValueSet = std::unordered_set<const Value *>;

bool tracksOwnership(const std::shared_ptr<Value> &value) {
  return value && value->getOwnership() == ValueOwnership::Owned &&
         containsManagedValues(value->getType());
}

void addUse(ValueSet &values, const std::shared_ptr<Value> &value) {
  if (tracksOwnership(value)) {
    values.insert(value.get());
  }
}

void addInstructionUses(ValueSet &values, const Instruction &instruction) {
  switch (instruction.getOpCode()) {
  case OpCode::Alloca:
  case OpCode::Alloc:
  case OpCode::Br:
  case OpCode::Phi:
    return;
  case OpCode::Load:
    addUse(values, static_cast<const LoadInst &>(instruction).getSource());
    return;
  case OpCode::Store: {
    const auto &store = static_cast<const StoreInst &>(instruction);
    addUse(values, store.getSource());
    addUse(values, store.getDestination());
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
    addUse(values, binary.getLhs());
    addUse(values, binary.getRhs());
    return;
  }
  case OpCode::Cmp: {
    const auto &comparison = static_cast<const CmpInst &>(instruction);
    addUse(values, comparison.getLhs());
    addUse(values, comparison.getRhs());
    return;
  }
  case OpCode::CondBr:
    addUse(values,
           static_cast<const CondBranchInst &>(instruction).getCondition());
    return;
  case OpCode::Ret:
    addUse(values, static_cast<const ReturnInst &>(instruction).getValue());
    return;
  case OpCode::Call: {
    const auto &call = static_cast<const CallInst &>(instruction);
    addUse(values, call.getCalleeValue());
    for (const auto &argument : call.getArguments()) {
      addUse(values, argument);
    }
    addUse(values, call.getVariadicPack());
    return;
  }
  case OpCode::Retain:
    addUse(values, static_cast<const RetainInst &>(instruction).getValue());
    return;
  case OpCode::Release:
    addUse(values, static_cast<const ReleaseInst &>(instruction).getValue());
    return;
  case OpCode::GetElementPtr: {
    const auto &gep = static_cast<const GetElementPtrInst &>(instruction);
    addUse(values, gep.getPointer());
    addUse(values, gep.getIndexValue());
    return;
  }
  case OpCode::Cast:
    addUse(values, static_cast<const CastInst &>(instruction).getSource());
    return;
  case OpCode::WeakLock:
    addUse(values, static_cast<const WeakLockInst &>(instruction).getWeakValue());
    return;
  case OpCode::WeakAlive:
    addUse(values,
           static_cast<const WeakAliveInst &>(instruction).getWeakValue());
    return;
  case OpCode::InlineAsm: {
    const auto &inlineAsm = static_cast<const InlineAsmInst &>(instruction);
    for (const auto &operand : inlineAsm.getOutputs()) {
      addUse(values, operand.value);
    }
    for (const auto &operand : inlineAsm.getInputs()) {
      addUse(values, operand.value);
    }
    return;
  }
  }
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
  case OpCode::WeakLock:
    return static_cast<const WeakLockInst &>(instruction).getResult();
  case OpCode::WeakAlive:
    return static_cast<const WeakAliveInst &>(instruction).getResult();
  case OpCode::Store:
  case OpCode::Br:
  case OpCode::CondBr:
  case OpCode::Ret:
  case OpCode::Retain:
  case OpCode::Release:
  case OpCode::InlineAsm:
    return nullptr;
  }
  return nullptr;
}

void unionInto(ValueSet &destination, const ValueSet &source) {
  destination.insert(source.begin(), source.end());
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
  std::unordered_map<std::string, const BasicBlock *> blocks;
  std::unordered_map<const BasicBlock *, std::vector<const BasicBlock *>>
      successors;

  for (const auto &blockOwner : function.getBlocks()) {
    if (blockOwner) {
      blocks.emplace(blockOwner->label, blockOwner.get());
      successors.emplace(blockOwner.get(), std::vector<const BasicBlock *>{});
    }
  }
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
          if (tracksOwnership(phi.getResult())) {
            edgeState.erase(phi.getResult().get());
          }
          for (const auto &[label, value] : phi.getIncoming()) {
            if (label == block.label) {
              addUse(edgeState, value);
              break;
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
        addInstructionUses(live, *instruction);
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

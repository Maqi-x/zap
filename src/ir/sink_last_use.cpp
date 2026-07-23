#include "sink_last_use.hpp"

#include "control_flow_graph.hpp"

#include <deque>
#include <unordered_map>
#include <unordered_set>

namespace zir {
namespace {

struct Definition {
  BasicBlock *block;
  size_t index;
};

struct ScanState {
  const BasicBlock *block;
  size_t index;

  bool operator==(const ScanState &other) const {
    return block == other.block && index == other.index;
  }
};

struct ScanStateHash {
  size_t operator()(const ScanState &state) const {
    return std::hash<const BasicBlock *>{}(state.block) ^
           (std::hash<size_t>{}(state.index) << 1U);
  }
};

std::shared_ptr<Value> instructionResult(const Instruction &instruction) {
  switch (instruction.getOpCode()) {
  case OpCode::Alloca:
    return static_cast<const AllocaInst &>(instruction).getResult();
  case OpCode::Load:
    return static_cast<const LoadInst &>(instruction).getResult();
  case OpCode::Take:
    return static_cast<const TakeInst &>(instruction).getResult();
  case OpCode::Copy:
    return static_cast<const CopyInst &>(instruction).getResult();
  case OpCode::Move:
    return static_cast<const MoveInst &>(instruction).getResult();
  default:
    return nullptr;
  }
}

bool usesValueDirectly(const Instruction &instruction, const Value *value) {
  auto isValue = [value](const std::shared_ptr<Value> &candidate) {
    return candidate && candidate.get() == value;
  };
  switch (instruction.getOpCode()) {
  case OpCode::Alloca:
  case OpCode::Alloc:
  case OpCode::Br:
    return false;
  case OpCode::Load:
    return isValue(static_cast<const LoadInst &>(instruction).getSource());
  case OpCode::Take:
    return isValue(static_cast<const TakeInst &>(instruction).getSource());
  case OpCode::Store: {
    const auto &store = static_cast<const StoreInst &>(instruction);
    return isValue(store.getSource()) || isValue(store.getDestination());
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
    return isValue(binary.getLhs()) || isValue(binary.getRhs());
  }
  case OpCode::Cmp: {
    const auto &comparison = static_cast<const CmpInst &>(instruction);
    return isValue(comparison.getLhs()) || isValue(comparison.getRhs());
  }
  case OpCode::CondBr:
    return isValue(
        static_cast<const CondBranchInst &>(instruction).getCondition());
  case OpCode::Ret:
    return isValue(static_cast<const ReturnInst &>(instruction).getValue());
  case OpCode::Call: {
    const auto &call = static_cast<const CallInst &>(instruction);
    if (isValue(call.getCalleeValue()) || isValue(call.getVariadicPack())) {
      return true;
    }
    for (const auto &argument : call.getArguments()) {
      if (isValue(argument)) {
        return true;
      }
    }
    return false;
  }
  case OpCode::Retain:
    return isValue(static_cast<const RetainInst &>(instruction).getValue());
  case OpCode::Copy:
    return isValue(static_cast<const CopyInst &>(instruction).getSource());
  case OpCode::Move:
    return isValue(static_cast<const MoveInst &>(instruction).getSource());
  case OpCode::Borrow:
    return isValue(static_cast<const BorrowInst &>(instruction).getOwner());
  case OpCode::Destroy:
    return isValue(static_cast<const DestroyInst &>(instruction).getValue());
  case OpCode::Release:
    return isValue(static_cast<const ReleaseInst &>(instruction).getValue());
  case OpCode::GetElementPtr: {
    const auto &gep = static_cast<const GetElementPtrInst &>(instruction);
    return isValue(gep.getPointer()) || isValue(gep.getIndexValue());
  }
  case OpCode::Phi: {
    const auto &phi = static_cast<const PhiInst &>(instruction);
    for (const auto &[_, incoming] : phi.getIncoming()) {
      if (isValue(incoming)) {
        return true;
      }
    }
    return false;
  }
  case OpCode::Cast:
    return isValue(static_cast<const CastInst &>(instruction).getSource());
  case OpCode::WeakLock:
    return isValue(
        static_cast<const WeakLockInst &>(instruction).getWeakValue());
  case OpCode::WeakAlive:
    return isValue(
        static_cast<const WeakAliveInst &>(instruction).getWeakValue());
  case OpCode::InlineAsm: {
    const auto &inlineAsm = static_cast<const InlineAsmInst &>(instruction);
    for (const auto &operand : inlineAsm.getOutputs()) {
      if (isValue(operand.value)) {
        return true;
      }
    }
    for (const auto &operand : inlineAsm.getInputs()) {
      if (isValue(operand.value)) {
        return true;
      }
    }
    return false;
  }
  }
  return false;
}

ParameterOwnership parameterOwnership(const Module &module,
                                      const CallInst &call, size_t index) {
  if (call.isIndirect()) {
    const auto functionType =
        call.getCalleeValue()
            ? std::dynamic_pointer_cast<FunctionPointerType>(
                  call.getCalleeValue()->getType())
            : nullptr;
    return functionType && index < functionType->getParameterOwnership().size()
               ? functionType->getParameterOwnership()[index]
               : ParameterOwnership::Borrow;
  }
  const auto *callee = module.findFunction(call.getFunctionName());
  return callee && index < callee->getArguments().size()
             ? callee->getArguments()[index]->getParameterOwnership()
             : ParameterOwnership::Borrow;
}

bool hasOnlyDirectStorageUses(const Function &function, const Value *slot) {
  for (const auto &blockOwner : function.getBlocks()) {
    if (!blockOwner) {
      continue;
    }
    for (const auto &instruction : blockOwner->getInstructions()) {
      if (!instruction || !usesValueDirectly(*instruction, slot)) {
        continue;
      }
      if (instruction->getOpCode() == OpCode::Load ||
          instruction->getOpCode() == OpCode::Take) {
        continue;
      }
      if (instruction->getOpCode() == OpCode::Store &&
          static_cast<const StoreInst &>(*instruction)
                  .getDestination()
                  .get() == slot) {
        continue;
      }
      return false;
    }
  }
  return true;
}

bool hasDependentBorrow(
    const Function &function,
    const std::unordered_map<const Value *, Definition> &definitions,
    const Value *slot) {
  for (const auto &blockOwner : function.getBlocks()) {
    if (!blockOwner) {
      continue;
    }
    for (const auto &instruction : blockOwner->getInstructions()) {
      if (!instruction || instruction->getOpCode() != OpCode::Borrow) {
        continue;
      }
      const auto &owner =
          static_cast<const BorrowInst &>(*instruction).getOwner();
      const auto definition =
          owner ? definitions.find(owner.get()) : definitions.end();
      if (definition == definitions.end()) {
        continue;
      }
      const auto &ownerInstruction =
          definition->second.block->getInstructions()[definition->second.index];
      if (ownerInstruction &&
          ownerInstruction->getOpCode() == OpCode::Load &&
          static_cast<const LoadInst &>(*ownerInstruction)
                  .getSource()
                  .get() == slot) {
        return true;
      }
    }
  }
  return false;
}

bool hasStorageAccess(const Instruction &instruction, const Value *slot) {
  if (instruction.getOpCode() == OpCode::Load) {
    return static_cast<const LoadInst &>(instruction).getSource().get() == slot;
  }
  if (instruction.getOpCode() == OpCode::Take) {
    return static_cast<const TakeInst &>(instruction).getSource().get() == slot;
  }
  if (instruction.getOpCode() == OpCode::Store) {
    return static_cast<const StoreInst &>(instruction)
               .getDestination()
               .get() == slot;
  }
  return false;
}

bool isLastStorageRead(const ControlFlowGraph &cfg, const BasicBlock &callBlock,
                       size_t callIndex, const Value *slot) {
  std::deque<ScanState> worklist{{&callBlock, callIndex + 1}};
  std::unordered_set<ScanState, ScanStateHash> visited;
  while (!worklist.empty()) {
    const auto state = worklist.front();
    worklist.pop_front();
    if (!visited.insert(state).second) {
      continue;
    }
    const auto &instructions = state.block->getInstructions();
    bool overwritten = false;
    for (size_t i = state.index; i < instructions.size(); ++i) {
      const auto &instruction = instructions[i];
      if (!instruction) {
        continue;
      }
      if (instruction->getOpCode() == OpCode::Store &&
          static_cast<const StoreInst &>(*instruction)
                  .getDestination()
                  .get() == slot) {
        overwritten = true;
        break;
      }
      if ((instruction->getOpCode() == OpCode::Load &&
           static_cast<const LoadInst &>(*instruction).getSource().get() ==
               slot) ||
          (instruction->getOpCode() == OpCode::Take &&
           static_cast<const TakeInst &>(*instruction).getSource().get() ==
               slot)) {
        return false;
      }
    }
    if (overwritten) {
      continue;
    }
    const auto successors = cfg.successors().find(state.block);
    if (successors == cfg.successors().end()) {
      continue;
    }
    for (const auto *successor : successors->second) {
      worklist.push_back({successor, 0});
    }
  }
  return true;
}

bool optimizeOne(Module &module, Function &function) {
  std::unordered_map<const Value *, Definition> definitions;
  std::unordered_map<const Value *, size_t> useCounts;
  std::unordered_set<const Value *> localSlots;
  for (const auto &blockOwner : function.getBlocks()) {
    if (!blockOwner) {
      continue;
    }
    for (size_t i = 0; i < blockOwner->instructions.size(); ++i) {
      const auto &instruction = blockOwner->instructions[i];
      if (!instruction) {
        continue;
      }
      if (const auto result = instructionResult(*instruction)) {
        definitions[result.get()] = {blockOwner.get(), i};
      }
      if (instruction->getOpCode() == OpCode::Alloca) {
        localSlots.insert(
            static_cast<const AllocaInst &>(*instruction).getResult().get());
      }
    }
  }
  for (const auto &blockOwner : function.getBlocks()) {
    if (!blockOwner) {
      continue;
    }
    for (const auto &instruction : blockOwner->getInstructions()) {
      if (!instruction) {
        continue;
      }
      for (const auto &[value, _] : definitions) {
        if (usesValueDirectly(*instruction, value)) {
          ++useCounts[value];
        }
      }
    }
  }

  ControlFlowGraph cfg(function);
  for (const auto &blockOwner : function.getBlocks()) {
    if (!blockOwner || !cfg.isReachable(*blockOwner)) {
      continue;
    }
    auto &instructions = blockOwner->instructions;
    for (size_t callIndex = 0; callIndex < instructions.size(); ++callIndex) {
      if (!instructions[callIndex] ||
          instructions[callIndex]->getOpCode() != OpCode::Call) {
        continue;
      }
      const auto &call = static_cast<const CallInst &>(*instructions[callIndex]);
      for (size_t argumentIndex = 0;
           argumentIndex < call.getArguments().size(); ++argumentIndex) {
        if (parameterOwnership(module, call, argumentIndex) !=
            ParameterOwnership::Sink) {
          continue;
        }
        const auto &argument = call.getArguments()[argumentIndex];
        const auto copyDefinition =
            argument ? definitions.find(argument.get()) : definitions.end();
        if (copyDefinition == definitions.end() ||
            copyDefinition->second.block != blockOwner.get() ||
            copyDefinition->second.index >= callIndex ||
            useCounts[argument.get()] != 1) {
          continue;
        }
        const size_t copyIndex = copyDefinition->second.index;
        const auto *copy = dynamic_cast<const CopyInst *>(
            instructions[copyIndex].get());
        const auto loadDefinition =
            copy && copy->getSource()
                ? definitions.find(copy->getSource().get())
                : definitions.end();
        if (!copy || loadDefinition == definitions.end() ||
            loadDefinition->second.block != blockOwner.get() ||
            loadDefinition->second.index + 1 != copyIndex ||
            useCounts[copy->getSource().get()] != 1) {
          continue;
        }
        const size_t loadIndex = loadDefinition->second.index;
        const auto *load =
            dynamic_cast<const LoadInst *>(instructions[loadIndex].get());
        const auto &slot = load ? load->getSource() : nullptr;
        if (!load || !slot || localSlots.count(slot.get()) == 0 ||
            !hasOnlyDirectStorageUses(function, slot.get()) ||
            hasDependentBorrow(function, definitions, slot.get())) {
          continue;
        }
        bool accessedBeforeCall = false;
        for (size_t i = copyIndex + 1; i < callIndex; ++i) {
          if (instructions[i] &&
              hasStorageAccess(*instructions[i], slot.get())) {
            accessedBeforeCall = true;
            break;
          }
        }
        if (accessedBeforeCall ||
            !isLastStorageRead(cfg, *blockOwner, callIndex, slot.get())) {
          continue;
        }
        instructions[loadIndex] =
            std::make_unique<TakeInst>(copy->getResult(), slot);
        instructions.erase(instructions.begin() +
                           static_cast<std::ptrdiff_t>(copyIndex));
        return true;
      }
    }
  }
  return false;
}

} // namespace

size_t optimizeSinkArgumentMoves(Module &module) {
  size_t optimized = 0;
  for (const auto &function : module.getFunctions()) {
    if (!function) {
      continue;
    }
    while (optimizeOne(module, *function)) {
      ++optimized;
    }
  }
  return optimized;
}

} // namespace zir

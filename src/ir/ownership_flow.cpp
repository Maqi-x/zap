#include "ownership_flow.hpp"

#include "module.hpp"

#include <vector>

namespace zir {
namespace {

constexpr unsigned char available =
    static_cast<unsigned char>(OwnershipFlowState::Available);
constexpr unsigned char consumed =
    static_cast<unsigned char>(OwnershipFlowState::Consumed);
constexpr unsigned char unavailable =
    static_cast<unsigned char>(OwnershipFlowState::Unavailable);

bool ownsManagedValue(const std::shared_ptr<Value> &value) {
  return value && value->getOwnership() == ValueOwnership::Owned &&
         containsManagedValues(value->getType());
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
  case OpCode::Release:
  case OpCode::InlineAsm:
    return nullptr;
  }
  return nullptr;
}

std::vector<const Value *> collectOwnedValues(const Function &function) {
  std::vector<const Value *> values;
  std::unordered_set<const Value *> seen;
  auto add = [&](const std::shared_ptr<Value> &value) {
    if (ownsManagedValue(value) && seen.insert(value.get()).second) {
      values.push_back(value.get());
    }
  };
  for (const auto &argument : function.getArguments()) {
    add(argument);
  }
  for (const auto &blockOwner : function.getBlocks()) {
    if (!blockOwner) {
      continue;
    }
    for (const auto &instruction : blockOwner->getInstructions()) {
      if (instruction) {
        add(instructionResult(*instruction));
      }
    }
  }
  return values;
}

bool isBorrowedMethodSelf(const Module &module, const CallInst &call,
                          size_t argumentIndex) {
  if (call.isIndirect() || argumentIndex != 0) {
    return false;
  }
  const auto *callee = module.findFunction(call.getFunctionName());
  return callee && !callee->ownerTypeCodegenName.empty() &&
         !callee->getArguments().empty() && callee->getArguments().front() &&
         callee->getArguments().front()->getRawName() == "self";
}

bool transfersThroughCast(const CastInst &cast) {
  return ownsManagedValue(cast.getSource()) && cast.getTargetType() &&
         cast.getResult() &&
         cast.getResult()->getOwnership() == ValueOwnership::Owned;
}

bool transfersThroughCallArgument(const Module &module, const CallInst &call,
                                  size_t argumentIndex) {
  if (argumentIndex >= call.getArguments().size() ||
      argumentIndex >= call.getArgumentModes().size() ||
      !ownsManagedValue(call.getArguments()[argumentIndex]) ||
      call.getArgumentModes()[argumentIndex] !=
          CallInst::ArgumentMode::Transfer ||
      (argumentIndex < call.getArgumentIsRef().size() &&
       call.getArgumentIsRef()[argumentIndex])) {
    return false;
  }
  const auto &type = call.getArguments()[argumentIndex]->getType();
  return type->getIntrinsicKind() == IntrinsicTypeKind::String ||
         (!call.isIndirect() && type->getKind() == TypeKind::Class &&
          !isBorrowedMethodSelf(module, call, argumentIndex));
}

} // namespace

OwnershipFlowAnalysis::OwnershipFlowAnalysis(
    const Module &module, const Function &function,
    const BlockEdges &predecessors, const BlockEdges &successors,
    const std::unordered_set<const BasicBlock *> &reachable)
    : module_(module), function_(function), predecessors_(predecessors),
      successors_(successors), reachable_(reachable) {}

OwnershipFlowState
OwnershipFlowAnalysis::stateOnEdge(const BasicBlock &source,
                                   const BasicBlock &destination,
                                   const std::shared_ptr<Value> &value) const {
  const auto sourceStates = edgeStates_.find(&source);
  if (!value || sourceStates == edgeStates_.end()) {
    return OwnershipFlowState::Unavailable;
  }
  const auto destinationStates = sourceStates->second.find(&destination);
  if (destinationStates == sourceStates->second.end()) {
    return OwnershipFlowState::Unavailable;
  }
  const auto state = destinationStates->second.find(value.get());
  return state == destinationStates->second.end()
             ? OwnershipFlowState::Unavailable
             : static_cast<OwnershipFlowState>(state->second);
}

std::vector<OwnershipTransferViolation> OwnershipFlowAnalysis::analyze() {
  const auto ownedValues = collectOwnedValues(function_);
  OwnershipStates entryStates;
  for (const auto *value : ownedValues) {
    entryStates[value] = unavailable;
  }
  for (const auto &argument : function_.getArguments()) {
    if (ownsManagedValue(argument)) {
      entryStates[argument.get()] = available;
    }
  }

  std::vector<OwnershipTransferViolation> violations;
  std::unordered_set<std::string> reported;
  auto consume = [&](OwnershipStates &states,
                     const std::shared_ptr<Value> &value,
                     const BasicBlock &block, size_t instructionIndex,
                     const char *operation) {
    if (!ownsManagedValue(value)) {
      return;
    }
    auto &state = states[value.get()];
    if (state != available) {
      const auto key = block.label + ":" + std::to_string(instructionIndex) +
                       ":" + value->getName();
      if (reported.insert(key).second) {
        violations.push_back(
            {&block, instructionIndex, value, std::string(operation)});
      }
    }
    state = consumed;
  };

  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto &blockOwner : function_.getBlocks()) {
      if (!blockOwner || reachable_.count(blockOwner.get()) == 0) {
        continue;
      }
      const auto &block = *blockOwner;
      OwnershipStates states;
      if (!function_.getBlocks().empty() &&
          function_.getBlocks().front().get() == &block) {
        states = entryStates;
      } else {
        const auto predecessors = predecessors_.find(&block);
        if (predecessors != predecessors_.end()) {
          for (const auto *predecessor : predecessors->second) {
            if (reachable_.count(predecessor) == 0) {
              continue;
            }
            const auto sourceStates = edgeStates_.find(predecessor);
            if (sourceStates == edgeStates_.end()) {
              continue;
            }
            const auto destinationStates = sourceStates->second.find(&block);
            if (destinationStates == sourceStates->second.end()) {
              continue;
            }
            for (const auto *value : ownedValues) {
              states[value] |= destinationStates->second.at(value);
            }
          }
        }
      }
      for (size_t i = 0; i < block.getInstructions().size(); ++i) {
        const auto &instruction = block.getInstructions()[i];
        if (!instruction) {
          continue;
        }
        switch (instruction->getOpCode()) {
        case OpCode::Store: {
          const auto &store = static_cast<const StoreInst &>(*instruction);
          if (store.getSourceOwnership() == ValueOwnership::Owned) {
            consume(states, store.getSource(), block, i, "store");
          }
          break;
        }
        case OpCode::Ret: {
          const auto &ret = static_cast<const ReturnInst &>(*instruction);
          if (ret.getValueOwnership() == ValueOwnership::Owned) {
            consume(states, ret.getValue(), block, i, "return");
          }
          break;
        }
        case OpCode::Cast: {
          const auto &cast = static_cast<const CastInst &>(*instruction);
          if (transfersThroughCast(cast)) {
            consume(states, cast.getSource(), block, i, "cast");
          }
          break;
        }
        case OpCode::Call: {
          const auto &call = static_cast<const CallInst &>(*instruction);
          for (size_t argumentIndex = 0;
               argumentIndex < call.getArguments().size(); ++argumentIndex) {
            if (transfersThroughCallArgument(module_, call, argumentIndex)) {
              consume(states, call.getArguments()[argumentIndex], block, i,
                      "call");
            }
          }
          break;
        }
        case OpCode::Release:
          consume(states,
                  static_cast<const ReleaseInst &>(*instruction).getValue(),
                  block, i, "release");
          break;
        default:
          break;
        }
        if (const auto result = instructionResult(*instruction);
            ownsManagedValue(result)) {
          states[result.get()] = available;
        }
      }
      const auto successors = successors_.find(&block);
      if (successors == successors_.end()) {
        continue;
      }
      for (const auto *successor : successors->second) {
        auto edgeState = states;
        for (const auto *value : ownedValues) {
          edgeState.try_emplace(value, unavailable);
        }
        for (size_t i = 0; i < successor->getInstructions().size(); ++i) {
          const auto &instruction = successor->getInstructions()[i];
          if (!instruction || instruction->getOpCode() != OpCode::Phi) {
            continue;
          }
          const auto &phi = static_cast<const PhiInst &>(*instruction);
          if (!ownsManagedValue(phi.getResult()) ||
              phi.getResult()->getOwnership() != ValueOwnership::Owned) {
            continue;
          }
          for (const auto &[label, value] : phi.getIncoming()) {
            if (label == block.label) {
              consume(edgeState, value, *successor, i, "phi");
              break;
            }
          }
        }
        auto &storedState = edgeStates_[&block][successor];
        if (storedState != edgeState) {
          storedState = std::move(edgeState);
          changed = true;
        }
      }
    }
  }
  return violations;
}

} // namespace zir

#include "ownership_lowering.hpp"

#include "ownership_liveness.hpp"

#include <unordered_set>
#include <vector>

namespace zir {
namespace {

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
  case OpCode::KeepAlive:
  case OpCode::Release:
  case OpCode::InlineAsm:
    return nullptr;
  }
  return nullptr;
}

bool ownsManagedValue(const std::shared_ptr<Value> &value) {
  return value && value->getOwnership() == ValueOwnership::Owned &&
         containsManagedValues(value->getType());
}

size_t releaseInsertionIndex(const BasicBlock &block, size_t resultIndex) {
  if (block.getInstructions()[resultIndex]->getOpCode() != OpCode::Phi) {
    return resultIndex + 1;
  }
  size_t index = resultIndex + 1;
  while (index < block.getInstructions().size() &&
         block.getInstructions()[index] &&
         block.getInstructions()[index]->getOpCode() == OpCode::Phi) {
    ++index;
  }
  return index;
}

bool transfersOwnership(const Instruction &instruction,
                        const std::shared_ptr<Value> &value) {
  switch (instruction.getOpCode()) {
  case OpCode::Store: {
    const auto &store = static_cast<const StoreInst &>(instruction);
    return store.getSource() == value &&
           store.getSourceOwnership() == ValueOwnership::Owned;
  }
  case OpCode::Ret: {
    const auto &ret = static_cast<const ReturnInst &>(instruction);
    return ret.getValue() == value &&
           ret.getValueOwnership() == ValueOwnership::Owned;
  }
  case OpCode::Cast: {
    const auto &cast = static_cast<const CastInst &>(instruction);
    return cast.getSource() == value && cast.getResult() &&
           cast.getResult()->getOwnership() == ValueOwnership::Owned;
  }
  case OpCode::Call: {
    const auto &call = static_cast<const CallInst &>(instruction);
    for (size_t i = 0; i < call.getArguments().size(); ++i) {
      if (call.getArguments()[i] == value &&
          i < call.getArgumentModes().size() &&
          call.getArgumentModes()[i] == CallInst::ArgumentMode::Transfer) {
        return true;
      }
    }
    return false;
  }
  case OpCode::Release:
    return static_cast<const ReleaseInst &>(instruction).getValue() == value;
  case OpCode::KeepAlive:
    return static_cast<const KeepAliveInst &>(instruction).getValue() == value;
  default:
    return false;
  }
}

bool wasOwnershipTransferredBefore(
    const std::vector<std::unique_ptr<Instruction>> &instructions,
    size_t instructionIndex, const std::shared_ptr<Value> &value) {
  for (size_t i = 0; i < instructionIndex; ++i) {
    if (instructions[i] && transfersOwnership(*instructions[i], value)) {
      return true;
    }
  }
  return false;
}

} // namespace

void lowerDeadOwnedResults(Module &module) {
  for (const auto &function : module.getFunctions()) {
    if (!function) {
      continue;
    }
    const auto liveness = analyzeOwnershipLiveness(*function);
    for (const auto &blockOwner : function->getBlocks()) {
      if (!blockOwner) {
        continue;
      }
      auto &instructions = blockOwner->instructions;
      std::vector<std::pair<size_t, std::shared_ptr<Value>>> releases;
      std::vector<std::shared_ptr<Value>> ownedResults;
      std::unordered_set<const Value *> seenResults;
      for (size_t i = 0; i < instructions.size(); ++i) {
        if (!instructions[i]) {
          continue;
        }
        const auto result = instructionResult(*instructions[i]);
        if (!ownsManagedValue(result)) {
          continue;
        }
        if (seenResults.insert(result.get()).second) {
          ownedResults.push_back(result);
        }
        if (!liveness.isLiveAfter(*blockOwner, i, result)) {
          releases.emplace_back(releaseInsertionIndex(*blockOwner, i), result);
        }
      }
      for (size_t i = 0; i < instructions.size(); ++i) {
        if (!instructions[i]) {
          continue;
        }
        for (const auto &value : ownedResults) {
          if (liveness.isLastUse(*blockOwner, i, value) &&
              !transfersOwnership(*instructions[i], value) &&
              !wasOwnershipTransferredBefore(instructions, i, value)) {
            releases.emplace_back(releaseInsertionIndex(*blockOwner, i), value);
          }
        }
      }
      for (auto release = releases.rbegin(); release != releases.rend();
           ++release) {
        instructions.insert(instructions.begin() +
                                static_cast<std::ptrdiff_t>(release->first),
                            std::make_unique<ReleaseInst>(release->second));
      }
    }

    std::unordered_set<std::string> labels;
    for (const auto &blockOwner : function->getBlocks()) {
      if (blockOwner) {
        labels.insert(blockOwner->label);
      }
    }
    size_t edgeIndex = 0;
    std::vector<std::unique_ptr<BasicBlock>> edgeBlocks;
    for (const auto &sourceOwner : function->getBlocks()) {
      if (!sourceOwner || sourceOwner->getInstructions().empty()) {
        continue;
      }
      auto &source = *sourceOwner;
      const auto &terminator = source.getInstructions().back();
      if (!terminator || (terminator->getOpCode() != OpCode::Br &&
                          terminator->getOpCode() != OpCode::CondBr)) {
        continue;
      }
      std::vector<std::string> targets;
      if (terminator->getOpCode() == OpCode::Br) {
        targets.push_back(static_cast<const BranchInst &>(*terminator).getTarget());
      } else {
        const auto &branch = static_cast<const CondBranchInst &>(*terminator);
        targets.push_back(branch.getTrueLabel());
        if (branch.getFalseLabel() != branch.getTrueLabel()) {
          targets.push_back(branch.getFalseLabel());
        }
      }
      for (const auto &targetLabel : targets) {
        auto *destination = function->findBlock(targetLabel);
        if (!destination) {
          continue;
        }
        std::vector<std::shared_ptr<Value>> releases;
        for (const auto &instruction : source.getInstructions()) {
          if (!instruction) {
            continue;
          }
          const auto value = instructionResult(*instruction);
          if (!ownsManagedValue(value) ||
              liveness.isLiveOnEdge(source, *destination, value)) {
            continue;
          }
          bool transferred = false;
          for (const auto &use : source.getInstructions()) {
            if (use && transfersOwnership(*use, value)) {
              transferred = true;
              break;
            }
          }
          if (!transferred) {
            releases.push_back(value);
          }
        }
        if (releases.empty()) {
          continue;
        }
        std::string edgeLabel;
        do {
          edgeLabel = "ownership.release." + std::to_string(edgeIndex++);
        } while (!labels.insert(edgeLabel).second);
        auto edge = std::make_unique<BasicBlock>(edgeLabel);
        for (const auto &value : releases) {
          edge->addInstruction(std::make_unique<ReleaseInst>(value));
        }
        edge->addInstruction(std::make_unique<BranchInst>(targetLabel));
        if (terminator->getOpCode() == OpCode::Br) {
          static_cast<BranchInst &>(*terminator).setTarget(edgeLabel);
        } else {
          static_cast<CondBranchInst &>(*terminator).replaceTarget(targetLabel,
                                                                     edgeLabel);
        }
        for (const auto &instruction : destination->getInstructions()) {
          if (instruction && instruction->getOpCode() == OpCode::Phi) {
            static_cast<PhiInst &>(*instruction).replaceIncomingLabel(
                source.label, edgeLabel);
          }
        }
        edgeBlocks.push_back(std::move(edge));
      }
    }
    for (auto &edge : edgeBlocks) {
      function->addBlock(std::move(edge));
    }
  }
}

} // namespace zir

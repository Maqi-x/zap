#include "ownership_lowering.hpp"

#include "ownership_liveness.hpp"

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
      for (size_t i = 0; i < instructions.size(); ++i) {
        if (!instructions[i]) {
          continue;
        }
        const auto result = instructionResult(*instructions[i]);
        if (!ownsManagedValue(result) ||
            liveness.isLiveAfter(*blockOwner, i, result)) {
          continue;
        }
        releases.emplace_back(releaseInsertionIndex(*blockOwner, i), result);
      }
      for (auto release = releases.rbegin(); release != releases.rend();
           ++release) {
        instructions.insert(instructions.begin() +
                                static_cast<std::ptrdiff_t>(release->first),
                            std::make_unique<ReleaseInst>(release->second));
      }
    }
  }
}

} // namespace zir

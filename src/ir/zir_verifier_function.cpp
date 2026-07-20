#include "zir_verifier_internal.hpp"

#include <algorithm>
#include <deque>
#include <unordered_map>
#include <unordered_set>

namespace zir {
namespace {

using verifier_detail::instructionResult;
using verifier_detail::isAssignable;
using verifier_detail::isStringType;
using verifier_detail::isTerminator;
using verifier_detail::typeName;

class FunctionVerifier {
public:
  FunctionVerifier(const Module &module, const Function &function,
                   std::vector<VerificationError> &errors,
                   TypeInterner &typeInterner)
      : module_(module), function_(function), errors_(errors),
        typeInterner_(typeInterner) {}

  void verify() {
    verifySignature();
    if (function_.getBlocks().empty()) {
      error(VerificationErrorCode::MissingBody, nullptr, std::nullopt,
            "defined function has no basic blocks");
      return;
    }

    collectBlocks();
    collectDefinitionsAndEdges();
    computeDominators();
    verifyInstructions();
  }

private:
  struct DefinitionSite {
    const BasicBlock *block;
    size_t instructionIndex;
  };

  const Module &module_;
  const Function &function_;
  std::vector<VerificationError> &errors_;
  TypeInterner &typeInterner_;
  std::unordered_map<std::string, const BasicBlock *> blocks_;
  std::unordered_map<const BasicBlock *, std::vector<const BasicBlock *>>
      predecessors_;
  std::unordered_map<const BasicBlock *, std::vector<const BasicBlock *>>
      successors_;
  std::unordered_map<const Value *, DefinitionSite> definitions_;
  std::unordered_map<std::string, const Value *> valueNames_;
  std::unordered_set<const BasicBlock *> reachable_;
  std::unordered_map<const BasicBlock *, std::unordered_set<const BasicBlock *>>
      dominators_;

  void error(VerificationErrorCode code, const BasicBlock *block,
             std::optional<size_t> instructionIndex, std::string message) {
    errors_.push_back({code, function_.name,
                       block ? block->label : std::string{}, instructionIndex,
                       std::move(message)});
  }

  void verifySignature() {
    if (!function_.getReturnType()) {
      error(VerificationErrorCode::NullNode, nullptr, std::nullopt,
            "function has no return type");
    }
    for (const auto &argument : function_.getArguments()) {
      if (!argument || !argument->getType()) {
        error(VerificationErrorCode::NullNode, nullptr, std::nullopt,
              "function has a null argument or argument type");
        continue;
      }
      if (!valueNames_.emplace(argument->getName(), argument.get()).second) {
        error(VerificationErrorCode::DuplicateValue, nullptr, std::nullopt,
              "duplicate argument name " + argument->getName());
      }
    }
  }

  void collectBlocks() {
    for (const auto &block : function_.getBlocks()) {
      if (!block) {
        error(VerificationErrorCode::NullNode, nullptr, std::nullopt,
              "function contains a null basic block");
        continue;
      }
      if (!blocks_.emplace(block->label, block.get()).second) {
        error(VerificationErrorCode::DuplicateBlock, block.get(), std::nullopt,
              "duplicate basic block label " + block->label);
      }
      predecessors_[block.get()];
      successors_[block.get()];
    }
  }

  void addEdge(const BasicBlock &source, const std::string &targetLabel,
               size_t instructionIndex) {
    auto target = blocks_.find(targetLabel);
    if (target == blocks_.end()) {
      error(VerificationErrorCode::InvalidBranchTarget, &source,
            instructionIndex, "branch targets unknown block " + targetLabel);
      return;
    }
    successors_[&source].push_back(target->second);
    predecessors_[target->second].push_back(&source);
  }

  void collectDefinitionsAndEdges() {
    for (const auto &blockOwner : function_.getBlocks()) {
      if (!blockOwner) {
        continue;
      }
      const auto &block = *blockOwner;
      bool sawTerminator = false;
      for (size_t i = 0; i < block.getInstructions().size(); ++i) {
        const auto &instruction = block.getInstructions()[i];
        if (!instruction) {
          error(VerificationErrorCode::NullNode, &block, i,
                "basic block contains a null instruction");
          continue;
        }
        if (sawTerminator) {
          error(VerificationErrorCode::InstructionAfterTerminator, &block, i,
                "instruction appears after a terminator");
        }
        sawTerminator = sawTerminator || isTerminator(instruction->getOpCode());

        if (auto result = instructionResult(*instruction)) {
          if (!result->getType()) {
            error(VerificationErrorCode::NullNode, &block, i,
                  "instruction result has no type");
          }
          if (!definitions_.emplace(result.get(), DefinitionSite{&block, i})
                   .second) {
            error(VerificationErrorCode::DuplicateValue, &block, i,
                  "value is defined more than once: " + result->getName());
          }
          auto [_, inserted] = valueNames_.emplace(result->getName(), result.get());
          if (!inserted) {
            error(VerificationErrorCode::DuplicateValue, &block, i,
                  "duplicate value name " + result->getName());
          }
        }

        if (instruction->getOpCode() == OpCode::Br) {
          addEdge(block, static_cast<const BranchInst &>(*instruction).getTarget(),
                  i);
        } else if (instruction->getOpCode() == OpCode::CondBr) {
          const auto &branch = static_cast<const CondBranchInst &>(*instruction);
          addEdge(block, branch.getTrueLabel(), i);
          addEdge(block, branch.getFalseLabel(), i);
        }
      }
      if (!sawTerminator) {
        error(VerificationErrorCode::MissingTerminator, &block, std::nullopt,
              "basic block has no terminator");
      }
    }
  }

  void computeDominators() {
    if (function_.getBlocks().empty() || !function_.getBlocks().front()) {
      return;
    }
    const auto *entry = function_.getBlocks().front().get();
    std::deque<const BasicBlock *> worklist{entry};
    reachable_.insert(entry);
    while (!worklist.empty()) {
      const auto *block = worklist.front();
      worklist.pop_front();
      for (const auto *successor : successors_[block]) {
        if (reachable_.insert(successor).second) {
          worklist.push_back(successor);
        }
      }
    }

    for (const auto *block : reachable_) {
      dominators_[block] = reachable_;
    }
    dominators_[entry] = {entry};

    bool changed = true;
    while (changed) {
      changed = false;
      for (const auto *block : reachable_) {
        if (block == entry) {
          continue;
        }
        std::unordered_set<const BasicBlock *> next;
        bool firstPredecessor = true;
        for (const auto *predecessor : predecessors_[block]) {
          if (reachable_.count(predecessor) == 0) {
            continue;
          }
          if (firstPredecessor) {
            next = dominators_[predecessor];
            firstPredecessor = false;
          } else {
            for (auto it = next.begin(); it != next.end();) {
              if (dominators_[predecessor].count(*it) == 0) {
                it = next.erase(it);
              } else {
                ++it;
              }
            }
          }
        }
        next.insert(block);
        if (next != dominators_[block]) {
          dominators_[block] = std::move(next);
          changed = true;
        }
      }
    }
  }

  bool verifyValue(const std::shared_ptr<Value> &value,
                   const BasicBlock &useBlock, size_t useIndex,
                   const BasicBlock *edgePredecessor = nullptr) {
    if (!value || !value->getType()) {
      error(VerificationErrorCode::NullNode, &useBlock, useIndex,
            "instruction contains a null value or value type");
      return false;
    }
    if (value->getKind() != ValueKind::Register) {
      return true;
    }

    auto definition = definitions_.find(value.get());
    if (definition == definitions_.end()) {
      error(VerificationErrorCode::UndefinedValue, &useBlock, useIndex,
            "use of undefined value " + value->getName());
      return false;
    }

    const auto *effectiveBlock = edgePredecessor ? edgePredecessor : &useBlock;
    if (definition->second.block == effectiveBlock) {
      const size_t effectiveIndex =
          edgePredecessor ? effectiveBlock->getInstructions().size() : useIndex;
      if (definition->second.instructionIndex >= effectiveIndex) {
        error(VerificationErrorCode::UseBeforeDefinition, &useBlock, useIndex,
              "value is used before its definition: " + value->getName());
        return false;
      }
      return true;
    }

    if (reachable_.count(effectiveBlock) > 0 &&
        (reachable_.count(definition->second.block) == 0 ||
         dominators_[effectiveBlock].count(definition->second.block) == 0)) {
      error(VerificationErrorCode::DominanceViolation, &useBlock, useIndex,
            "definition of " + value->getName() +
                " does not dominate its use");
      return false;
    }
    return true;
  }

  void expectSameType(const std::shared_ptr<Type> &actual,
                      const std::shared_ptr<Type> &expected,
                      const BasicBlock &block, size_t index,
                      const std::string &context) {
    if (!typeInterner_.same(actual, expected)) {
      error(VerificationErrorCode::TypeMismatch, &block, index,
            context + ": expected " + typeName(expected) + ", got " +
                typeName(actual));
    }
  }

  void expectAssignable(const std::shared_ptr<Type> &actual,
                        const std::shared_ptr<Type> &expected,
                        const BasicBlock &block, size_t index,
                        const std::string &context) {
    if (!isAssignable(actual, expected, typeInterner_)) {
      error(VerificationErrorCode::TypeMismatch, &block, index,
            context + ": expected " + typeName(expected) + ", got " +
                typeName(actual));
    }
  }

  void verifyInstructions() {
    for (const auto &blockOwner : function_.getBlocks()) {
      if (!blockOwner) {
        continue;
      }
      const auto &block = *blockOwner;
      for (size_t i = 0; i < block.getInstructions().size(); ++i) {
        const auto &instruction = block.getInstructions()[i];
        if (instruction) {
          verifyInstruction(*instruction, block, i);
        }
      }
    }
  }

  void verifyInstruction(const Instruction &instruction, const BasicBlock &block,
                         size_t index) {
    switch (instruction.getOpCode()) {
    case OpCode::Alloca: {
      const auto &alloca = static_cast<const AllocaInst &>(instruction);
      if (!alloca.getResult() || !alloca.getAllocatedType()) {
        error(VerificationErrorCode::InvalidResult, &block, index,
              "alloca requires a result and allocated type");
        return;
      }
      auto pointer = std::dynamic_pointer_cast<PointerType>(
          alloca.getResult()->getType());
      if (!pointer || !typeInterner_.same(pointer->getBaseType(),
                                          alloca.getAllocatedType())) {
        error(VerificationErrorCode::TypeMismatch, &block, index,
              "alloca result must point to the allocated type");
      }
      return;
    }
    case OpCode::Load: {
      const auto &load = static_cast<const LoadInst &>(instruction);
      verifyValue(load.getSource(), block, index);
      auto pointer = load.getSource()
                         ? std::dynamic_pointer_cast<PointerType>(
                               load.getSource()->getType())
                         : nullptr;
      if (!pointer || !load.getResult()) {
        error(VerificationErrorCode::InvalidOperand, &block, index,
              "load requires a pointer source and result");
      } else {
        expectSameType(load.getResult()->getType(), pointer->getBaseType(), block,
                       index, "load result type");
      }
      return;
    }
    case OpCode::Store: {
      const auto &store = static_cast<const StoreInst &>(instruction);
      verifyValue(store.getSource(), block, index);
      verifyValue(store.getDestination(), block, index);
      auto pointer = store.getDestination()
                         ? std::dynamic_pointer_cast<PointerType>(
                               store.getDestination()->getType())
                         : nullptr;
      if (!pointer || !store.getSource()) {
        error(VerificationErrorCode::InvalidOperand, &block, index,
              "store requires a value and pointer destination");
      } else {
        expectAssignable(store.getSource()->getType(), pointer->getBaseType(),
                         block, index, "stored value type");
        if (store.getSourceOwnership() != store.getSource()->getOwnership()) {
          error(VerificationErrorCode::InvalidOperand, &block, index,
                "store ownership does not match its source value");
        }
      }
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
      verifyValue(binary.getLhs(), block, index);
      verifyValue(binary.getRhs(), block, index);
      if (!binary.getResult() || !binary.getLhs() || !binary.getRhs()) {
        error(VerificationErrorCode::InvalidOperand, &block, index,
              "binary instruction requires two operands and a result");
        return;
      }

      const auto lhsType = binary.getLhs()->getType();
      const auto rhsType = binary.getRhs()->getType();
      const bool isAdd = instruction.getOpCode() == OpCode::Add;
      const bool isSubtract = instruction.getOpCode() == OpCode::Sub;
      const bool isStringConcat =
          isAdd &&
          (isStringType(lhsType) || isStringType(rhsType) ||
           lhsType->getKind() == TypeKind::Char ||
           rhsType->getKind() == TypeKind::Char);
      const bool lhsPointer = lhsType->getKind() == TypeKind::Pointer;
      const bool rhsPointer = rhsType->getKind() == TypeKind::Pointer;

      if (isStringConcat) {
        if ((!isStringType(lhsType) && lhsType->getKind() != TypeKind::Char) ||
            (!isStringType(rhsType) && rhsType->getKind() != TypeKind::Char) ||
            binary.getResult()->getType()->getIntrinsicKind() !=
                IntrinsicTypeKind::String) {
          error(VerificationErrorCode::TypeMismatch, &block, index,
                "string concatenation requires String/StringView/Char "
                "operands and a String result");
        }
      } else if (isAdd && lhsPointer != rhsPointer) {
        const auto offsetType = lhsPointer ? rhsType : lhsType;
        const auto pointerType = lhsPointer ? lhsType : rhsType;
        if (!offsetType->isInteger()) {
          error(VerificationErrorCode::TypeMismatch, &block, index,
                "pointer offset must be an integer");
        }
        expectSameType(binary.getResult()->getType(), pointerType, block, index,
                       "pointer addition result type");
      } else if (isSubtract && lhsPointer && rhsPointer) {
        expectSameType(lhsType, rhsType, block, index,
                       "pointer subtraction operand type");
        if (!binary.getResult()->getType()->isInteger()) {
          error(VerificationErrorCode::TypeMismatch, &block, index,
                "pointer subtraction result must be an integer");
        }
      } else if (isSubtract && lhsPointer && !rhsPointer) {
        if (!rhsType->isInteger()) {
          error(VerificationErrorCode::TypeMismatch, &block, index,
                "pointer offset must be an integer");
        }
        expectSameType(binary.getResult()->getType(), lhsType, block, index,
                       "pointer subtraction result type");
      } else {
        expectSameType(rhsType, lhsType, block, index,
                       "binary operand type");
        expectSameType(binary.getResult()->getType(), lhsType, block, index,
                       "binary result type");
      }
      return;
    }
    case OpCode::Cmp: {
      const auto &compare = static_cast<const CmpInst &>(instruction);
      verifyValue(compare.getLhs(), block, index);
      verifyValue(compare.getRhs(), block, index);
      if (compare.getLhs() && compare.getRhs()) {
        expectSameType(compare.getRhs()->getType(), compare.getLhs()->getType(),
                       block, index, "comparison operand type");
      }
      if (!compare.getResult() ||
          compare.getResult()->getType()->getKind() != TypeKind::Bool) {
        error(VerificationErrorCode::InvalidResult, &block, index,
              "comparison result must be Bool");
      }
      return;
    }
    case OpCode::Br:
      return;
    case OpCode::CondBr: {
      const auto &branch = static_cast<const CondBranchInst &>(instruction);
      verifyValue(branch.getCondition(), block, index);
      if (!branch.getCondition() ||
          branch.getCondition()->getType()->getKind() != TypeKind::Bool) {
        error(VerificationErrorCode::InvalidConditionType, &block, index,
              "conditional branch requires a Bool condition");
      }
      return;
    }
    case OpCode::Ret: {
      const auto &returnInstruction =
          static_cast<const ReturnInst &>(instruction);
      if (returnInstruction.getValue()) {
        verifyValue(returnInstruction.getValue(), block, index);
      }
      const bool returnsVoid = function_.getReturnType() &&
                               function_.getReturnType()->getKind() ==
                                   TypeKind::Void;
      if (returnsVoid && returnInstruction.getValue()) {
        error(VerificationErrorCode::InvalidReturn, &block, index,
              "void function returns a value");
      } else if (!returnsVoid && !returnInstruction.getValue()) {
        error(VerificationErrorCode::InvalidReturn, &block, index,
              "non-void function returns no value");
      } else if (returnInstruction.getValue()) {
        auto expectedReturnType = function_.getReturnType();
        if (function_.returnsRef) {
          expectedReturnType =
              std::make_shared<PointerType>(expectedReturnType);
        }
        expectAssignable(returnInstruction.getValue()->getType(),
                         expectedReturnType, block, index,
                         "return value type");
        if (returnInstruction.getValueOwnership() !=
            returnInstruction.getValue()->getOwnership()) {
          error(VerificationErrorCode::InvalidReturn, &block, index,
                "return ownership does not match its value");
        }
        if (function_.returnsRef &&
            returnInstruction.getValueOwnership() == ValueOwnership::Owned) {
          error(VerificationErrorCode::InvalidReturn, &block, index,
                "ref return cannot transfer ownership");
        }
      }
      return;
    }
    case OpCode::Call:
      verifyCall(static_cast<const CallInst &>(instruction), block, index);
      return;
    case OpCode::Retain:
      verifyValue(static_cast<const RetainInst &>(instruction).getValue(), block,
                  index);
      return;
    case OpCode::Release:
      verifyValue(static_cast<const ReleaseInst &>(instruction).getValue(),
                  block, index);
      return;
    case OpCode::Alloc: {
      const auto &alloc = static_cast<const AllocInst &>(instruction);
      if (!alloc.getResult() || !alloc.getAllocatedType()) {
        error(VerificationErrorCode::InvalidResult, &block, index,
              "alloc requires a result and allocated type");
      } else {
        expectSameType(alloc.getResult()->getType(), alloc.getAllocatedType(),
                       block, index, "alloc result type");
        if (alloc.getResult()->getOwnership() != ValueOwnership::Owned) {
          error(VerificationErrorCode::InvalidResult, &block, index,
                "alloc result must be owned");
        }
      }
      return;
    }
    case OpCode::GetElementPtr:
      verifyGetElementPtr(static_cast<const GetElementPtrInst &>(instruction),
                          block, index);
      return;
    case OpCode::Phi:
      verifyPhi(static_cast<const PhiInst &>(instruction), block, index);
      return;
    case OpCode::Cast: {
      const auto &cast = static_cast<const CastInst &>(instruction);
      verifyValue(cast.getSource(), block, index);
      if (!cast.getResult() || !cast.getTargetType() || !cast.getSource()) {
        error(VerificationErrorCode::InvalidResult, &block, index,
              "cast requires a source, result, and target type");
      } else {
        expectSameType(cast.getResult()->getType(), cast.getTargetType(), block,
                       index, "cast result type");
        const auto expectedOwnership =
            cast.getTargetType()->getIntrinsicKind() ==
                    IntrinsicTypeKind::String
                ? ValueOwnership::Owned
            : containsManagedValues(cast.getTargetType()) &&
                    cast.getSource()->getOwnership() == ValueOwnership::Owned
                ? ValueOwnership::Owned
                : ValueOwnership::Borrowed;
        if (cast.getResult()->getOwnership() != expectedOwnership) {
          error(VerificationErrorCode::InvalidResult, &block, index,
                "cast result ownership does not match source and target");
        }
      }
      return;
    }
    case OpCode::WeakLock: {
      const auto &weakLock = static_cast<const WeakLockInst &>(instruction);
      verifyValue(weakLock.getWeakValue(), block, index);
      if (!weakLock.getResult() || !weakLock.getWeakValue() ||
          weakLock.getResult()->getType()->getKind() != TypeKind::Class ||
          weakLock.getWeakValue()->getType()->getKind() != TypeKind::Class) {
        error(VerificationErrorCode::InvalidOperand, &block, index,
              "weak.lock requires class operands");
      } else {
        const auto weakType = std::static_pointer_cast<ClassType>(
            weakLock.getWeakValue()->getType());
        const auto resultType = std::static_pointer_cast<ClassType>(
            weakLock.getResult()->getType());
        if (!weakType->isWeak() || resultType->isWeak()) {
          error(VerificationErrorCode::InvalidOperand, &block, index,
                "weak.lock requires a weak source and strong result");
        } else {
          auto expectedResultType = std::make_shared<ClassType>(*weakType);
          expectedResultType->setWeak(false);
          expectSameType(weakLock.getResult()->getType(), expectedResultType,
                         block, index, "weak.lock result type");
        }
        if (weakLock.getResult()->getOwnership() != ValueOwnership::Owned) {
          error(VerificationErrorCode::InvalidResult, &block, index,
                "weak.lock result must be owned");
        }
      }
      return;
    }
    case OpCode::WeakAlive: {
      const auto &weakAlive = static_cast<const WeakAliveInst &>(instruction);
      verifyValue(weakAlive.getWeakValue(), block, index);
      if (!weakAlive.getResult() ||
          weakAlive.getResult()->getType()->getKind() != TypeKind::Bool) {
        error(VerificationErrorCode::InvalidResult, &block, index,
              "weak.alive result must be Bool");
      }
      return;
    }
    case OpCode::InlineAsm: {
      const auto &assembly = static_cast<const InlineAsmInst &>(instruction);
      for (const auto &operand : assembly.getOutputs()) {
        verifyValue(operand.value, block, index);
      }
      for (const auto &operand : assembly.getInputs()) {
        verifyValue(operand.value, block, index);
      }
      return;
    }
    }
  }

  void verifyCall(const CallInst &call, const BasicBlock &block, size_t index) {
    for (const auto &argument : call.getArguments()) {
      verifyValue(argument, block, index);
    }
    if (call.getVariadicPack()) {
      verifyValue(call.getVariadicPack(), block, index);
    }

    std::vector<std::shared_ptr<Type>> parameterTypes;
    std::shared_ptr<Type> returnType;
    bool variadic = false;
    if (call.isIndirect()) {
      verifyValue(call.getCalleeValue(), block, index);
      auto functionType = call.getCalleeValue()
                              ? std::dynamic_pointer_cast<FunctionPointerType>(
                                    call.getCalleeValue()->getType())
                              : nullptr;
      if (!functionType) {
        error(VerificationErrorCode::InvalidCall, &block, index,
              "indirect call requires a function pointer");
        return;
      }
      parameterTypes = functionType->getParams();
      returnType = functionType->getReturnType();
    } else {
      const auto *callee = module_.findFunction(call.getFunctionName());
      if (!callee) {
        error(VerificationErrorCode::InvalidCall, &block, index,
              "call targets unknown function " + call.getFunctionName());
        return;
      }
      for (const auto &argument : callee->getArguments()) {
        if (argument && !argument->isVariadicPack()) {
          parameterTypes.push_back(argument->getType());
        }
      }
      returnType = callee->getReturnType();
      variadic = callee->isCVariadic ||
                 (!callee->getArguments().empty() &&
                  callee->getArguments().back()->isVariadicPack());
    }

    if ((!variadic && call.getArguments().size() != parameterTypes.size()) ||
        (variadic && call.getArguments().size() < parameterTypes.size())) {
      error(VerificationErrorCode::InvalidCall, &block, index,
            "call argument count does not match the callee signature");
    }
    const size_t checkedArguments =
        std::min(call.getArguments().size(), parameterTypes.size());
    for (size_t i = 0; i < checkedArguments; ++i) {
      if (call.getArguments()[i]) {
        expectAssignable(call.getArguments()[i]->getType(), parameterTypes[i],
                         block, index,
                         "call argument " + std::to_string(i) + " type");
      }
    }
    if (call.getResult() && returnType) {
      auto expectedReturnType = returnType;
      if (call.returnsRef()) {
        expectedReturnType = std::make_shared<PointerType>(returnType);
      }
      expectSameType(call.getResult()->getType(), expectedReturnType, block,
                     index, "call result type");
      const auto expectedOwnership =
          call.getResultOwnership() == CallInst::ResultOwnership::Owned
              ? ValueOwnership::Owned
              : ValueOwnership::Borrowed;
      if (call.getResult()->getOwnership() != expectedOwnership) {
        error(VerificationErrorCode::InvalidResult, &block, index,
              "call result ownership does not match call metadata");
      }
    }
    if (!call.getArgumentIsRef().empty() &&
        call.getArgumentIsRef().size() != call.getArguments().size()) {
      error(VerificationErrorCode::InvalidCall, &block, index,
            "call ref-argument metadata has the wrong size");
    }
  }

  void verifyGetElementPtr(const GetElementPtrInst &gep,
                           const BasicBlock &block, size_t index) {
    verifyValue(gep.getPointer(), block, index);
    if (gep.getIndexValue()) {
      verifyValue(gep.getIndexValue(), block, index);
      if (!gep.getIndexValue()->getType()->isInteger()) {
        error(VerificationErrorCode::InvalidOperand, &block, index,
              "dynamic getelementptr index must be an integer");
      }
    }
    if (!gep.getResult() || !gep.getPointer()) {
      error(VerificationErrorCode::InvalidResult, &block, index,
            "getelementptr requires pointer and result operands");
      return;
    }
    if (gep.getResult()->getType()->getKind() != TypeKind::Pointer) {
      error(VerificationErrorCode::TypeMismatch, &block, index,
            "getelementptr result must be a pointer");
    }
  }

  void verifyPhi(const PhiInst &phi, const BasicBlock &block, size_t index) {
    if (!phi.getResult()) {
      error(VerificationErrorCode::InvalidResult, &block, index,
            "phi requires a result");
      return;
    }
    std::unordered_set<const BasicBlock *> incomingBlocks;
    for (const auto &[label, value] : phi.getIncoming()) {
      auto incomingBlock = blocks_.find(label);
      if (incomingBlock == blocks_.end() ||
          std::find(predecessors_[&block].begin(), predecessors_[&block].end(),
                    incomingBlock->second) == predecessors_[&block].end()) {
        error(VerificationErrorCode::InvalidPhi, &block, index,
              "phi incoming label is not a predecessor: " + label);
        continue;
      }
      if (!incomingBlocks.insert(incomingBlock->second).second) {
        error(VerificationErrorCode::InvalidPhi, &block, index,
              "phi contains duplicate incoming block " + label);
      }
      if (value) {
        verifyValue(value, block, index, incomingBlock->second);
        expectSameType(value->getType(), phi.getResult()->getType(), block,
                       index, "phi incoming value type");
      }
    }
    std::unordered_set<const BasicBlock *> predecessors(
        predecessors_[&block].begin(), predecessors_[&block].end());
    if (incomingBlocks != predecessors) {
      error(VerificationErrorCode::InvalidPhi, &block, index,
            "phi must have one incoming value for every predecessor");
    }

    ValueOwnership expectedOwnership = ValueOwnership::Borrowed;
    if (containsManagedValues(phi.getResult()->getType()) &&
        !phi.getIncoming().empty()) {
      const bool allIncomingOwned = std::all_of(
          phi.getIncoming().begin(), phi.getIncoming().end(),
          [](const auto &incoming) {
            return incoming.second &&
                   incoming.second->getOwnership() == ValueOwnership::Owned;
          });
      if (allIncomingOwned) {
        expectedOwnership = ValueOwnership::Owned;
      }
    }
    if (phi.getResult()->getOwnership() != expectedOwnership) {
      error(VerificationErrorCode::InvalidResult, &block, index,
            "phi result ownership does not match incoming values");
    }
  }
};

} // namespace

void verifier_detail::verifyDefinedFunction(
    const Module &module, const Function &function,
    std::vector<VerificationError> &errors, TypeInterner &typeInterner) {
  FunctionVerifier(module, function, errors, typeInterner).verify();
}

} // namespace zir

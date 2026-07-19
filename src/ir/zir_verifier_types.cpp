#include "zir_verifier_internal.hpp"

namespace zir::verifier_detail {

bool sameType(const std::shared_ptr<Type> &lhs,
              const std::shared_ptr<Type> &rhs) {
  if (lhs == rhs) {
    return true;
  }
  if (!lhs || !rhs || lhs->getKind() != rhs->getKind() ||
      lhs->getIntrinsicKind() != rhs->getIntrinsicKind()) {
    return false;
  }
  if (lhs->getIntrinsicKind() != IntrinsicTypeKind::None) {
    return true;
  }

  switch (lhs->getKind()) {
  case TypeKind::Pointer: {
    const auto &lhsPointer = static_cast<const PointerType &>(*lhs);
    const auto &rhsPointer = static_cast<const PointerType &>(*rhs);
    return sameType(lhsPointer.getBaseType(), rhsPointer.getBaseType());
  }
  case TypeKind::Array: {
    const auto &lhsArray = static_cast<const ArrayType &>(*lhs);
    const auto &rhsArray = static_cast<const ArrayType &>(*rhs);
    return lhsArray.getSize() == rhsArray.getSize() &&
           sameType(lhsArray.getBaseType(), rhsArray.getBaseType());
  }
  case TypeKind::Record: {
    const auto &lhsRecord = static_cast<const RecordType &>(*lhs);
    const auto &rhsRecord = static_cast<const RecordType &>(*rhs);
    return lhsRecord.getCodegenName() == rhsRecord.getCodegenName();
  }
  case TypeKind::Class: {
    const auto &lhsClass = static_cast<const ClassType &>(*lhs);
    const auto &rhsClass = static_cast<const ClassType &>(*rhs);
    return lhsClass.getCodegenName() == rhsClass.getCodegenName() &&
           lhsClass.isWeak() == rhsClass.isWeak();
  }
  case TypeKind::Enum: {
    const auto &lhsEnum = static_cast<const EnumType &>(*lhs);
    const auto &rhsEnum = static_cast<const EnumType &>(*rhs);
    return lhsEnum.getCodegenName() == rhsEnum.getCodegenName();
  }
  case TypeKind::TaggedUnion: {
    const auto &lhsUnion = static_cast<const TaggedUnionType &>(*lhs);
    const auto &rhsUnion = static_cast<const TaggedUnionType &>(*rhs);
    return lhsUnion.getCodegenName() == rhsUnion.getCodegenName();
  }
  case TypeKind::FunctionPointer: {
    const auto &lhsFunction = static_cast<const FunctionPointerType &>(*lhs);
    const auto &rhsFunction = static_cast<const FunctionPointerType &>(*rhs);
    if (!sameType(lhsFunction.getReturnType(), rhsFunction.getReturnType()) ||
        lhsFunction.getParams().size() != rhsFunction.getParams().size()) {
      return false;
    }
    for (size_t i = 0; i < lhsFunction.getParams().size(); ++i) {
      if (!sameType(lhsFunction.getParams()[i], rhsFunction.getParams()[i])) {
        return false;
      }
    }
    return true;
  }
  default:
    return true;
  }
}

bool isAssignable(const std::shared_ptr<Type> &actual,
                  const std::shared_ptr<Type> &expected) {
  if (sameType(actual, expected)) {
    return true;
  }
  if (!actual || !expected) {
    return false;
  }
  if (actual->getKind() == TypeKind::NullPtr) {
    return expected->getKind() == TypeKind::Pointer ||
           expected->getKind() == TypeKind::FunctionPointer ||
           expected->getKind() == TypeKind::Class;
  }
  if (actual->getKind() != TypeKind::Class ||
      expected->getKind() != TypeKind::Class) {
    return false;
  }

  const auto actualClass = std::static_pointer_cast<ClassType>(actual);
  const auto expectedClass = std::static_pointer_cast<ClassType>(expected);
  if (actualClass->isWeak() && !expectedClass->isWeak()) {
    return false;
  }
  for (auto current = actualClass; current; current = current->getBase()) {
    if (current->getCodegenName() == expectedClass->getCodegenName()) {
      return true;
    }
  }
  return false;
}

bool isTerminator(OpCode opcode) {
  return opcode == OpCode::Br || opcode == OpCode::CondBr ||
         opcode == OpCode::Ret;
}

bool isStringType(const std::shared_ptr<Type> &type) {
  return type && type->getIntrinsicKind() != IntrinsicTypeKind::None;
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
  default:
    return nullptr;
  }
}

std::string typeName(const std::shared_ptr<Type> &type) {
  return type ? type->toString() : "<null type>";
}

} // namespace zir::verifier_detail

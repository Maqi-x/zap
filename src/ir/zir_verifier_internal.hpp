#pragma once

#include "zir_verifier.hpp"

namespace zir::verifier_detail {

bool sameType(const std::shared_ptr<Type> &lhs,
              const std::shared_ptr<Type> &rhs);
bool isAssignable(const std::shared_ptr<Type> &actual,
                  const std::shared_ptr<Type> &expected);
bool isTerminator(OpCode opcode);
bool isStringType(const std::shared_ptr<Type> &type);
std::shared_ptr<Value> instructionResult(const Instruction &instruction);
std::string typeName(const std::shared_ptr<Type> &type);

void verifyDefinedFunction(const Module &module, const Function &function,
                           std::vector<VerificationError> &errors);

} // namespace zir::verifier_detail

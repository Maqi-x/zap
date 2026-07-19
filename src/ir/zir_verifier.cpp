#include "zir_verifier_internal.hpp"

#include <sstream>
#include <unordered_set>

namespace zir {

std::string VerificationResult::format() const {
  std::ostringstream output;
  for (size_t i = 0; i < errors_.size(); ++i) {
    const auto &error = errors_[i];
    if (i) {
      output << '\n';
    }
    output << "in function @" << error.function;
    if (!error.block.empty()) {
      output << ", block %" << error.block;
    }
    if (error.instructionIndex) {
      output << ", instruction " << *error.instructionIndex;
    }
    output << ": " << error.message;
  }
  return output.str();
}

VerificationResult ZirVerifier::verify(const Module &module) const {
  VerificationResult result;
  std::vector<VerificationError> errors;
  std::unordered_set<std::string> symbols;
  TypeInterner typeInterner;
  auto registerFunction = [&](const std::unique_ptr<Function> &function,
                              bool external) {
    if (!function) {
      errors.push_back({VerificationErrorCode::NullNode, {}, {}, std::nullopt,
                        "module contains a null function"});
      return;
    }
    if (!symbols.insert(function->name).second) {
      errors.push_back(
          {VerificationErrorCode::DuplicateSymbol, function->name, {},
           std::nullopt, "duplicate function symbol " + function->name});
    }
    if (external) {
      if (!function->getBlocks().empty()) {
        errors.push_back(
            {VerificationErrorCode::InvalidResult, function->name, {},
             std::nullopt, "external function must not have a body"});
      }
      return;
    }
    verifier_detail::verifyDefinedFunction(module, *function, errors,
                                           typeInterner);
  };

  for (const auto &function : module.getExternalFunctions()) {
    registerFunction(function, true);
  }
  for (const auto &function : module.getFunctions()) {
    registerFunction(function, false);
  }
  result.errors_ = std::move(errors);
  return result;
}

} // namespace zir

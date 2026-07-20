#include "ir/zir_verifier.hpp"

#include <iostream>
#include <memory>
#include <string>

namespace {

using zir::BasicBlock;
using zir::BranchInst;
using zir::ClassType;
using zir::CmpInst;
using zir::CondBranchInst;
using zir::Constant;
using zir::Function;
using zir::LoadInst;
using zir::Module;
using zir::OpCode;
using zir::PhiInst;
using zir::PointerType;
using zir::PrimitiveType;
using zir::Register;
using zir::ReturnInst;
using zir::StoreInst;
using zir::StoreMode;
using zir::Type;
using zir::TypeKind;
using zir::ValueOwnership;
using zir::VerificationErrorCode;
using zir::WeakLockInst;
using zir::ZirVerifier;

std::shared_ptr<Type> primitive(TypeKind kind) {
  return std::make_shared<PrimitiveType>(kind);
}

std::shared_ptr<Register> reg(const std::string &name,
                              const std::shared_ptr<Type> &type) {
  return std::make_shared<Register>(name, type);
}

bool hasError(const zir::VerificationResult &result,
              VerificationErrorCode code) {
  for (const auto &error : result.errors()) {
    if (error.code == code) {
      return true;
    }
  }
  return false;
}

std::unique_ptr<Function> validFunction() {
  auto i32 = primitive(TypeKind::Int32);
  auto boolean = primitive(TypeKind::Bool);
  auto function = std::make_unique<Function>("valid", i32);

  auto entry = std::make_unique<BasicBlock>("entry");
  auto slot = reg("slot", std::make_shared<PointerType>(i32));
  auto loaded = reg("loaded", i32);
  auto condition = reg("condition", boolean);
  entry->addInstruction(
      std::make_unique<zir::AllocaInst>(slot, i32));
  entry->addInstruction(std::make_unique<StoreInst>(
      std::make_shared<Constant>("4", i32), slot, zir::StoreMode::Assign));
  entry->addInstruction(std::make_unique<LoadInst>(loaded, slot));
  entry->addInstruction(std::make_unique<CmpInst>(
      "eq", condition, loaded, std::make_shared<Constant>("4", i32)));
  entry->addInstruction(
      std::make_unique<CondBranchInst>(condition, "left", "right"));

  auto left = std::make_unique<BasicBlock>("left");
  auto leftValue = reg("left.value", i32);
  left->addInstruction(std::make_unique<zir::BinaryInst>(
      OpCode::Add, leftValue, loaded, std::make_shared<Constant>("1", i32)));
  left->addInstruction(std::make_unique<BranchInst>("merge"));

  auto right = std::make_unique<BasicBlock>("right");
  auto rightValue = reg("right.value", i32);
  right->addInstruction(std::make_unique<zir::BinaryInst>(
      OpCode::Sub, rightValue, loaded, std::make_shared<Constant>("1", i32)));
  right->addInstruction(std::make_unique<BranchInst>("merge"));

  auto merge = std::make_unique<BasicBlock>("merge");
  auto result = reg("result", i32);
  merge->addInstruction(std::make_unique<PhiInst>(
      result, std::vector<std::pair<std::string, std::shared_ptr<zir::Value>>>{
                  {"left", leftValue}, {"right", rightValue}}));
  merge->addInstruction(std::make_unique<ReturnInst>(result));

  function->addBlock(std::move(entry));
  function->addBlock(std::move(left));
  function->addBlock(std::move(right));
  function->addBlock(std::move(merge));
  return function;
}

bool expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << message << '\n';
    return false;
  }
  return true;
}

bool testValidFunction() {
  Module module("valid-module");
  module.addFunction(validFunction());
  auto result = ZirVerifier().verify(module);
  return expect(result.ok(), "valid ZIR was rejected:\n" + result.format());
}

bool testMissingTerminator() {
  Module module("missing-terminator");
  auto function = std::make_unique<Function>("broken", primitive(TypeKind::Void));
  function->addBlock(std::make_unique<BasicBlock>("entry"));
  module.addFunction(std::move(function));
  auto result = ZirVerifier().verify(module);
  return expect(hasError(result, VerificationErrorCode::MissingTerminator),
                "missing terminator was not diagnosed");
}

bool testUnknownBranchTarget() {
  Module module("unknown-target");
  auto function = std::make_unique<Function>("broken", primitive(TypeKind::Void));
  auto entry = std::make_unique<BasicBlock>("entry");
  entry->addInstruction(std::make_unique<BranchInst>("missing"));
  function->addBlock(std::move(entry));
  module.addFunction(std::move(function));
  auto result = ZirVerifier().verify(module);
  return expect(hasError(result, VerificationErrorCode::InvalidBranchTarget),
                "unknown branch target was not diagnosed");
}

bool testInstructionAfterTerminator() {
  Module module("instruction-after-terminator");
  auto i32 = primitive(TypeKind::Int32);
  auto function = std::make_unique<Function>("broken", primitive(TypeKind::Void));
  auto entry = std::make_unique<BasicBlock>("entry");
  auto dead = reg("dead", i32);
  entry->addInstruction(std::make_unique<ReturnInst>());
  entry->addInstruction(std::make_unique<zir::BinaryInst>(
      OpCode::Add, dead, std::make_shared<Constant>("1", i32),
      std::make_shared<Constant>("2", i32)));
  function->addBlock(std::move(entry));
  module.addFunction(std::move(function));
  auto result = ZirVerifier().verify(module);
  return expect(
      hasError(result, VerificationErrorCode::InstructionAfterTerminator),
      "instruction after terminator was not diagnosed");
}

bool testUseBeforeDefinition() {
  Module module("use-before-definition");
  auto i32 = primitive(TypeKind::Int32);
  auto function = std::make_unique<Function>("broken", i32);
  auto entry = std::make_unique<BasicBlock>("entry");
  auto value = reg("late", i32);
  auto result = reg("result", i32);
  entry->addInstruction(std::make_unique<zir::BinaryInst>(
      OpCode::Add, result, value, std::make_shared<Constant>("1", i32)));
  entry->addInstruction(std::make_unique<zir::BinaryInst>(
      OpCode::Add, value, std::make_shared<Constant>("1", i32),
      std::make_shared<Constant>("2", i32)));
  entry->addInstruction(std::make_unique<ReturnInst>(result));
  function->addBlock(std::move(entry));
  module.addFunction(std::move(function));
  auto verification = ZirVerifier().verify(module);
  return expect(
      hasError(verification, VerificationErrorCode::UseBeforeDefinition),
      "use before definition was not diagnosed");
}

bool testStoreTypeMismatch() {
  Module module("store-type-mismatch");
  auto i32 = primitive(TypeKind::Int32);
  auto i64 = primitive(TypeKind::Int64);
  auto function = std::make_unique<Function>("broken", primitive(TypeKind::Void));
  auto entry = std::make_unique<BasicBlock>("entry");
  auto slot = reg("slot", std::make_shared<PointerType>(i32));
  entry->addInstruction(std::make_unique<zir::AllocaInst>(slot, i32));
  entry->addInstruction(std::make_unique<StoreInst>(
      std::make_shared<Constant>("1", i64), slot, zir::StoreMode::Assign));
  entry->addInstruction(std::make_unique<ReturnInst>());
  function->addBlock(std::move(entry));
  module.addFunction(std::move(function));
  auto result = ZirVerifier().verify(module);
  return expect(hasError(result, VerificationErrorCode::TypeMismatch),
                "store type mismatch was not diagnosed");
}

bool testStoreModeRendering() {
  auto i32 = primitive(TypeKind::Int32);
  auto slot = reg("slot", std::make_shared<PointerType>(i32));
  auto value = std::make_shared<Constant>("1", i32);
  const std::vector<std::pair<StoreMode, std::string>> cases = {
      {StoreMode::Assign, "store.assign"},
      {StoreMode::Initialize, "store.initialize"},
      {StoreMode::RawAssign, "store.raw_assign"},
      {StoreMode::RawInitialize, "store.raw_initialize"},
  };
  for (const auto &[mode, prefix] : cases) {
    StoreInst store(value, slot, mode);
    const std::string rendered = store.toString();
    if (!expect(rendered.compare(0, prefix.size(), prefix) == 0,
                "store mode is missing from ZIR rendering")) {
      return false;
    }
  }
  return true;
}

bool testAllocRequiresOwnedResult() {
  Module module("alloc-ownership");
  auto classType = std::make_shared<zir::ClassType>("Node");
  auto function =
      std::make_unique<Function>("broken", primitive(TypeKind::Void));
  auto entry = std::make_unique<BasicBlock>("entry");
  auto result = reg("node", classType);
  entry->addInstruction(std::make_unique<zir::AllocInst>(result, classType));
  entry->addInstruction(std::make_unique<ReturnInst>());
  function->addBlock(std::move(entry));
  module.addFunction(std::move(function));
  auto verification = ZirVerifier().verify(module);
  return expect(hasError(verification, VerificationErrorCode::InvalidResult),
                "borrowed alloc result was not diagnosed");
}

bool testWeakLockRequiresOwnedStrongResult() {
  Module module("weak-lock-ownership");
  auto weakType = std::make_shared<ClassType>("Node");
  weakType->setWeak(true);
  auto strongType = std::make_shared<ClassType>(*weakType);
  strongType->setWeak(false);
  auto function =
      std::make_unique<Function>("broken", primitive(TypeKind::Void));
  auto weakValue = std::make_shared<zir::Argument>("weak", weakType);
  function->arguments.push_back(weakValue);
  auto entry = std::make_unique<BasicBlock>("entry");
  auto result = reg("locked", strongType);
  entry->addInstruction(std::make_unique<WeakLockInst>(result, weakValue));
  entry->addInstruction(std::make_unique<ReturnInst>());
  function->addBlock(std::move(entry));
  module.addFunction(std::move(function));

  auto verification = ZirVerifier().verify(module);
  return expect(hasError(verification, VerificationErrorCode::InvalidResult),
                "borrowed weak.lock result was not diagnosed");
}

bool testDominanceViolation() {
  Module module("dominance-violation");
  auto i32 = primitive(TypeKind::Int32);
  auto boolean = primitive(TypeKind::Bool);
  auto function = std::make_unique<Function>("broken", i32);

  auto entry = std::make_unique<BasicBlock>("entry");
  entry->addInstruction(std::make_unique<CondBranchInst>(
      std::make_shared<Constant>("true", boolean), "defines", "skips"));

  auto defines = std::make_unique<BasicBlock>("defines");
  auto onePathValue = reg("one.path", i32);
  defines->addInstruction(std::make_unique<zir::BinaryInst>(
      OpCode::Add, onePathValue, std::make_shared<Constant>("1", i32),
      std::make_shared<Constant>("2", i32)));
  defines->addInstruction(std::make_unique<BranchInst>("merge"));

  auto skips = std::make_unique<BasicBlock>("skips");
  skips->addInstruction(std::make_unique<BranchInst>("merge"));

  auto merge = std::make_unique<BasicBlock>("merge");
  merge->addInstruction(std::make_unique<ReturnInst>(onePathValue));

  function->addBlock(std::move(entry));
  function->addBlock(std::move(defines));
  function->addBlock(std::move(skips));
  function->addBlock(std::move(merge));
  module.addFunction(std::move(function));
  auto result = ZirVerifier().verify(module);
  return expect(hasError(result, VerificationErrorCode::DominanceViolation),
                "non-dominating definition was not diagnosed");
}

bool testPhiRequiresEveryPredecessor() {
  Module module("incomplete-phi");
  auto i32 = primitive(TypeKind::Int32);
  auto boolean = primitive(TypeKind::Bool);
  auto function = std::make_unique<Function>("broken", i32);
  auto entry = std::make_unique<BasicBlock>("entry");
  entry->addInstruction(std::make_unique<CondBranchInst>(
      std::make_shared<Constant>("true", boolean), "left", "right"));
  auto left = std::make_unique<BasicBlock>("left");
  left->addInstruction(std::make_unique<BranchInst>("merge"));
  auto right = std::make_unique<BasicBlock>("right");
  right->addInstruction(std::make_unique<BranchInst>("merge"));
  auto merge = std::make_unique<BasicBlock>("merge");
  auto result = reg("result", i32);
  merge->addInstruction(std::make_unique<PhiInst>(
      result, std::vector<std::pair<std::string, std::shared_ptr<zir::Value>>>{
                  {"left", std::make_shared<Constant>("1", i32)}}));
  merge->addInstruction(std::make_unique<ReturnInst>(result));
  function->addBlock(std::move(entry));
  function->addBlock(std::move(left));
  function->addBlock(std::move(right));
  function->addBlock(std::move(merge));
  module.addFunction(std::move(function));
  auto verification = ZirVerifier().verify(module);
  return expect(hasError(verification, VerificationErrorCode::InvalidPhi),
                "incomplete phi was not diagnosed");
}

} // namespace

int main() {
  bool ok = true;
  ok = testValidFunction() && ok;
  ok = testMissingTerminator() && ok;
  ok = testUnknownBranchTarget() && ok;
  ok = testInstructionAfterTerminator() && ok;
  ok = testUseBeforeDefinition() && ok;
  ok = testStoreTypeMismatch() && ok;
  ok = testStoreModeRendering() && ok;
  ok = testAllocRequiresOwnedResult() && ok;
  ok = testWeakLockRequiresOwnedStrongResult() && ok;
  ok = testDominanceViolation() && ok;
  ok = testPhiRequiresEveryPredecessor() && ok;
  return ok ? 0 : 1;
}

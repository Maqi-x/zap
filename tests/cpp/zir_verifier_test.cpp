#include "ir/string_type.hpp"
#include "ir/ownership_flow.hpp"
#include "ir/ownership_liveness.hpp"
#include "ir/ownership_lowering.hpp"
#include "ir/zir_verifier.hpp"

#include <iostream>
#include <memory>
#include <string>

namespace {

using zir::BasicBlock;
using zir::BranchInst;
using zir::CastInst;
using zir::ClassType;
using zir::CmpInst;
using zir::CondBranchInst;
using zir::Constant;
using zir::Function;
using zir::FunctionPointerType;
using zir::FunctionReference;
using zir::LoadInst;
using zir::Module;
using zir::OpCode;
using zir::OwnershipFlowAnalysis;
using zir::OwnershipFlowState;
using zir::PhiInst;
using zir::PointerType;
using zir::PrimitiveType;
using zir::ReleaseInst;
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

bool testManagedCallRequiresOwnedResult() {
  Module module("managed-call-ownership");
  auto stringType = zir::makeStringType();
  auto functionType = std::make_shared<FunctionPointerType>(
      std::vector<std::shared_ptr<Type>>{}, stringType);
  auto callee =
      std::make_shared<FunctionReference>("make_string", functionType);
  auto function =
      std::make_unique<Function>("broken", primitive(TypeKind::Void));
  auto entry = std::make_unique<BasicBlock>("entry");
  auto result = reg("text", stringType);
  entry->addInstruction(std::make_unique<zir::CallInst>(
      result, callee, std::vector<std::shared_ptr<zir::Value>>{}, false,
      zir::CallInst::ResultOwnership::Owned));
  entry->addInstruction(std::make_unique<ReturnInst>());
  function->addBlock(std::move(entry));
  module.addFunction(std::move(function));

  auto verification = ZirVerifier().verify(module);
  return expect(hasError(verification, VerificationErrorCode::InvalidResult),
                "borrowed managed call result was not diagnosed");
}

bool testManagedTypeClassification() {
  auto stringType = zir::makeStringType();
  auto stringViewType = zir::makeStringViewType();
  auto record = std::make_shared<zir::RecordType>("TextBox");
  record->addField("text", stringType);
  auto array = std::make_shared<zir::ArrayType>(record, 2);

  return expect(zir::containsManagedValues(stringType),
                "String was not classified as managed") &&
         expect(!zir::containsManagedValues(stringViewType),
                "StringView was classified as managed") &&
         expect(zir::containsManagedValues(record),
                "record containing String was not classified as managed") &&
         expect(
             zir::containsManagedValues(array),
             "array containing managed records was not classified as managed");
}

bool testPhiRequiresOwnershipMatchingIncomingValues() {
  Module module("phi-ownership");
  auto stringType = zir::makeStringType();
  auto boolean = primitive(TypeKind::Bool);
  auto function =
      std::make_unique<Function>("broken", primitive(TypeKind::Void));
  auto leftValue = std::make_shared<zir::Argument>("left.value", stringType);
  auto rightValue = std::make_shared<zir::Argument>("right.value", stringType);
  leftValue->setOwnership(ValueOwnership::Owned);
  rightValue->setOwnership(ValueOwnership::Owned);
  function->arguments.push_back(leftValue);
  function->arguments.push_back(rightValue);

  auto entry = std::make_unique<BasicBlock>("entry");
  entry->addInstruction(std::make_unique<CondBranchInst>(
      std::make_shared<Constant>("true", boolean), "left", "right"));
  auto left = std::make_unique<BasicBlock>("left");
  left->addInstruction(std::make_unique<BranchInst>("merge"));
  auto right = std::make_unique<BasicBlock>("right");
  right->addInstruction(std::make_unique<BranchInst>("merge"));
  auto merge = std::make_unique<BasicBlock>("merge");
  auto result = reg("result", stringType);
  merge->addInstruction(std::make_unique<PhiInst>(
      result, std::vector<std::pair<std::string, std::shared_ptr<zir::Value>>>{
                  {"left", leftValue}, {"right", rightValue}}));
  merge->addInstruction(std::make_unique<ReturnInst>());

  function->addBlock(std::move(entry));
  function->addBlock(std::move(left));
  function->addBlock(std::move(right));
  function->addBlock(std::move(merge));
  module.addFunction(std::move(function));

  auto verification = ZirVerifier().verify(module);
  return expect(
      hasError(verification, VerificationErrorCode::InvalidResult),
      "borrowed phi result with owned incoming values was not diagnosed");
}

bool testReturnRequiresOwnershipMatchingValue() {
  Module module("return-ownership");
  auto stringType = zir::makeStringType();
  auto function = std::make_unique<Function>("broken", stringType);
  auto value = std::make_shared<zir::Argument>("value", stringType);
  value->setOwnership(ValueOwnership::Owned);
  function->arguments.push_back(value);

  auto entry = std::make_unique<BasicBlock>("entry");
  entry->addInstruction(
      std::make_unique<ReturnInst>(value, ValueOwnership::Borrowed));
  function->addBlock(std::move(entry));
  module.addFunction(std::move(function));

  auto verification = ZirVerifier().verify(module);
  return expect(hasError(verification, VerificationErrorCode::InvalidReturn),
                "borrowed return of an owned value was not diagnosed");
}

bool testStoreRequiresOwnershipMatchingSource() {
  Module module("store-ownership");
  auto stringType = zir::makeStringType();
  auto function =
      std::make_unique<Function>("broken", primitive(TypeKind::Void));
  auto value = std::make_shared<zir::Argument>("value", stringType);
  value->setOwnership(ValueOwnership::Owned);
  function->arguments.push_back(value);

  auto entry = std::make_unique<BasicBlock>("entry");
  auto slot = reg("slot", std::make_shared<PointerType>(stringType));
  entry->addInstruction(std::make_unique<zir::AllocaInst>(slot, stringType));
  entry->addInstruction(std::make_unique<StoreInst>(
      value, slot, StoreMode::Assign, ValueOwnership::Borrowed));
  entry->addInstruction(std::make_unique<ReturnInst>());
  function->addBlock(std::move(entry));
  module.addFunction(std::move(function));

  auto verification = ZirVerifier().verify(module);
  return expect(hasError(verification, VerificationErrorCode::InvalidOperand),
                "borrowed store of an owned value was not diagnosed");
}

bool testCastRequiresOwnershipMatchingSourceAndTarget() {
  Module module("cast-ownership");
  auto strongType = std::make_shared<ClassType>("Node");
  auto weakType = std::make_shared<ClassType>(*strongType);
  weakType->setWeak(true);
  auto function =
      std::make_unique<Function>("broken", primitive(TypeKind::Void));
  auto source = std::make_shared<zir::Argument>("source", strongType);
  source->setOwnership(ValueOwnership::Owned);
  function->arguments.push_back(source);

  auto entry = std::make_unique<BasicBlock>("entry");
  auto result = reg("result", weakType);
  entry->addInstruction(std::make_unique<CastInst>(result, source, weakType));
  entry->addInstruction(std::make_unique<ReturnInst>());
  function->addBlock(std::move(entry));
  module.addFunction(std::move(function));

  auto verification = ZirVerifier().verify(module);
  return expect(hasError(verification, VerificationErrorCode::InvalidResult),
                "borrowed managed cast result was not diagnosed");
}

bool testCallRequiresOwnershipMatchingArguments() {
  Module module("call-ownership");
  auto stringType = zir::makeStringType();
  auto function =
      std::make_unique<Function>("broken", primitive(TypeKind::Void));
  auto argument = std::make_shared<zir::Argument>("argument", stringType);
  argument->setOwnership(ValueOwnership::Owned);
  function->arguments.push_back(argument);

  auto entry = std::make_unique<BasicBlock>("entry");
  auto calleeType = std::make_shared<FunctionPointerType>(
      std::vector<std::shared_ptr<Type>>{stringType},
      primitive(TypeKind::Void));
  auto callee = std::make_shared<FunctionReference>("callee", calleeType);
  argument->setOwnership(ValueOwnership::Borrowed);
  entry->addInstruction(std::make_unique<zir::CallInst>(
      nullptr, callee, std::vector<std::shared_ptr<zir::Value>>{argument},
      false, zir::CallInst::ResultOwnership::Borrowed,
      std::vector<zir::CallInst::ArgumentMode>{
          zir::CallInst::ArgumentMode::Transfer}));
  entry->addInstruction(std::make_unique<ReturnInst>());
  function->addBlock(std::move(entry));
  module.addFunction(std::move(function));

  auto verification = ZirVerifier().verify(module);
  return expect(
      hasError(verification, VerificationErrorCode::InvalidCall),
      "transfer of a borrowed call argument was not diagnosed");
}

bool testOwnershipTransferAcrossControlFlow() {
  Module module("ownership-control-flow");
  auto classType = std::make_shared<ClassType>("Node");
  auto boolean = primitive(TypeKind::Bool);
  auto function = std::make_unique<Function>("broken", classType);
  auto value = std::make_shared<zir::Argument>("value", classType);
  value->setOwnership(ValueOwnership::Owned);
  function->arguments.push_back(value);

  auto entry = std::make_unique<BasicBlock>("entry");
  entry->addInstruction(std::make_unique<CondBranchInst>(
      std::make_shared<Constant>("true", boolean), "consume", "skip"));

  auto consume = std::make_unique<BasicBlock>("consume");
  auto slot = reg("slot", std::make_shared<PointerType>(classType));
  consume->addInstruction(std::make_unique<zir::AllocaInst>(slot, classType));
  consume->addInstruction(
      std::make_unique<StoreInst>(value, slot, StoreMode::Assign));
  consume->addInstruction(std::make_unique<BranchInst>("merge"));

  auto skip = std::make_unique<BasicBlock>("skip");
  skip->addInstruction(std::make_unique<BranchInst>("merge"));

  auto merge = std::make_unique<BasicBlock>("merge");
  merge->addInstruction(std::make_unique<ReturnInst>(value));

  function->addBlock(std::move(entry));
  function->addBlock(std::move(consume));
  function->addBlock(std::move(skip));
  function->addBlock(std::move(merge));
  module.addFunction(std::move(function));

  auto verification = ZirVerifier().verify(module);
  return expect(
      hasError(verification, VerificationErrorCode::OwnershipViolation),
      "double ownership transfer across control flow was not diagnosed");
}

bool testPhiTransfersOwnershipOnIncomingEdge() {
  Module module("phi-ownership-transfer");
  auto classType = std::make_shared<ClassType>("Node");
  auto boolean = primitive(TypeKind::Bool);
  auto function = std::make_unique<Function>("broken", classType);
  auto value = std::make_shared<zir::Argument>("value", classType);
  value->setOwnership(ValueOwnership::Owned);
  function->arguments.push_back(value);

  auto entry = std::make_unique<BasicBlock>("entry");
  entry->addInstruction(std::make_unique<CondBranchInst>(
      std::make_shared<Constant>("true", boolean), "left", "right"));

  auto left = std::make_unique<BasicBlock>("left");
  auto slot = reg("slot", std::make_shared<PointerType>(classType));
  left->addInstruction(std::make_unique<zir::AllocaInst>(slot, classType));
  left->addInstruction(
      std::make_unique<StoreInst>(value, slot, StoreMode::Assign));
  left->addInstruction(std::make_unique<BranchInst>("merge"));

  auto right = std::make_unique<BasicBlock>("right");
  right->addInstruction(std::make_unique<BranchInst>("merge"));

  auto merge = std::make_unique<BasicBlock>("merge");
  auto result = reg("result", classType);
  result->setOwnership(ValueOwnership::Owned);
  merge->addInstruction(std::make_unique<PhiInst>(
      result, std::vector<std::pair<std::string, std::shared_ptr<zir::Value>>>{
                  {"left", value}, {"right", value}}));
  merge->addInstruction(std::make_unique<ReturnInst>(result));

  function->addBlock(std::move(entry));
  function->addBlock(std::move(left));
  function->addBlock(std::move(right));
  function->addBlock(std::move(merge));
  module.addFunction(std::move(function));

  auto verification = ZirVerifier().verify(module);
  return expect(hasError(verification, VerificationErrorCode::OwnershipViolation),
                "phi ownership transfer on its incoming edge was not diagnosed");
}

bool testPhiAllowsSeparateAlternativeOwnershipTransfers() {
  Module module("phi-alternative-ownership-transfer");
  auto classType = std::make_shared<ClassType>("Node");
  auto boolean = primitive(TypeKind::Bool);
  auto function = std::make_unique<Function>("valid", classType);
  auto value = std::make_shared<zir::Argument>("value", classType);
  value->setOwnership(ValueOwnership::Owned);
  function->arguments.push_back(value);

  auto entry = std::make_unique<BasicBlock>("entry");
  entry->addInstruction(std::make_unique<CondBranchInst>(
      std::make_shared<Constant>("true", boolean), "left", "right"));
  auto left = std::make_unique<BasicBlock>("left");
  left->addInstruction(std::make_unique<BranchInst>("merge"));
  auto right = std::make_unique<BasicBlock>("right");
  right->addInstruction(std::make_unique<BranchInst>("merge"));

  auto merge = std::make_unique<BasicBlock>("merge");
  auto result = reg("result", classType);
  result->setOwnership(ValueOwnership::Owned);
  merge->addInstruction(std::make_unique<PhiInst>(
      result, std::vector<std::pair<std::string, std::shared_ptr<zir::Value>>>{
                  {"left", value}, {"right", value}}));
  merge->addInstruction(std::make_unique<ReturnInst>(result));

  function->addBlock(std::move(entry));
  function->addBlock(std::move(left));
  function->addBlock(std::move(right));
  function->addBlock(std::move(merge));
  module.addFunction(std::move(function));

  auto verification = ZirVerifier().verify(module);
  return expect(verification.ok(),
                "separate phi edge ownership transfers were rejected:\n" +
                    verification.format());
}

bool testOwnershipLivenessTracksPhiEdges() {
  auto classType = std::make_shared<ClassType>("Node");
  auto boolean = primitive(TypeKind::Bool);
  auto function = std::make_unique<Function>("liveness", classType);
  auto value = std::make_shared<zir::Argument>("value", classType);
  value->setOwnership(ValueOwnership::Owned);
  function->arguments.push_back(value);

  auto entry = std::make_unique<BasicBlock>("entry");
  entry->addInstruction(std::make_unique<CondBranchInst>(
      std::make_shared<Constant>("true", boolean), "left", "right"));
  auto left = std::make_unique<BasicBlock>("left");
  left->addInstruction(std::make_unique<BranchInst>("merge"));
  auto right = std::make_unique<BasicBlock>("right");
  right->addInstruction(std::make_unique<BranchInst>("merge"));
  auto merge = std::make_unique<BasicBlock>("merge");
  auto result = reg("result", classType);
  result->setOwnership(ValueOwnership::Owned);
  merge->addInstruction(std::make_unique<PhiInst>(
      result, std::vector<std::pair<std::string, std::shared_ptr<zir::Value>>>{
                  {"left", value}, {"right", value}}));
  merge->addInstruction(std::make_unique<ReturnInst>(result));

  auto *leftBlock = left.get();
  auto *rightBlock = right.get();
  auto *mergeBlock = merge.get();
  function->addBlock(std::move(entry));
  function->addBlock(std::move(left));
  function->addBlock(std::move(right));
  function->addBlock(std::move(merge));

  const auto liveness = zir::analyzeOwnershipLiveness(*function);
  return expect(liveness.isLiveOnEdge(*leftBlock, *mergeBlock, value) &&
                    liveness.isLiveOnEdge(*rightBlock, *mergeBlock, value),
                "owned phi inputs were not live on their incoming edges") &&
         expect(!liveness.isLiveAtBlockEntry(*mergeBlock, value) &&
                    liveness.isLiveAfter(*mergeBlock, 0, result),
                "phi liveness did not transfer ownership to its result");
}

bool testOwnershipFlowTracksEdgesMergesAndLoops() {
  auto classType = std::make_shared<ClassType>("Node");
  auto boolean = primitive(TypeKind::Bool);
  Module module("ownership-flow");

  auto branchFunction =
      std::make_unique<Function>("branch", primitive(TypeKind::Void));
  auto value = std::make_shared<zir::Argument>("value", classType);
  value->setOwnership(ValueOwnership::Owned);
  branchFunction->arguments.push_back(value);
  auto entry = std::make_unique<BasicBlock>("entry");
  entry->addInstruction(std::make_unique<CondBranchInst>(
      std::make_shared<Constant>("true", boolean), "left", "right"));
  auto left = std::make_unique<BasicBlock>("left");
  left->addInstruction(std::make_unique<ReleaseInst>(value));
  left->addInstruction(std::make_unique<BranchInst>("exit"));
  auto right = std::make_unique<BasicBlock>("right");
  right->addInstruction(std::make_unique<BranchInst>("exit"));
  auto exit = std::make_unique<BasicBlock>("exit");
  exit->addInstruction(std::make_unique<ReturnInst>());
  auto *entryBlock = entry.get();
  auto *leftBlock = left.get();
  auto *rightBlock = right.get();
  auto *exitBlock = exit.get();
  branchFunction->addBlock(std::move(entry));
  branchFunction->addBlock(std::move(left));
  branchFunction->addBlock(std::move(right));
  branchFunction->addBlock(std::move(exit));
  OwnershipFlowAnalysis::BlockEdges predecessors{{entryBlock, {}},
                                                  {leftBlock, {entryBlock}},
                                                  {rightBlock, {entryBlock}},
                                                  {exitBlock,
                                                   {leftBlock, rightBlock}}};
  OwnershipFlowAnalysis::BlockEdges successors{{entryBlock,
                                                {leftBlock, rightBlock}},
                                               {leftBlock, {exitBlock}},
                                               {rightBlock, {exitBlock}},
                                               {exitBlock, {}}};
  OwnershipFlowAnalysis branchAnalysis(
      module, *branchFunction, predecessors, successors,
      {entryBlock, leftBlock, rightBlock, exitBlock});
  const auto branchViolations = branchAnalysis.analyze();

  auto loopFunction =
      std::make_unique<Function>("loop", primitive(TypeKind::Void));
  auto loopEntry = std::make_unique<BasicBlock>("entry");
  auto node = reg("node", classType);
  node->setOwnership(ValueOwnership::Owned);
  loopEntry->addInstruction(std::make_unique<zir::AllocInst>(node, classType));
  loopEntry->addInstruction(std::make_unique<BranchInst>("loop"));
  auto loop = std::make_unique<BasicBlock>("loop");
  loop->addInstruction(std::make_unique<CondBranchInst>(
      std::make_shared<Constant>("true", boolean), "loop", "exit"));
  auto loopExit = std::make_unique<BasicBlock>("exit");
  loopExit->addInstruction(std::make_unique<ReturnInst>());
  auto *loopEntryBlock = loopEntry.get();
  auto *loopBlock = loop.get();
  auto *loopExitBlock = loopExit.get();
  loopFunction->addBlock(std::move(loopEntry));
  loopFunction->addBlock(std::move(loop));
  loopFunction->addBlock(std::move(loopExit));
  OwnershipFlowAnalysis::BlockEdges loopPredecessors{
      {loopEntryBlock, {}}, {loopBlock, {loopEntryBlock, loopBlock}},
      {loopExitBlock, {loopBlock}}};
  OwnershipFlowAnalysis::BlockEdges loopSuccessors{
      {loopEntryBlock, {loopBlock}}, {loopBlock, {loopBlock, loopExitBlock}},
      {loopExitBlock, {}}};
  OwnershipFlowAnalysis loopAnalysis(
      module, *loopFunction, loopPredecessors, loopSuccessors,
      {loopEntryBlock, loopBlock, loopExitBlock});
  const auto loopViolations = loopAnalysis.analyze();

  return expect(branchViolations.empty(),
                "ownership flow reported a false branch transfer violation") &&
         expect(branchAnalysis.stateOnEdge(*leftBlock, *exitBlock, value) ==
                    OwnershipFlowState::Consumed &&
                    branchAnalysis.stateOnEdge(*rightBlock, *exitBlock, value) ==
                        OwnershipFlowState::Available,
                "ownership flow did not preserve per-edge branch states") &&
         expect(loopViolations.empty(),
                "ownership flow reported a false loop transfer violation") &&
         expect(loopAnalysis.stateOnEdge(*loopBlock, *loopBlock, node) ==
                    OwnershipFlowState::Available &&
                    loopAnalysis.stateOnEdge(*loopBlock, *loopExitBlock, node) ==
                        OwnershipFlowState::Available,
                "ownership flow did not reach a stable loop state");
}

bool testReleaseConsumesOwnedValue() {
  Module module("release-ownership");
  auto stringType = zir::makeStringType();
  auto function =
      std::make_unique<Function>("broken", primitive(TypeKind::Void));
  auto value = std::make_shared<zir::Argument>("value", stringType);
  value->setOwnership(ValueOwnership::Owned);
  function->arguments.push_back(value);

  auto entry = std::make_unique<BasicBlock>("entry");
  entry->addInstruction(std::make_unique<ReleaseInst>(value));
  entry->addInstruction(std::make_unique<ReleaseInst>(value));
  entry->addInstruction(std::make_unique<ReturnInst>());
  function->addBlock(std::move(entry));
  module.addFunction(std::move(function));

  const auto verification = ZirVerifier().verify(module);
  return expect(hasError(verification, VerificationErrorCode::OwnershipViolation),
                "double release of an owned value was not diagnosed");
}

bool testOwnershipLoweringReleasesDeadOwnedResults() {
  Module module("ownership-lowering");
  auto classType = std::make_shared<ClassType>("Node");
  auto function =
      std::make_unique<Function>("valid", primitive(TypeKind::Void));
  auto entry = std::make_unique<BasicBlock>("entry");
  auto result = reg("node", classType);
  result->setOwnership(ValueOwnership::Owned);
  entry->addInstruction(std::make_unique<zir::AllocInst>(result, classType));
  entry->addInstruction(std::make_unique<ReturnInst>());
  function->addBlock(std::move(entry));
  module.addFunction(std::move(function));

  zir::lowerDeadOwnedResults(module);
  const auto &instructions = module.getFunctions().front()->getBlocks().front()
                                 ->getInstructions();
  return expect(instructions.size() == 3 &&
                    instructions[1]->getOpCode() == OpCode::Release,
                "ownership lowering did not release a dead owned result") &&
         expect(ZirVerifier().verify(module).ok(),
                "ownership-lowered ZIR was rejected by the verifier");
}

bool testOwnershipLoweringReleasesAtLastLocalUse() {
  Module module("ownership-last-use-lowering");
  auto classType = std::make_shared<ClassType>("Node");
  auto boolean = primitive(TypeKind::Bool);
  auto function =
      std::make_unique<Function>("valid", primitive(TypeKind::Void));
  auto entry = std::make_unique<BasicBlock>("entry");
  auto node = reg("node", classType);
  node->setOwnership(ValueOwnership::Owned);
  auto comparison = reg("comparison", boolean);
  entry->addInstruction(std::make_unique<zir::AllocInst>(node, classType));
  entry->addInstruction(std::make_unique<CmpInst>(
      "eq", comparison, node, std::make_shared<Constant>("null", classType)));
  entry->addInstruction(std::make_unique<ReturnInst>());
  function->addBlock(std::move(entry));
  module.addFunction(std::move(function));

  zir::lowerDeadOwnedResults(module);
  const auto &instructions = module.getFunctions().front()->getBlocks().front()
                                 ->getInstructions();
  return expect(instructions.size() == 4 &&
                    instructions[2]->getOpCode() == OpCode::Release,
                "ownership lowering did not release at the local last use") &&
         expect(ZirVerifier().verify(module).ok(),
                "last-use-lowered ZIR was rejected by the verifier");
}

bool testCallBorrowAllowsOwnedValueToBeReleasedAfterward() {
  Module module("call-borrow-ownership");
  auto classType = std::make_shared<ClassType>("Node");

  auto make = std::make_unique<Function>("make", classType);
  module.addExternalFunction(std::move(make));
  auto consume =
      std::make_unique<Function>("consume", primitive(TypeKind::Void));
  consume->arguments.push_back(
      std::make_shared<zir::Argument>("value", classType));
  module.addExternalFunction(std::move(consume));

  auto function =
      std::make_unique<Function>("valid", primitive(TypeKind::Void));
  auto entry = std::make_unique<BasicBlock>("entry");
  auto value = reg("value", classType);
  value->setOwnership(ValueOwnership::Owned);
  entry->addInstruction(std::make_unique<zir::CallInst>(
      value, "make", std::vector<std::shared_ptr<zir::Value>>{},
      std::vector<bool>{}, nullptr, false,
      zir::CallInst::ResultOwnership::Owned));
  entry->addInstruction(std::make_unique<zir::CallInst>(
      nullptr, "consume", std::vector<std::shared_ptr<zir::Value>>{value}));
  entry->addInstruction(std::make_unique<ReturnInst>());
  function->addBlock(std::move(entry));
  module.addFunction(std::move(function));

  zir::lowerDeadOwnedResults(module);
  const auto &instructions = module.getFunctions().front()->getBlocks().front()
                                 ->getInstructions();
  return expect(instructions.size() == 4 &&
                    instructions[2]->getOpCode() == OpCode::Release,
                "borrowed call did not preserve a later release") &&
         expect(ZirVerifier().verify(module).ok(),
                "borrowed call of an owned value was rejected");
}

bool testOwnershipLoweringReleasesOnDeadCfgEdge() {
  Module module("ownership-edge-lowering");
  auto classType = std::make_shared<ClassType>("Node");
  auto boolean = primitive(TypeKind::Bool);
  auto function =
      std::make_unique<Function>("valid", primitive(TypeKind::Void));

  auto entry = std::make_unique<BasicBlock>("entry");
  auto node = reg("node", classType);
  node->setOwnership(ValueOwnership::Owned);
  entry->addInstruction(std::make_unique<zir::AllocInst>(node, classType));
  entry->addInstruction(std::make_unique<CondBranchInst>(
      std::make_shared<Constant>("true", boolean), "live", "dead"));

  auto live = std::make_unique<BasicBlock>("live");
  auto comparison = reg("comparison", boolean);
  live->addInstruction(std::make_unique<CmpInst>(
      "eq", comparison, node, std::make_shared<Constant>("null", classType)));
  live->addInstruction(std::make_unique<ReturnInst>());

  auto dead = std::make_unique<BasicBlock>("dead");
  dead->addInstruction(std::make_unique<ReturnInst>());

  function->addBlock(std::move(entry));
  function->addBlock(std::move(live));
  function->addBlock(std::move(dead));
  module.addFunction(std::move(function));

  zir::lowerDeadOwnedResults(module);
  const auto &blocks = module.getFunctions().front()->getBlocks();
  const auto &branch = static_cast<const CondBranchInst &>(
      *blocks.front()->getInstructions().back());
  auto *releaseBlock = module.getFunctions().front()->findBlock(
      branch.getFalseLabel());
  return expect(blocks.size() == 4 && releaseBlock &&
                    releaseBlock->getInstructions().size() == 2 &&
                    releaseBlock->getInstructions().front()->getOpCode() ==
                        OpCode::Release,
                "ownership lowering did not split the dead CFG edge") &&
         expect(ZirVerifier().verify(module).ok(),
                "edge-lowered ZIR was rejected by the verifier");
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
  ok = testManagedCallRequiresOwnedResult() && ok;
  ok = testManagedTypeClassification() && ok;
  ok = testPhiRequiresOwnershipMatchingIncomingValues() && ok;
  ok = testReturnRequiresOwnershipMatchingValue() && ok;
  ok = testStoreRequiresOwnershipMatchingSource() && ok;
  ok = testCastRequiresOwnershipMatchingSourceAndTarget() && ok;
  ok = testCallRequiresOwnershipMatchingArguments() && ok;
  ok = testOwnershipTransferAcrossControlFlow() && ok;
  ok = testPhiTransfersOwnershipOnIncomingEdge() && ok;
  ok = testPhiAllowsSeparateAlternativeOwnershipTransfers() && ok;
  ok = testOwnershipLivenessTracksPhiEdges() && ok;
  ok = testOwnershipFlowTracksEdgesMergesAndLoops() && ok;
  ok = testReleaseConsumesOwnedValue() && ok;
  ok = testOwnershipLoweringReleasesDeadOwnedResults() && ok;
  ok = testOwnershipLoweringReleasesAtLastLocalUse() && ok;
  ok = testCallBorrowAllowsOwnedValueToBeReleasedAfterward() && ok;
  ok = testOwnershipLoweringReleasesOnDeadCfgEdge() && ok;
  ok = testDominanceViolation() && ok;
  ok = testPhiRequiresEveryPredecessor() && ok;
  return ok ? 0 : 1;
}

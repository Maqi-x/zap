#include "ir/ownership_liveness.hpp"
#include "ir/string_type.hpp"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using zir::BasicBlock;
using zir::BorrowInst;
using zir::BranchInst;
using zir::CallInst;
using zir::CondBranchInst;
using zir::Constant;
using zir::Function;
using zir::LoadInst;
using zir::PointerType;
using zir::PrimitiveType;
using zir::Register;
using zir::ReturnInst;
using zir::StoreInst;
using zir::StoreMode;
using zir::Type;
using zir::TypeKind;
using zir::ValueOwnership;

std::shared_ptr<Type> primitive(TypeKind kind) {
  return std::make_shared<PrimitiveType>(kind);
}

std::shared_ptr<Register> reg(const std::string &name,
                              const std::shared_ptr<Type> &type) {
  return std::make_shared<Register>(name, type);
}

bool expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

void addOwnedStringAndView(BasicBlock &block, const std::string &name,
                           const std::shared_ptr<zir::Value> &slot,
                           StoreMode storeMode,
                           std::shared_ptr<Register> &owner) {
  const auto stringType = zir::makeStringType();
  owner = reg(name + ".owner", stringType);
  owner->setOwnership(ValueOwnership::OwnedStrong);
  auto view = reg(name + ".view", zir::makeStringViewType());
  block.addInstruction(std::make_unique<CallInst>(
      owner, "make", std::vector<std::shared_ptr<zir::Value>>{},
      std::vector<bool>{}, nullptr, false, CallInst::ResultOwnership::Owned));
  block.addInstruction(std::make_unique<BorrowInst>(view, owner));
  block.addInstruction(std::make_unique<StoreInst>(view, slot, storeMode));
}

bool testStorageProvenanceCrossesPassThroughBlocks() {
  const auto stringViewType = zir::makeStringViewType();
  auto function =
      std::make_unique<Function>("diamond", primitive(TypeKind::Void));

  auto entry = std::make_unique<BasicBlock>("entry");
  auto slot = reg("slot", std::make_shared<PointerType>(stringViewType));
  entry->addInstruction(
      std::make_unique<zir::AllocaInst>(slot, stringViewType));
  entry->addInstruction(std::make_unique<CondBranchInst>(
      std::make_shared<Constant>("true", primitive(TypeKind::Bool)), "left",
      "right"));

  auto left = std::make_unique<BasicBlock>("left");
  std::shared_ptr<Register> leftOwner;
  addOwnedStringAndView(*left, "left", slot, StoreMode::Initialize, leftOwner);
  left->addInstruction(std::make_unique<BranchInst>("left.forward"));

  auto leftForward = std::make_unique<BasicBlock>("left.forward");
  leftForward->addInstruction(std::make_unique<BranchInst>("merge"));

  auto right = std::make_unique<BasicBlock>("right");
  std::shared_ptr<Register> rightOwner;
  addOwnedStringAndView(*right, "right", slot, StoreMode::Assign, rightOwner);
  right->addInstruction(std::make_unique<BranchInst>("right.forward"));

  auto rightForward = std::make_unique<BasicBlock>("right.forward");
  rightForward->addInstruction(std::make_unique<BranchInst>("merge"));

  auto merge = std::make_unique<BasicBlock>("merge");
  auto loaded = reg("loaded", stringViewType);
  merge->addInstruction(std::make_unique<LoadInst>(loaded, slot));
  merge->addInstruction(std::make_unique<CallInst>(
      nullptr, "consume", std::vector<std::shared_ptr<zir::Value>>{loaded}));
  merge->addInstruction(std::make_unique<ReturnInst>());

  const auto *leftForwardBlock = leftForward.get();
  const auto *rightForwardBlock = rightForward.get();
  const auto *mergeBlock = merge.get();
  function->addBlock(std::move(entry));
  function->addBlock(std::move(left));
  function->addBlock(std::move(leftForward));
  function->addBlock(std::move(right));
  function->addBlock(std::move(rightForward));
  function->addBlock(std::move(merge));

  const auto liveness = zir::analyzeOwnershipLiveness(*function);
  return expect(
      liveness.isLiveOnEdge(*leftForwardBlock, *mergeBlock, leftOwner) &&
          !liveness.isLiveOnEdge(*leftForwardBlock, *mergeBlock, rightOwner) &&
          liveness.isLiveOnEdge(*rightForwardBlock, *mergeBlock, rightOwner) &&
          !liveness.isLiveOnEdge(*rightForwardBlock, *mergeBlock, leftOwner),
      "storage borrow provenance did not cross pass-through CFG blocks");
}

bool testStorageProvenanceReachesLoopBackEdge() {
  const auto stringViewType = zir::makeStringViewType();
  auto function = std::make_unique<Function>("loop", primitive(TypeKind::Void));

  auto entry = std::make_unique<BasicBlock>("entry");
  auto slot = reg("slot", std::make_shared<PointerType>(stringViewType));
  entry->addInstruction(
      std::make_unique<zir::AllocaInst>(slot, stringViewType));
  std::shared_ptr<Register> owner;
  addOwnedStringAndView(*entry, "loop", slot, StoreMode::Initialize, owner);
  entry->addInstruction(std::make_unique<BranchInst>("loop"));

  auto loop = std::make_unique<BasicBlock>("loop");
  auto loaded = reg("loaded", stringViewType);
  loop->addInstruction(std::make_unique<LoadInst>(loaded, slot));
  loop->addInstruction(std::make_unique<CallInst>(
      nullptr, "consume", std::vector<std::shared_ptr<zir::Value>>{loaded}));
  loop->addInstruction(std::make_unique<CondBranchInst>(
      std::make_shared<Constant>("true", primitive(TypeKind::Bool)), "loop",
      "exit"));

  auto exit = std::make_unique<BasicBlock>("exit");
  exit->addInstruction(std::make_unique<ReturnInst>());

  const auto *entryBlock = entry.get();
  const auto *loopBlock = loop.get();
  function->addBlock(std::move(entry));
  function->addBlock(std::move(loop));
  function->addBlock(std::move(exit));

  const auto liveness = zir::analyzeOwnershipLiveness(*function);
  return expect(liveness.isLiveOnEdge(*entryBlock, *loopBlock, owner) &&
                    liveness.isLiveOnEdge(*loopBlock, *loopBlock, owner),
                "storage borrow provenance did not reach the loop back-edge");
}

} // namespace

int main() {
  bool ok = true;
  ok = testStorageProvenanceCrossesPassThroughBlocks() && ok;
  ok = testStorageProvenanceReachesLoopBackEdge() && ok;
  return ok ? 0 : 1;
}

#include "class_arc_emitter.hpp"
#include "llvm_codegen.hpp"

namespace codegen {
bool LLVMCodeGen::isClassType(const std::shared_ptr<zir::Type> &type) const {
  return arcEmitter_->isClassType(type);
}

bool LLVMCodeGen::isWeakClassType(
    const std::shared_ptr<zir::Type> &type) const {
  return arcEmitter_->isWeakClassType(type);
}

bool LLVMCodeGen::expressionProducesOwnedClass(
    const sema::BoundExpression *expr) const {
  return arcEmitter_->expressionProducesOwnedClass(expr);
}

void LLVMCodeGen::emitRetainIfNeeded(llvm::Value *value,
                                     const std::shared_ptr<zir::Type> &type) {
  arcEmitter_->emitRetainIfNeeded(value, type);
}

void LLVMCodeGen::emitReleaseIfNeeded(llvm::Value *value,
                                      const std::shared_ptr<zir::Type> &type) {
  arcEmitter_->emitReleaseIfNeeded(value, type);
}

void LLVMCodeGen::emitRetainWeakIfNeeded(
    llvm::Value *value, const std::shared_ptr<zir::Type> &type) {
  arcEmitter_->emitRetainWeakIfNeeded(value, type);
}

void LLVMCodeGen::emitReleaseWeakIfNeeded(
    llvm::Value *value, const std::shared_ptr<zir::Type> &type) {
  arcEmitter_->emitReleaseWeakIfNeeded(value, type);
}

llvm::Value *
LLVMCodeGen::emitWeakAlive(llvm::Value *value,
                           const std::shared_ptr<zir::Type> &type) {
  return arcEmitter_->emitWeakAlive(value, type);
}

llvm::Value *LLVMCodeGen::emitWeakLock(llvm::Value *value,
                                       const std::shared_ptr<zir::Type> &type) {
  return arcEmitter_->emitWeakLock(value, type);
}

void LLVMCodeGen::emitStoreWithArc(llvm::Value *addr, llvm::Value *value,
                                   const std::shared_ptr<zir::Type> &type,
                                   bool valueIsOwned) {
  arcEmitter_->emitStoreWithArc(addr, value, type, valueIsOwned);
}

void LLVMCodeGen::emitScopeReleases() { arcEmitter_->emitScopeReleases(); }

void LLVMCodeGen::ensureClassArcSupport(
    const std::shared_ptr<zir::ClassType> &classType) {
  arcEmitter_->ensureClassArcSupport(classType);
}

void LLVMCodeGen::ensureArcSupport(sema::BoundRootNode &root) {
  arcEmitter_->ensureArcSupport(root);
}
} // namespace codegen

#include "class_arc_emitter.hpp"
#include "llvm_codegen.hpp"

namespace codegen
{
  bool LLVMCodeGen::isClassType(const std::shared_ptr<zir::Type> &type) const
  {
    return arcEmitter_->isClassType(type);
  }

  bool LLVMCodeGen::expressionProducesOwnedClass(const sema::BoundExpression *expr) const
  {
    return arcEmitter_->expressionProducesOwnedClass(expr);
  }

  void LLVMCodeGen::emitRetainIfNeeded(llvm::Value *value,
                                       const std::shared_ptr<zir::Type> &type)
  {
    arcEmitter_->emitRetainIfNeeded(value, type);
  }

  void LLVMCodeGen::emitReleaseIfNeeded(llvm::Value *value,
                                        const std::shared_ptr<zir::Type> &type)
  {
    arcEmitter_->emitReleaseIfNeeded(value, type);
  }

  void LLVMCodeGen::emitScopeReleases()
  {
    arcEmitter_->emitScopeReleases();
  }

  void LLVMCodeGen::ensureClassArcSupport(const std::shared_ptr<zir::ClassType> &classType)
  {
    arcEmitter_->ensureClassArcSupport(classType);
  }

  void LLVMCodeGen::ensureArcSupport(sema::BoundRootNode &root)
  {
    arcEmitter_->ensureArcSupport(root);
  }
} // namespace codegen

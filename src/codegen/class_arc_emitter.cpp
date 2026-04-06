#include "class_arc_emitter.hpp"
#include "llvm_codegen.hpp"
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>

namespace codegen
{
  ClassArcEmitter::ClassArcEmitter(LLVMCodeGen &codegen) : codegen_(codegen) {}

  bool ClassArcEmitter::isClassType(const std::shared_ptr<zir::Type> &type) const
  {
    return type && type->getKind() == zir::TypeKind::Class;
  }

  bool ClassArcEmitter::expressionProducesOwnedClass(const sema::BoundExpression *expr) const
  {
    if (!expr || !isClassType(expr->type))
    {
      return false;
    }
    if (dynamic_cast<const sema::BoundNewExpression *>(expr))
    {
      return true;
    }
    if (dynamic_cast<const sema::BoundFunctionCall *>(expr))
    {
      return true;
    }
    if (auto cast = dynamic_cast<const sema::BoundCast *>(expr))
    {
      return expressionProducesOwnedClass(cast->expression.get());
    }
    return false;
  }

  void ClassArcEmitter::emitRetainIfNeeded(llvm::Value *value,
                                           const std::shared_ptr<zir::Type> &type)
  {
    if (!value || !isClassType(type))
    {
      return;
    }

    auto *valuePtrTy = llvm::dyn_cast<llvm::PointerType>(value->getType());
    if (!valuePtrTy)
    {
      return;
    }

    auto *retainBB =
        llvm::BasicBlock::Create(codegen_.ctx_, "arc.retain.do", codegen_.currentFn_);
    auto *contBB =
        llvm::BasicBlock::Create(codegen_.ctx_, "arc.retain.cont", codegen_.currentFn_);
    auto *isNull = codegen_.builder_.CreateICmpEQ(
        value, llvm::ConstantPointerNull::get(valuePtrTy), "arc.retain.isnull");
    codegen_.builder_.CreateCondBr(isNull, contBB, retainBB);

    codegen_.builder_.SetInsertPoint(retainBB);
    auto classType = std::static_pointer_cast<zir::ClassType>(type);
    auto *objectTy = codegen_.structCache_.at(classType->getCodegenName() + ".obj");
    auto *typedPtr = codegen_.builder_.CreateBitCast(
        value, llvm::PointerType::getUnqual(objectTy), "arc.retain.cast");
    auto *countAddr =
        codegen_.builder_.CreateStructGEP(objectTy, typedPtr, 0, "arc.retain.count.addr");
    auto *count = codegen_.builder_.CreateLoad(
        llvm::Type::getInt64Ty(codegen_.ctx_), countAddr, "arc.retain.count");
    auto *next = codegen_.builder_.CreateAdd(
        count, llvm::ConstantInt::get(llvm::Type::getInt64Ty(codegen_.ctx_), 1),
        "arc.retain.next");
    codegen_.builder_.CreateStore(next, countAddr);
    codegen_.builder_.CreateBr(contBB);
    codegen_.builder_.SetInsertPoint(contBB);
  }

  void ClassArcEmitter::emitReleaseIfNeeded(llvm::Value *value,
                                            const std::shared_ptr<zir::Type> &type)
  {
    if (!value || !isClassType(type))
    {
      return;
    }

    auto *valuePtrTy = llvm::dyn_cast<llvm::PointerType>(value->getType());
    if (!valuePtrTy)
    {
      return;
    }

    auto *releaseBB =
        llvm::BasicBlock::Create(codegen_.ctx_, "arc.release.do", codegen_.currentFn_);
    auto *contBB =
        llvm::BasicBlock::Create(codegen_.ctx_, "arc.release.cont", codegen_.currentFn_);
    auto *isNull = codegen_.builder_.CreateICmpEQ(
        value, llvm::ConstantPointerNull::get(valuePtrTy), "arc.release.isnull");
    codegen_.builder_.CreateCondBr(isNull, contBB, releaseBB);

    codegen_.builder_.SetInsertPoint(releaseBB);
    auto classType = std::static_pointer_cast<zir::ClassType>(type);
    auto *objectTy = codegen_.structCache_.at(classType->getCodegenName() + ".obj");
    auto *typedPtr = codegen_.builder_.CreateBitCast(
        value, llvm::PointerType::getUnqual(objectTy), "arc.release.cast");
    auto *releaseAddr =
        codegen_.builder_.CreateStructGEP(objectTy, typedPtr, 1, "arc.release.fn.addr");
    auto *releaseFn = codegen_.builder_.CreateLoad(
        llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(codegen_.ctx_)),
        releaseAddr, "arc.release.fn");
    auto *rawObject = codegen_.builder_.CreateBitCast(
        typedPtr, llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(codegen_.ctx_)));
    auto *releaseTy = llvm::FunctionType::get(
        llvm::Type::getVoidTy(codegen_.ctx_),
        {llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(codegen_.ctx_))}, false);
    codegen_.builder_.CreateCall(releaseTy, releaseFn, {rawObject});
    codegen_.builder_.CreateBr(contBB);
    codegen_.builder_.SetInsertPoint(contBB);
  }

  void ClassArcEmitter::emitScopeReleases()
  {
    if (codegen_.scopeClassLocals_.empty())
    {
      return;
    }

    auto &locals = codegen_.scopeClassLocals_.back();
    for (auto it = locals.rbegin(); it != locals.rend(); ++it)
    {
      auto *addr = it->second;
      auto *value = codegen_.builder_.CreateLoad(
          codegen_.toLLVMType(*it->first), addr, "arc.scope.value");
      emitReleaseIfNeeded(value, it->first);
    }
    locals.clear();
  }

  void ClassArcEmitter::ensureClassArcSupport(const std::shared_ptr<zir::ClassType> &classType)
  {
    if (!classType || codegen_.classReleaseFns_.count(classType->getName()))
    {
      return;
    }

    for (const auto &field : classType->getFields())
    {
      if (field.type && field.type->getKind() == zir::TypeKind::Class)
      {
        ensureClassArcSupport(std::static_pointer_cast<zir::ClassType>(field.type));
      }
    }

    if (!codegen_.classVTables_.count(classType->getName()))
    {
      std::vector<llvm::Constant *> entries;
      if (auto base = classType->getBase())
      {
        ensureClassArcSupport(base);
        auto *baseVTable = codegen_.classVTables_.at(base->getName());
        if (auto *baseInit = baseVTable->getInitializer())
        {
          for (unsigned i = 0; i < baseInit->getNumOperands(); ++i)
          {
            entries.push_back(llvm::cast<llvm::Constant>(baseInit->getOperand(i)));
          }
        }
      }

      auto methodsIt = codegen_.classVirtualMethodFns_.find(classType->getName());
      if (methodsIt != codegen_.classVirtualMethodFns_.end())
      {
        auto *i8PtrTy = llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(codegen_.ctx_));
        for (const auto &[slot, fn] : methodsIt->second)
        {
          if (slot >= static_cast<int>(entries.size()))
          {
            entries.resize(static_cast<size_t>(slot + 1),
                           llvm::ConstantPointerNull::get(i8PtrTy));
          }
          entries[slot] = llvm::ConstantExpr::getBitCast(fn, i8PtrTy);
        }
      }

      auto *i8PtrTy = llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(codegen_.ctx_));
      auto *vtableTy =
          llvm::ArrayType::get(i8PtrTy, static_cast<uint64_t>(entries.size()));
      auto *init = llvm::ConstantArray::get(vtableTy, entries);
      auto *gv = new llvm::GlobalVariable(
          *codegen_.module_, vtableTy, true, llvm::GlobalValue::InternalLinkage,
          init, "__zap_vtable_" + classType->getCodegenName());
      codegen_.classVTables_[classType->getName()] = gv;
    }

    auto *objectTy = codegen_.structCache_.at(classType->getCodegenName() + ".obj");
    auto *rawPtrTy = llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(codegen_.ctx_));
    auto *helperTy = llvm::FunctionType::get(llvm::Type::getVoidTy(codegen_.ctx_), {rawPtrTy}, false);
    auto helperName = "__zap_arc_release_" + classType->getCodegenName();
    auto *helper = llvm::Function::Create(
        helperTy, llvm::Function::InternalLinkage, helperName, *codegen_.module_);
    codegen_.classReleaseFns_[classType->getName()] = helper;

    auto savedFn = codegen_.currentFn_;
    auto savedLocals = codegen_.localValues_;
    auto savedBlock = codegen_.builder_.GetInsertBlock();

    codegen_.currentFn_ = helper;
    codegen_.localValues_.clear();
    auto *entry = llvm::BasicBlock::Create(codegen_.ctx_, "entry", helper);
    auto *decrementedBB = llvm::BasicBlock::Create(codegen_.ctx_, "arc.dec", helper);
    auto *destroyBB = llvm::BasicBlock::Create(codegen_.ctx_, "arc.destroy", helper);
    auto *returnBB = llvm::BasicBlock::Create(codegen_.ctx_, "arc.ret", helper);
    codegen_.builder_.SetInsertPoint(entry);

    auto *rawObject = &*helper->arg_begin();
    rawObject->setName("object.raw");
    auto *isNull = codegen_.builder_.CreateICmpEQ(
        rawObject, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(rawPtrTy)));
    codegen_.builder_.CreateCondBr(isNull, returnBB, decrementedBB);

    codegen_.builder_.SetInsertPoint(decrementedBB);
    auto *typedObject = codegen_.builder_.CreateBitCast(
        rawObject, llvm::PointerType::getUnqual(objectTy), "object");
    auto *countAddr =
        codegen_.builder_.CreateStructGEP(objectTy, typedObject, 0, "refcount.addr");
    auto *count = codegen_.builder_.CreateLoad(
        llvm::Type::getInt64Ty(codegen_.ctx_), countAddr, "refcount");
    auto *nextCount = codegen_.builder_.CreateSub(
        count, llvm::ConstantInt::get(llvm::Type::getInt64Ty(codegen_.ctx_), 1), "refcount.next");
    codegen_.builder_.CreateStore(nextCount, countAddr);
    auto *isZero = codegen_.builder_.CreateICmpEQ(
        nextCount, llvm::ConstantInt::get(llvm::Type::getInt64Ty(codegen_.ctx_), 0));
    codegen_.builder_.CreateCondBr(isZero, destroyBB, returnBB);

    codegen_.builder_.SetInsertPoint(destroyBB);
    auto dtorIt = codegen_.classDestructorFns_.find(classType->getName());
    if (dtorIt != codegen_.classDestructorFns_.end())
    {
      auto *dtorPtr = codegen_.builder_.CreateBitCast(
          typedObject, dtorIt->second->getArg(0)->getType(), "dtor.self");
      codegen_.builder_.CreateCall(dtorIt->second, {dtorPtr});
    }

    for (size_t i = 0; i < classType->getFields().size(); ++i)
    {
      const auto &field = classType->getFields()[i];
      if (!field.type || field.type->getKind() != zir::TypeKind::Class)
      {
        continue;
      }
      auto *fieldAddr = codegen_.builder_.CreateStructGEP(
          objectTy, typedObject, static_cast<unsigned>(i + 3));
      auto *fieldValue = codegen_.builder_.CreateLoad(
          codegen_.toLLVMType(*field.type), fieldAddr, field.name);
      emitReleaseIfNeeded(fieldValue, field.type);
    }

    if (codegen_.functionMap_.count("free") == 0)
    {
      auto *freeTy = llvm::FunctionType::get(llvm::Type::getVoidTy(codegen_.ctx_), {rawPtrTy}, false);
      auto *freeFn = llvm::Function::Create(
          freeTy, llvm::Function::ExternalLinkage, "free", *codegen_.module_);
      codegen_.functionMap_["free"] = freeFn;
    }
    codegen_.builder_.CreateCall(codegen_.functionMap_.at("free"), {rawObject});
    codegen_.builder_.CreateBr(returnBB);

    codegen_.builder_.SetInsertPoint(returnBB);
    codegen_.builder_.CreateRetVoid();

    codegen_.currentFn_ = savedFn;
    codegen_.localValues_ = std::move(savedLocals);
    if (savedBlock)
    {
      codegen_.builder_.SetInsertPoint(savedBlock);
    }
  }

  void ClassArcEmitter::ensureArcSupport(sema::BoundRootNode &root)
  {
    for (const auto &rec : root.records)
    {
      if (rec->type && rec->type->getKind() == zir::TypeKind::Class)
      {
        codegen_.toLLVMType(*rec->type);
      }
    }
  }
} // namespace codegen

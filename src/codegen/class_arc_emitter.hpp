#pragma once

#include "../ir/type.hpp"
#include <cstdint>
#include <memory>
#include <vector>

namespace llvm {
class Function;
class Value;
}

namespace codegen {
class LLVMCodeGen;

class ClassArcEmitter {
public:
  explicit ClassArcEmitter(LLVMCodeGen &codegen);

  bool isClassType(const std::shared_ptr<zir::Type> &type) const;
  bool isWeakClassType(const std::shared_ptr<zir::Type> &type) const;
  void emitRetainIfNeeded(llvm::Value *value,
                          const std::shared_ptr<zir::Type> &type);
  void emitReleaseIfNeeded(llvm::Value *value,
                           const std::shared_ptr<zir::Type> &type);
  void emitRetainWeakIfNeeded(llvm::Value *value,
                              const std::shared_ptr<zir::Type> &type);
  void emitReleaseWeakIfNeeded(llvm::Value *value,
                               const std::shared_ptr<zir::Type> &type);
  llvm::Value *emitWeakAlive(llvm::Value *value,
                             const std::shared_ptr<zir::Type> &type);
  llvm::Value *emitWeakLock(llvm::Value *value,
                            const std::shared_ptr<zir::Type> &type);
  void emitStoreWithArc(llvm::Value *addr, llvm::Value *value,
                        const std::shared_ptr<zir::Type> &type,
                        bool valueIsOwned, bool skipReleaseOld = false);
  void ensureClassArcSupport(const std::shared_ptr<zir::ClassType> &classType);

private:
  llvm::Function *getOrCreateRefcountFailureFunction(const char *name);
  void emitRefcountFailure(const char *name);
  void ensureNestedClassArcSupport(const std::shared_ptr<zir::Type> &type);
  void collectStrongReferenceOffsets(const std::shared_ptr<zir::Type> &type,
                                     uint64_t baseOffset,
                                     std::vector<uint32_t> &offsets);

  LLVMCodeGen &codegen_;
};
} // namespace codegen

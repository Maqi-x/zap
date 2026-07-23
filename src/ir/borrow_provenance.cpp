#include "borrow_provenance.hpp"

#include <utility>

namespace zir {
namespace {

using OwnerSet = BorrowProvenance::OwnerSet;
using OwnerMap = std::unordered_map<const Value *, OwnerSet>;
using StorageState = std::unordered_map<const Value *, OwnerSet>;
using StorageStates = std::unordered_map<const BasicBlock *, StorageState>;

const OwnerSet &emptyOwners() {
  static const OwnerSet empty;
  return empty;
}

bool tracksOwnership(const std::shared_ptr<Value> &value) {
  return value && isOwned(value->getOwnership()) &&
         containsManagedValues(value->getType());
}

bool addOwners(OwnerMap &owners, const Value *destination,
               const OwnerSet &sources) {
  if (!destination || sources.empty()) {
    return false;
  }
  auto &destinationOwners = owners[destination];
  const size_t previousSize = destinationOwners.size();
  destinationOwners.insert(sources.begin(), sources.end());
  return destinationOwners.size() != previousSize;
}

bool addOwnersFromValue(OwnerMap &owners, const Value *destination,
                        const std::shared_ptr<Value> &source) {
  if (!destination || !source) {
    return false;
  }
  const auto sourceOwners = owners.find(source.get());
  return sourceOwners != owners.end() &&
         addOwners(owners, destination, sourceOwners->second);
}

OwnerSet collectLocalStorage(const Function &function) {
  OwnerSet localStorage;
  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto &blockOwner : function.getBlocks()) {
      if (!blockOwner) {
        continue;
      }
      for (const auto &instruction : blockOwner->getInstructions()) {
        if (!instruction) {
          continue;
        }
        if (instruction->getOpCode() == OpCode::Alloca) {
          changed = localStorage
                        .insert(static_cast<const AllocaInst &>(*instruction)
                                    .getResult()
                                    .get())
                        .second ||
                    changed;
        } else if (instruction->getOpCode() == OpCode::GetElementPtr) {
          const auto &gep =
              static_cast<const GetElementPtrInst &>(*instruction);
          if (localStorage.count(gep.getPointer().get()) != 0) {
            changed =
                localStorage.insert(gep.getResult().get()).second || changed;
          }
        }
      }
    }
  }
  return localStorage;
}

OwnerMap collectValueOwners(
    const Function &function, const OwnerSet &localStorage,
    std::unordered_map<const Value *, std::shared_ptr<Value>> &derivedFrom) {
  OwnerMap owners;
  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto &blockOwner : function.getBlocks()) {
      if (!blockOwner) {
        continue;
      }
      for (const auto &instruction : blockOwner->getInstructions()) {
        if (!instruction) {
          continue;
        }
        switch (instruction->getOpCode()) {
        case OpCode::Borrow: {
          const auto &borrow = static_cast<const BorrowInst &>(*instruction);
          if (borrow.getResult() && tracksOwnership(borrow.getOwner())) {
            changed = addOwners(owners, borrow.getResult().get(),
                                {borrow.getOwner().get()}) ||
                      changed;
          }
          break;
        }
        case OpCode::Phi: {
          const auto &phi = static_cast<const PhiInst &>(*instruction);
          if (!phi.getResult()) {
            break;
          }
          for (const auto &[_, incoming] : phi.getIncoming()) {
            changed =
                addOwnersFromValue(owners, phi.getResult().get(), incoming) ||
                changed;
          }
          break;
        }
        case OpCode::Store: {
          const auto &store = static_cast<const StoreInst &>(*instruction);
          if (store.getDestination() &&
              localStorage.count(store.getDestination().get()) != 0) {
            changed = addOwnersFromValue(owners, store.getDestination().get(),
                                         store.getSource()) ||
                      changed;
          }
          break;
        }
        case OpCode::Load: {
          const auto &load = static_cast<const LoadInst &>(*instruction);
          if (load.getResult()) {
            changed = addOwnersFromValue(owners, load.getResult().get(),
                                         load.getSource()) ||
                      changed;
          }
          break;
        }
        case OpCode::Cast: {
          const auto &cast = static_cast<const CastInst &>(*instruction);
          if (cast.getResult() && cast.getSource() &&
              cast.getResult()->getType()->getIntrinsicKind() ==
                  IntrinsicTypeKind::StringView &&
              cast.getSource()->getType()->getIntrinsicKind() ==
                  IntrinsicTypeKind::StringView) {
            derivedFrom[cast.getResult().get()] = cast.getSource();
            changed = addOwnersFromValue(owners, cast.getResult().get(),
                                         cast.getSource()) ||
                      changed;
          }
          break;
        }
        default:
          break;
        }
      }
    }
  }
  return owners;
}

void unionStorageStates(StorageState &destination, const StorageState &source) {
  for (const auto &[slot, owners] : source) {
    destination[slot].insert(owners.begin(), owners.end());
  }
}

void collectEntryLoads(
    const Function &function, const OwnerSet &localStorage,
    std::unordered_map<const BasicBlock *, OwnerSet> &entryLoads) {
  for (const auto &blockOwner : function.getBlocks()) {
    if (!blockOwner) {
      continue;
    }
    OwnerSet writtenStorage;
    for (const auto &instruction : blockOwner->getInstructions()) {
      if (!instruction) {
        continue;
      }
      if (instruction->getOpCode() == OpCode::Store) {
        const auto &store = static_cast<const StoreInst &>(*instruction);
        if (store.getDestination() &&
            localStorage.count(store.getDestination().get()) != 0) {
          writtenStorage.insert(store.getDestination().get());
        }
      } else if (instruction->getOpCode() == OpCode::Load) {
        const auto &load = static_cast<const LoadInst &>(*instruction);
        if (load.getResult() && load.getSource() &&
            localStorage.count(load.getSource().get()) != 0 &&
            writtenStorage.count(load.getSource().get()) == 0) {
          entryLoads[blockOwner.get()].insert(load.getResult().get());
        }
      }
    }
  }
}

StorageStates analyzeStorageOwners(const Function &function,
                                   const ControlFlowGraph &cfg,
                                   const OwnerSet &localStorage,
                                   const OwnerMap &valueOwners,
                                   OwnerMap &loadOwners) {
  StorageStates entryStates;
  StorageStates exitStates;
  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto &blockOwner : function.getBlocks()) {
      if (!blockOwner) {
        continue;
      }
      StorageState entry;
      for (const auto *predecessor : cfg.predecessors().at(blockOwner.get())) {
        unionStorageStates(entry, exitStates[predecessor]);
      }
      if (entryStates[blockOwner.get()] != entry) {
        entryStates[blockOwner.get()] = entry;
        changed = true;
      }

      StorageState state = std::move(entry);
      for (const auto &instruction : blockOwner->getInstructions()) {
        if (!instruction) {
          continue;
        }
        if (instruction->getOpCode() == OpCode::Load) {
          const auto &load = static_cast<const LoadInst &>(*instruction);
          if (load.getResult() && load.getSource() &&
              localStorage.count(load.getSource().get()) != 0) {
            loadOwners[load.getResult().get()] = state[load.getSource().get()];
          }
          continue;
        }
        if (instruction->getOpCode() != OpCode::Store) {
          continue;
        }
        const auto &store = static_cast<const StoreInst &>(*instruction);
        if (!store.getDestination() ||
            localStorage.count(store.getDestination().get()) == 0) {
          continue;
        }
        OwnerSet owners;
        if (store.getSource()) {
          const auto ownerIt = valueOwners.find(store.getSource().get());
          if (ownerIt != valueOwners.end()) {
            owners = ownerIt->second;
          }
        }
        if (state[store.getDestination().get()] != owners) {
          state[store.getDestination().get()] = std::move(owners);
        }
      }
      if (exitStates[blockOwner.get()] != state) {
        exitStates[blockOwner.get()] = std::move(state);
        changed = true;
      }
    }
  }
  return exitStates;
}

} // namespace

const BorrowProvenance::OwnerSet &
BorrowProvenance::ownersOf(const std::shared_ptr<Value> &value) const {
  if (!value) {
    return emptyOwners();
  }
  const auto owners = owners_.find(value.get());
  return owners == owners_.end() ? emptyOwners() : owners->second;
}

BorrowProvenance::OwnerSet BorrowProvenance::ownersAtDefinition(
    const std::shared_ptr<Value> &value) const {
  OwnerSet result;
  OwnerSet visited;
  std::vector<std::shared_ptr<Value>> pending{value};
  while (!pending.empty()) {
    auto current = std::move(pending.back());
    pending.pop_back();
    if (!current || !visited.insert(current.get()).second) {
      continue;
    }
    const auto load = loadOwners_.find(current.get());
    if (load != loadOwners_.end()) {
      result.insert(load->second.begin(), load->second.end());
      continue;
    }
    const auto derived = derivedFrom_.find(current.get());
    if (derived != derivedFrom_.end()) {
      pending.push_back(derived->second);
      continue;
    }
    const auto phi = phiIncoming_.find(current.get());
    if (phi != phiIncoming_.end()) {
      pending.insert(pending.end(), phi->second.begin(), phi->second.end());
      continue;
    }
    const auto &owners = ownersOf(current);
    result.insert(owners.begin(), owners.end());
  }
  return result;
}

BorrowProvenance::OwnerSet
BorrowProvenance::ownersOnEdge(const std::shared_ptr<Value> &value,
                               const BasicBlock &source,
                               const BasicBlock &destination) const {
  std::shared_ptr<Value> current = value;
  OwnerSet visited;
  while (current && visited.insert(current.get()).second) {
    for (const auto &instruction : destination.getInstructions()) {
      if (!instruction) {
        continue;
      }
      if (instruction->getOpCode() == OpCode::Phi) {
        const auto &phi = static_cast<const PhiInst &>(*instruction);
        if (phi.getResult() != current) {
          continue;
        }
        for (const auto &[label, incoming] : phi.getIncoming()) {
          if (label == source.label) {
            return ownersOf(incoming);
          }
        }
        return {};
      }
      if (instruction->getOpCode() == OpCode::Load) {
        const auto &load = static_cast<const LoadInst &>(*instruction);
        if (load.getResult() == current &&
            isEntryLoad(destination, load.getResult())) {
          return ownersStoredAtExit(source, load.getSource());
        }
      }
    }
    const auto derived = derivedFrom_.find(current.get());
    if (derived == derivedFrom_.end()) {
      return ownersOf(current);
    }
    current = derived->second;
  }
  return {};
}

const BorrowProvenance::OwnerSet &BorrowProvenance::ownersStoredAtExit(
    const BasicBlock &block, const std::shared_ptr<Value> &storage) const {
  if (!storage) {
    return emptyOwners();
  }
  const auto blockState = exitStorage_.find(&block);
  if (blockState == exitStorage_.end()) {
    return emptyOwners();
  }
  const auto owners = blockState->second.find(storage.get());
  return owners == blockState->second.end() ? emptyOwners() : owners->second;
}

bool BorrowProvenance::isLocalStorage(
    const std::shared_ptr<Value> &value) const {
  return value && localStorage_.count(value.get()) != 0;
}

bool BorrowProvenance::isEntryLoad(const BasicBlock &block,
                                   const std::shared_ptr<Value> &result) const {
  const auto loads = entryLoads_.find(&block);
  return result && loads != entryLoads_.end() &&
         loads->second.count(result.get()) != 0;
}

BorrowProvenance analyzeBorrowProvenance(const Function &function,
                                         const ControlFlowGraph &cfg) {
  BorrowProvenance result;
  result.localStorage_ = collectLocalStorage(function);
  result.owners_ =
      collectValueOwners(function, result.localStorage_, result.derivedFrom_);
  for (const auto &blockOwner : function.getBlocks()) {
    if (!blockOwner) {
      continue;
    }
    for (const auto &instruction : blockOwner->getInstructions()) {
      if (!instruction || instruction->getOpCode() != OpCode::Phi) {
        continue;
      }
      const auto &phi = static_cast<const PhiInst &>(*instruction);
      if (!phi.getResult()) {
        continue;
      }
      auto &incoming = result.phiIncoming_[phi.getResult().get()];
      for (const auto &[_, value] : phi.getIncoming()) {
        incoming.push_back(value);
      }
    }
  }
  collectEntryLoads(function, result.localStorage_, result.entryLoads_);
  result.exitStorage_ = analyzeStorageOwners(
      function, cfg, result.localStorage_, result.owners_, result.loadOwners_);
  return result;
}

} // namespace zir

#pragma once
#include "../ir/type.hpp"
#include <memory>
#include <string>
#include <vector>

namespace sema {

enum class SymbolKind { Variable, Function, Type };

class Symbol {
public:
  std::string name;
  std::shared_ptr<zir::Type> type;
  virtual ~Symbol() noexcept = default;
  virtual SymbolKind getKind() const noexcept = 0;

protected:
  Symbol(std::string n, std::shared_ptr<zir::Type> t)
      : name(std::move(n)), type(std::move(t)) {}
};

class VariableSymbol : public Symbol {
public:
  VariableSymbol(std::string n, std::shared_ptr<zir::Type> t)
      : Symbol(std::move(n), std::move(t)) {}
  SymbolKind getKind() const noexcept override { return SymbolKind::Variable; }
};

class FunctionSymbol : public Symbol {
public:
  std::vector<std::shared_ptr<VariableSymbol>> parameters;
  std::shared_ptr<zir::Type> returnType;

  FunctionSymbol(std::string n,
                 std::vector<std::shared_ptr<VariableSymbol>> params,
                 std::shared_ptr<zir::Type> retType)
      : Symbol(std::move(n), nullptr), parameters(std::move(params)),
        returnType(std::move(retType)) {}

  SymbolKind getKind() const noexcept override { return SymbolKind::Function; }
};

class TypeSymbol : public Symbol {
public:
  TypeSymbol(std::string n, std::shared_ptr<zir::Type> t)
      : Symbol(std::move(n), std::move(t)) {}
  SymbolKind getKind() const noexcept override { return SymbolKind::Type; }
};

} // namespace sema

#include "ir/string_type.hpp"
#include "ir/type_identity.hpp"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using zir::ArrayType;
using zir::ClassType;
using zir::FunctionPointerType;
using zir::PointerType;
using zir::PrimitiveType;
using zir::RecordType;
using zir::Type;
using zir::TypeInterner;
using zir::TypeKind;

std::shared_ptr<Type> primitive(TypeKind kind) {
  return std::make_shared<PrimitiveType>(kind);
}

bool expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

bool testDisplayNamesDoNotDefineIdentity() {
  TypeInterner types;
  return expect(!types.same(primitive(TypeKind::Int),
                            primitive(TypeKind::Int32)),
                "Int and Int32 were merged because both render as i32") &&
         expect(!types.same(primitive(TypeKind::Char),
                            primitive(TypeKind::Int8)),
                "Char and Int8 were merged because both render as i8");
}

bool testStructuralTypes() {
  TypeInterner types;
  auto lhsPointer =
      std::make_shared<PointerType>(primitive(TypeKind::UInt16));
  auto rhsPointer =
      std::make_shared<PointerType>(primitive(TypeKind::UInt16));
  auto differentPointer =
      std::make_shared<PointerType>(primitive(TypeKind::Int16));

  auto lhsArray = std::make_shared<ArrayType>(lhsPointer, 4);
  auto rhsArray = std::make_shared<ArrayType>(rhsPointer, 4);
  auto differentArray = std::make_shared<ArrayType>(rhsPointer, 8);

  auto lhsFunction = std::make_shared<FunctionPointerType>(
      std::vector<std::shared_ptr<Type>>{lhsArray}, primitive(TypeKind::Bool));
  auto rhsFunction = std::make_shared<FunctionPointerType>(
      std::vector<std::shared_ptr<Type>>{rhsArray}, primitive(TypeKind::Bool));

  return expect(types.same(lhsPointer, rhsPointer),
                "equal pointer types have different identities") &&
         expect(!types.same(lhsPointer, differentPointer),
                "different pointer base types have equal identities") &&
         expect(types.same(lhsArray, rhsArray),
                "equal array types have different identities") &&
         expect(!types.same(lhsArray, differentArray),
                "array size is absent from type identity") &&
         expect(types.same(lhsFunction, rhsFunction),
                "equal function types have different identities");
}

bool testNominalAndQualifiedTypes() {
  TypeInterner types;
  auto first = std::make_shared<RecordType>("VisibleName", "module.Type");
  auto renamed = std::make_shared<RecordType>("Alias", "module.Type");
  auto other = std::make_shared<RecordType>("VisibleName", "other.Type");

  auto strong = std::make_shared<ClassType>("Node", "module.Node");
  auto weak = std::make_shared<ClassType>(*strong);
  weak->setWeak(true);

  return expect(types.same(first, renamed),
                "nominal identity does not use the declaration codegen name") &&
         expect(!types.same(first, other),
                "different nominal declarations have equal identities") &&
         expect(!types.same(strong, weak),
                "weak qualification is absent from class identity") &&
         expect(types.same(zir::makeStringType(), zir::makeStringType()),
                "intrinsic String identity is not canonical") &&
         expect(!types.same(zir::makeStringType(), zir::makeStringViewType()),
                "String and StringView have equal intrinsic identities");
}

bool testInternerDeduplicatesIdentity() {
  TypeInterner types;
  types.intern(primitive(TypeKind::UInt64));
  types.intern(primitive(TypeKind::UInt64));
  return expect(types.size() == 1,
                "interner retained duplicate canonical identities");
}

} // namespace

int main() {
  bool ok = true;
  ok = testDisplayNamesDoNotDefineIdentity() && ok;
  ok = testStructuralTypes() && ok;
  ok = testNominalAndQualifiedTypes() && ok;
  ok = testInternerDeduplicatesIdentity() && ok;
  return ok ? 0 : 1;
}

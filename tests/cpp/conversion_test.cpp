#include "ir/string_type.hpp"
#include "sema/conversion.hpp"

#include <iostream>
#include <memory>
#include <string>

namespace {

using sema::ConversionClassifier;
using sema::ConversionKind;
using sema::ConversionRank;
using zir::ClassType;
using zir::PointerType;
using zir::PrimitiveType;
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

bool testNumericConversions() {
  TypeInterner types;
  ConversionClassifier conversions(types);

  auto identity = conversions.classifyImplicit(primitive(TypeKind::Int),
                                               primitive(TypeKind::Int));
  auto widening = conversions.classifyImplicit(primitive(TypeKind::Int16),
                                               primitive(TypeKind::Int64));
  auto narrowing = conversions.classifyImplicit(primitive(TypeKind::Int64),
                                                primitive(TypeKind::Int16));
  auto signedness = conversions.classifyImplicit(primitive(TypeKind::UInt),
                                                 primitive(TypeKind::Int));
  auto intToFloat = conversions.classifyImplicit(primitive(TypeKind::Int),
                                                 primitive(TypeKind::Float64));
  auto floatToInt = conversions.classifyImplicit(primitive(TypeKind::Float),
                                                 primitive(TypeKind::Int));

  return expect(identity && identity->kind == ConversionKind::Identity &&
                    identity->rank == ConversionRank::Exact,
                "identity conversion was not classified as exact") &&
         expect(widening && widening->kind == ConversionKind::SignedWidening &&
                    widening->rank == ConversionRank::Promotion,
                "signed widening was not classified as a promotion") &&
         expect(narrowing &&
                    narrowing->kind == ConversionKind::SignedNarrowing &&
                    narrowing->rank == ConversionRank::Narrowing,
                "signed narrowing has the wrong classification") &&
         expect(signedness &&
                    signedness->kind == ConversionKind::SignednessChange &&
                    signedness->rank == ConversionRank::Lossy,
                "signedness change has the wrong classification") &&
         expect(intToFloat &&
                    intToFloat->kind == ConversionKind::IntegerToFloat,
                "integer-to-float conversion was rejected") &&
         expect(floatToInt &&
                    floatToInt->kind == ConversionKind::FloatToInteger,
                "float-to-integer conversion was rejected");
}

bool testStringAndReferenceConversions() {
  TypeInterner types;
  ConversionClassifier conversions(types);

  auto toView = conversions.classifyImplicit(zir::makeStringType(),
                                             zir::makeStringViewType());
  auto toOwned = conversions.classifyImplicit(zir::makeStringViewType(),
                                              zir::makeStringType());
  auto charPointer =
      std::make_shared<PointerType>(primitive(TypeKind::Char));
  auto stringPointer =
      conversions.classifyImplicit(zir::makeStringViewType(), charPointer);
  auto nullPointer = conversions.classifyImplicit(
      primitive(TypeKind::NullPtr), charPointer);

  auto base = std::make_shared<ClassType>("Base", "module.Base");
  auto derived = std::make_shared<ClassType>("Derived", "module.Derived");
  derived->setBase(base);
  auto weakBase = std::make_shared<ClassType>(*base);
  weakBase->setWeak(true);
  auto upcast = conversions.classifyImplicit(derived, base);
  auto toWeak = conversions.classifyImplicit(derived, weakBase);
  auto toStrong = conversions.classifyImplicit(weakBase, base);

  return expect(toView && toView->kind == ConversionKind::StringToView &&
                    toView->rank == ConversionRank::Exact,
                "String-to-view conversion is not an exact match") &&
         expect(toOwned && toOwned->kind == ConversionKind::StringToOwned,
                "view-to-owned String conversion was rejected") &&
         expect(stringPointer &&
                    stringPointer->kind == ConversionKind::StringToCharPointer,
                "String-to-Char-pointer conversion was rejected") &&
         expect(nullPointer &&
                    nullPointer->kind == ConversionKind::NullToPointer,
                "null-to-pointer conversion was rejected") &&
         expect(upcast && upcast->kind == ConversionKind::ClassUpcast,
                "derived-to-base conversion was rejected") &&
         expect(toWeak && toWeak->kind == ConversionKind::StrongToWeak,
                "strong-to-weak conversion was rejected") &&
         expect(!toStrong, "weak-to-strong conversion was accepted");
}

bool testContextSpecificConversions() {
  TypeInterner types;
  ConversionClassifier conversions(types);
  auto charPointer =
      std::make_shared<PointerType>(primitive(TypeKind::Char));

  auto implicitPointerInteger =
      conversions.classifyImplicit(charPointer, primitive(TypeKind::Int64));
  auto explicitPointerInteger =
      conversions.classifyExplicit(charPointer, primitive(TypeKind::Int64));
  auto cCharPromotion = conversions.classifyCVariadic(
      primitive(TypeKind::Char), primitive(TypeKind::Int));
  auto cBoolPromotion = conversions.classifyCVariadic(
      primitive(TypeKind::Bool), primitive(TypeKind::Int));

  return expect(!implicitPointerInteger,
                "pointer-to-integer conversion became implicit") &&
         expect(explicitPointerInteger &&
                    explicitPointerInteger->kind ==
                        ConversionKind::ExplicitPointerInteger,
                "explicit pointer-to-integer conversion was rejected") &&
         expect(cCharPromotion &&
                    cCharPromotion->kind ==
                        ConversionKind::CVariadicPromotion,
                "C variadic Char promotion was rejected") &&
         expect(cBoolPromotion &&
                    cBoolPromotion->kind ==
                        ConversionKind::CVariadicPromotion,
                "C variadic Bool promotion was rejected");
}

bool testCachedConversionUsesRequestedTargetObject() {
  TypeInterner types;
  ConversionClassifier conversions(types);
  auto source = primitive(TypeKind::Int16);
  auto firstTarget = primitive(TypeKind::Int64);
  auto secondTarget = primitive(TypeKind::Int64);

  auto first = conversions.classifyImplicit(source, firstTarget);
  auto second = conversions.classifyImplicit(source, secondTarget);
  return expect(first && first->targetType == firstTarget,
                "uncached conversion lost its requested target object") &&
         expect(second && second->targetType == secondTarget,
                "cached conversion reused a stale target object");
}

} // namespace

int main() {
  bool ok = true;
  ok = testNumericConversions() && ok;
  ok = testStringAndReferenceConversions() && ok;
  ok = testContextSpecificConversions() && ok;
  ok = testCachedConversionUsesRequestedTargetObject() && ok;
  return ok ? 0 : 1;
}

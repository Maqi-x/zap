#include "conversion.hpp"

#include "../ir/failable_type.hpp"
#include "../ir/string_type.hpp"

namespace sema {
namespace {

bool isSignedInteger(const std::shared_ptr<zir::Type> &type) {
  if (!type) {
    return false;
  }
  switch (type->getKind()) {
  case zir::TypeKind::Int8:
  case zir::TypeKind::Int16:
  case zir::TypeKind::Int32:
  case zir::TypeKind::Int64:
  case zir::TypeKind::Int:
    return true;
  default:
    return false;
  }
}

int bitWidth(const std::shared_ptr<zir::Type> &type) {
  if (!type) {
    return 0;
  }
  switch (type->getKind()) {
  case zir::TypeKind::Bool:
    return 1;
  case zir::TypeKind::Char:
  case zir::TypeKind::Int8:
  case zir::TypeKind::UInt8:
    return 8;
  case zir::TypeKind::Int16:
  case zir::TypeKind::UInt16:
    return 16;
  case zir::TypeKind::Int32:
  case zir::TypeKind::UInt32:
  case zir::TypeKind::Int:
  case zir::TypeKind::UInt:
  case zir::TypeKind::Float:
  case zir::TypeKind::Float32:
    return 32;
  case zir::TypeKind::Int64:
  case zir::TypeKind::UInt64:
  case zir::TypeKind::Float64:
    return 64;
  default:
    return 0;
  }
}

Conversion conversion(ConversionKind kind, ConversionRank rank,
                      const std::shared_ptr<zir::Type> &target) {
  return {kind, rank, target};
}

} // namespace

std::string_view Conversion::description() const {
  switch (kind) {
  case ConversionKind::Identity:
    return "exact match";
  case ConversionKind::StringToView:
    return "string-to-view conversion";
  case ConversionKind::StringToOwned:
    return "view-to-owned-string conversion";
  case ConversionKind::EnumToInteger:
    return "enum-to-integer conversion";
  case ConversionKind::FloatingWidening:
    return "floating widening";
  case ConversionKind::FloatingNarrowing:
    return "floating narrowing";
  case ConversionKind::SignedWidening:
    return "signed widening";
  case ConversionKind::SignedNarrowing:
    return "signed narrowing";
  case ConversionKind::UnsignedWidening:
    return "unsigned widening";
  case ConversionKind::UnsignedNarrowing:
    return "unsigned narrowing";
  case ConversionKind::SignednessChange:
    return "signedness-changing integer conversion";
  case ConversionKind::IntegerToFloat:
    return "integer-to-float conversion";
  case ConversionKind::FloatToInteger:
    return "float-to-integer conversion";
  case ConversionKind::NullToPointer:
  case ConversionKind::NullToClass:
    return "null-to-reference conversion";
  case ConversionKind::StringToCharPointer:
    return "string-to-character-pointer conversion";
  case ConversionKind::ClassUpcast:
    return "derived-to-base conversion";
  case ConversionKind::StrongToWeak:
    return "strong-to-weak conversion";
  case ConversionKind::ArrayToVariadicView:
    return "array-to-variadic-view conversion";
  case ConversionKind::Failable:
    return "failable conversion";
  case ConversionKind::CVariadicPromotion:
    return "C variadic promotion";
  case ConversionKind::ExplicitCharInteger:
    return "explicit character/integer conversion";
  case ConversionKind::ExplicitPointer:
    return "explicit pointer conversion";
  case ConversionKind::ExplicitStringPointer:
    return "explicit string-to-pointer conversion";
  case ConversionKind::ExplicitPointerString:
    return "explicit pointer-to-string conversion";
  case ConversionKind::ExplicitPointerInteger:
    return "explicit pointer/integer conversion";
  }
  return "conversion";
}

size_t ConversionClassifier::TypePairHash::operator()(
    const TypePair &pair) const noexcept {
  size_t result = zir::TypeIdHash{}(pair.source);
  result ^= zir::TypeIdHash{}(pair.target) + 0x9e3779b9U +
            (result << 6U) + (result >> 2U);
  return result;
}

bool isVariadicViewType(const std::shared_ptr<zir::Type> &type) {
  return type && type->getKind() == zir::TypeKind::Record &&
         static_cast<const zir::RecordType &>(*type).getName().rfind(
             "__zap_varargs_", 0) == 0;
}

std::optional<Conversion> ConversionClassifier::classifyImplicit(
    const std::shared_ptr<zir::Type> &source,
    const std::shared_ptr<zir::Type> &target) const {
  if (!source || !target) {
    return std::nullopt;
  }
  const TypePair key{types_.intern(source), types_.intern(target)};
  if (auto cached = implicitCache_.find(key); cached != implicitCache_.end()) {
    auto result = cached->second;
    if (result) {
      result->targetType = target;
    }
    return result;
  }
  auto result = classifyImplicitUncached(source, target);
  implicitCache_.emplace(key, result);
  return result;
}

std::optional<Conversion> ConversionClassifier::classifyImplicitUncached(
    const std::shared_ptr<zir::Type> &source,
    const std::shared_ptr<zir::Type> &target) const {
  if (types_.same(source, target)) {
    return conversion(ConversionKind::Identity, ConversionRank::Exact, target);
  }

  if (source->getKind() == zir::TypeKind::NullPtr) {
    if (target->getKind() == zir::TypeKind::Pointer) {
      return conversion(ConversionKind::NullToPointer,
                        ConversionRank::NullPointer, target);
    }
    if (target->getKind() == zir::TypeKind::Class) {
      return conversion(ConversionKind::NullToClass,
                        ConversionRank::Structural, target);
    }
  }

  if (zir::isIntrinsicStringType(source) &&
      zir::isIntrinsicStringType(target)) {
    if (zir::isIntrinsicStringViewType(target)) {
      return conversion(ConversionKind::StringToView, ConversionRank::Exact,
                        target);
    }
    return conversion(ConversionKind::StringToOwned, ConversionRank::Promotion,
                      target);
  }

  if (zir::isIntrinsicStringType(source) &&
      target->getKind() == zir::TypeKind::Pointer) {
    const auto &pointer = static_cast<const zir::PointerType &>(*target);
    if (pointer.getBaseType()->getKind() == zir::TypeKind::Char) {
      return conversion(ConversionKind::StringToCharPointer,
                        ConversionRank::Structural, target);
    }
  }

  if (source->getKind() == zir::TypeKind::Class &&
      target->getKind() == zir::TypeKind::Class) {
    auto sourceClass = std::static_pointer_cast<zir::ClassType>(source);
    auto targetClass = std::static_pointer_cast<zir::ClassType>(target);
    if (sourceClass->isWeak() && !targetClass->isWeak()) {
      return std::nullopt;
    }
    for (auto current = sourceClass; current; current = current->getBase()) {
      if (current->getCodegenName() == targetClass->getCodegenName()) {
        auto kind = !sourceClass->isWeak() && targetClass->isWeak()
                        ? ConversionKind::StrongToWeak
                        : ConversionKind::ClassUpcast;
        return conversion(kind, ConversionRank::Structural, target);
      }
    }
  }

  if (source->getKind() == zir::TypeKind::Array &&
      isVariadicViewType(target)) {
    const auto &array = static_cast<const zir::ArrayType &>(*source);
    const auto &view = static_cast<const zir::RecordType &>(*target);
    if (!view.getFields().empty() && view.getFields()[0].type &&
        view.getFields()[0].type->getKind() == zir::TypeKind::Pointer) {
      const auto &data =
          static_cast<const zir::PointerType &>(*view.getFields()[0].type);
      if (types_.same(array.getBaseType(), data.getBaseType())) {
        return conversion(ConversionKind::ArrayToVariadicView,
                          ConversionRank::Structural, target);
      }
    }
  }

  auto sourceFailable = zir::getFailableTypeLayout(source);
  auto targetFailable = zir::getFailableTypeLayout(target);
  if (sourceFailable && targetFailable &&
      classifyImplicit(sourceFailable->valueType, targetFailable->valueType) &&
      classifyImplicit(sourceFailable->errorType, targetFailable->errorType)) {
    return conversion(ConversionKind::Failable, ConversionRank::Structural,
                      target);
  }

  if (source->getKind() == zir::TypeKind::Enum &&
      target->getKind() == zir::TypeKind::Int) {
    return conversion(ConversionKind::EnumToInteger, ConversionRank::Promotion,
                      target);
  }

  if (source->isFloatingPoint() && target->isFloatingPoint()) {
    bool widening = bitWidth(target) >= bitWidth(source);
    return conversion(widening ? ConversionKind::FloatingWidening
                               : ConversionKind::FloatingNarrowing,
                      widening ? ConversionRank::Promotion
                               : ConversionRank::Lossy,
                      target);
  }

  if (isSignedInteger(source) && isSignedInteger(target)) {
    bool widening = bitWidth(target) >= bitWidth(source);
    return conversion(widening ? ConversionKind::SignedWidening
                               : ConversionKind::SignedNarrowing,
                      widening ? ConversionRank::Promotion
                               : ConversionRank::Narrowing,
                      target);
  }

  if (source->isInteger() && source->isUnsigned() && target->isInteger() &&
      target->isUnsigned()) {
    bool widening = bitWidth(target) >= bitWidth(source);
    return conversion(widening ? ConversionKind::UnsignedWidening
                               : ConversionKind::UnsignedNarrowing,
                      widening ? ConversionRank::Promotion
                               : ConversionRank::Narrowing,
                      target);
  }

  if (source->isInteger() && target->isFloatingPoint()) {
    bool preservesWidth = bitWidth(target) >= bitWidth(source);
    return conversion(ConversionKind::IntegerToFloat,
                      preservesWidth ? ConversionRank::Numeric
                                     : ConversionRank::Lossy,
                      target);
  }

  if (source->isFloatingPoint() && target->isInteger()) {
    return conversion(ConversionKind::FloatToInteger,
                      ConversionRank::FloatToInteger, target);
  }

  if (source->isInteger() && target->isInteger()) {
    return conversion(ConversionKind::SignednessChange, ConversionRank::Lossy,
                      target);
  }

  return std::nullopt;
}

std::optional<Conversion> ConversionClassifier::classifyExplicit(
    const std::shared_ptr<zir::Type> &source,
    const std::shared_ptr<zir::Type> &target) const {
  if (auto implicit = classifyImplicit(source, target)) {
    return implicit;
  }
  if (!source || !target) {
    return std::nullopt;
  }

  bool sourceChar = source->getKind() == zir::TypeKind::Char;
  bool targetChar = target->getKind() == zir::TypeKind::Char;
  if ((sourceChar && target->isInteger()) ||
      (source->isInteger() && targetChar)) {
    return conversion(ConversionKind::ExplicitCharInteger,
                      ConversionRank::Explicit, target);
  }
  if (source->getKind() == zir::TypeKind::Enum && target->isInteger()) {
    return conversion(ConversionKind::EnumToInteger, ConversionRank::Explicit,
                      target);
  }

  bool sourcePointer = source->getKind() == zir::TypeKind::Pointer;
  bool targetPointer = target->getKind() == zir::TypeKind::Pointer;
  if ((sourcePointer || source->getKind() == zir::TypeKind::NullPtr) &&
      targetPointer) {
    return conversion(ConversionKind::ExplicitPointer,
                      ConversionRank::Explicit, target);
  }
  if (zir::isIntrinsicStringType(source) && targetPointer) {
    return conversion(ConversionKind::ExplicitStringPointer,
                      ConversionRank::Explicit, target);
  }
  if (sourcePointer && zir::isIntrinsicStringType(target)) {
    const auto &pointer = static_cast<const zir::PointerType &>(*source);
    auto baseKind = pointer.getBaseType()->getKind();
    if (baseKind == zir::TypeKind::Void || baseKind == zir::TypeKind::Char) {
      return conversion(ConversionKind::ExplicitPointerString,
                        ConversionRank::Explicit, target);
    }
  }
  if ((sourcePointer && target->isInteger()) ||
      (source->isInteger() && targetPointer)) {
    return conversion(ConversionKind::ExplicitPointerInteger,
                      ConversionRank::Explicit, target);
  }
  return std::nullopt;
}

std::optional<Conversion> ConversionClassifier::classifyCVariadic(
    const std::shared_ptr<zir::Type> &source,
    const std::shared_ptr<zir::Type> &target) const {
  if (auto implicit = classifyImplicit(source, target)) {
    return implicit;
  }
  if (!source || !target) {
    return std::nullopt;
  }
  auto sourceKind = source->getKind();
  if (target->getKind() == zir::TypeKind::Int &&
      (sourceKind == zir::TypeKind::Bool ||
       sourceKind == zir::TypeKind::Char)) {
    return conversion(ConversionKind::CVariadicPromotion,
                      ConversionRank::Promotion, target);
  }
  return std::nullopt;
}

} // namespace sema

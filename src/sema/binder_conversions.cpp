#include "binder.hpp"

#include <algorithm>

namespace sema {

bool Binder::isSignedIntegerType(std::shared_ptr<zir::Type> type) const {
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

bool Binder::isUnsignedIntegerType(std::shared_ptr<zir::Type> type) const {
  return type && type->isInteger() && type->isUnsigned();
}

int Binder::typeBitWidth(std::shared_ptr<zir::Type> type) const {
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

bool Binder::isNumeric(std::shared_ptr<zir::Type> type) const {
  return type->isInteger() || type->isFloatingPoint();
}

bool Binder::isPointerType(std::shared_ptr<zir::Type> type) const {
  return type && type->getKind() == zir::TypeKind::Pointer;
}

bool Binder::isNullType(std::shared_ptr<zir::Type> type) const {
  return type && type->getKind() == zir::TypeKind::NullPtr;
}

bool Binder::isUnsafeActive() const {
  return unsafeDepth_ > 0 || unsafeTypeContextDepth_ > 0;
}

void Binder::requireUnsafeEnabled(SourceSpan span, const std::string &feature) {
  if (!allowUnsafe_) {
    error(span, "Using " + feature + " requires '--allow-unsafe'.");
  }
}

void Binder::requireUnsafeContext(SourceSpan span, const std::string &feature) {
  if (!isUnsafeActive()) {
    error(span, "Using " + feature + " is only allowed inside unsafe code.");
  }
}

std::shared_ptr<zir::Type>
Binder::getPromotedType(std::shared_ptr<zir::Type> t1,
                        std::shared_ptr<zir::Type> t2) {
  if (typeInterner_.same(t1, t2))
    return t1;

  if (t1->isFloatingPoint() || t2->isFloatingPoint()) {
    if (t1->getKind() == zir::TypeKind::Float64 ||
        t2->getKind() == zir::TypeKind::Float64) {
      return std::make_shared<zir::PrimitiveType>(zir::TypeKind::Float64);
    }
    return std::make_shared<zir::PrimitiveType>(zir::TypeKind::Float);
  }

  // Integer promotion: preserve unsignedness when both operands are unsigned,
  // and choose width-aware integer kinds instead of always forcing Int64.
  if (isUnsignedIntegerType(t1) && isUnsignedIntegerType(t2)) {
    int width = std::max(typeBitWidth(t1), typeBitWidth(t2));
    if (width <= 8)
      return std::make_shared<zir::PrimitiveType>(zir::TypeKind::UInt8);
    if (width <= 16)
      return std::make_shared<zir::PrimitiveType>(zir::TypeKind::UInt16);
    if (width <= 32)
      return std::make_shared<zir::PrimitiveType>(zir::TypeKind::UInt);
    return std::make_shared<zir::PrimitiveType>(zir::TypeKind::UInt64);
  }

  // Mixed signed/unsigned or both signed:
  // - preserve unsignedness when the unsigned operand width is >= signed width
  //   (e.g. Int + UInt -> UInt, Int16 + UInt16 -> UInt16, Int + UInt64 ->
  //   UInt64)
  // - otherwise use signed, width-aware promotion.
  if ((isUnsignedIntegerType(t1) && isSignedIntegerType(t2)) ||
      (isSignedIntegerType(t1) && isUnsignedIntegerType(t2))) {
    auto unsignedType = isUnsignedIntegerType(t1) ? t1 : t2;
    auto signedType = isUnsignedIntegerType(t1) ? t2 : t1;

    int unsignedWidth = typeBitWidth(unsignedType);
    int signedWidth = typeBitWidth(signedType);

    if (unsignedWidth >= signedWidth) {
      if (unsignedWidth <= 8)
        return std::make_shared<zir::PrimitiveType>(zir::TypeKind::UInt8);
      if (unsignedWidth <= 16)
        return std::make_shared<zir::PrimitiveType>(zir::TypeKind::UInt16);
      if (unsignedWidth <= 32)
        return std::make_shared<zir::PrimitiveType>(zir::TypeKind::UInt);
      return std::make_shared<zir::PrimitiveType>(zir::TypeKind::UInt64);
    }

    int width = std::max(unsignedWidth, signedWidth);
    if (width <= 8)
      return std::make_shared<zir::PrimitiveType>(zir::TypeKind::Int8);
    if (width <= 16)
      return std::make_shared<zir::PrimitiveType>(zir::TypeKind::Int16);
    if (width <= 32)
      return std::make_shared<zir::PrimitiveType>(zir::TypeKind::Int);
    return std::make_shared<zir::PrimitiveType>(zir::TypeKind::Int64);
  }

  // Both signed: keep signed result, width-aware.
  int width = std::max(typeBitWidth(t1), typeBitWidth(t2));
  if (width <= 8)
    return std::make_shared<zir::PrimitiveType>(zir::TypeKind::Int8);
  if (width <= 16)
    return std::make_shared<zir::PrimitiveType>(zir::TypeKind::Int16);
  if (width <= 32)
    return std::make_shared<zir::PrimitiveType>(zir::TypeKind::Int);
  return std::make_shared<zir::PrimitiveType>(zir::TypeKind::Int64);
}

std::shared_ptr<zir::Type>
Binder::getCVariadicArgumentType(std::shared_ptr<zir::Type> type) {
  if (!type)
    return nullptr;

  if (isStringType(type)) {
    return std::make_shared<zir::PointerType>(
        std::make_shared<zir::PrimitiveType>(zir::TypeKind::Char));
  }

  if (isPointerType(type))
    return type;

  switch (type->getKind()) {
  case zir::TypeKind::Float:
  case zir::TypeKind::Float32:
    return std::make_shared<zir::PrimitiveType>(zir::TypeKind::Float64);
  case zir::TypeKind::Bool:
  case zir::TypeKind::Char:
  case zir::TypeKind::Int8:
  case zir::TypeKind::Int16:
  case zir::TypeKind::UInt8:
  case zir::TypeKind::UInt16:
    return std::make_shared<zir::PrimitiveType>(zir::TypeKind::Int);
  case zir::TypeKind::Int32:
  case zir::TypeKind::Int64:
  case zir::TypeKind::UInt32:
  case zir::TypeKind::UInt64:
  case zir::TypeKind::Int:
  case zir::TypeKind::UInt:
  case zir::TypeKind::Float64:
    return type;
  default:
    return nullptr;
  }
}

std::unique_ptr<BoundExpression>
Binder::applyConversion(std::unique_ptr<BoundExpression> expr,
                        const Conversion &conversion) {
  if (!expr || !conversion.targetType)
    return expr;
  if (!conversion.requiresCast()) {
    return expr;
  }
  return std::make_unique<BoundCast>(std::move(expr), conversion.targetType);
}

} // namespace sema

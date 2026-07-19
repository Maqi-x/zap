#include "../ast/class_decl.hpp"
#include "../ast/const/const_char.hpp"
#include "../ast/record_decl.hpp"
#include "../ir/string_type.hpp"
#include "binder.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

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

int Binder::conversionCost(std::shared_ptr<zir::Type> from,
                           std::shared_ptr<zir::Type> to) const {
  if (!from || !to) {
    return 1000;
  }
  if (from->toString() == to->toString()) {
    return 0;
  }
  if (isNullType(from) && isPointerType(to)) {
    return 3;
  }
  if (!canConvert(from, to)) {
    return 1000;
  }
  if (isStringType(from) && isStringType(to)) {
    return zir::isIntrinsicStringViewType(to) ? 0 : 1;
  }

  if (from->getKind() == zir::TypeKind::Enum && to->isInteger()) {
    return 1;
  }

  if (from->isFloatingPoint() && to->isFloatingPoint()) {
    return typeBitWidth(to) >= typeBitWidth(from) ? 1 : 5;
  }

  if (isSignedIntegerType(from) && isSignedIntegerType(to)) {
    return typeBitWidth(to) >= typeBitWidth(from) ? 1 : 4;
  }

  if (isUnsignedIntegerType(from) && isUnsignedIntegerType(to)) {
    return typeBitWidth(to) >= typeBitWidth(from) ? 1 : 4;
  }

  if (from->isInteger() && to->isFloatingPoint()) {
    return typeBitWidth(to) >= typeBitWidth(from) ? 2 : 5;
  }

  if (from->isFloatingPoint() && to->isInteger()) {
    return 6;
  }

  if (from->isInteger() && to->isInteger()) {
    return 5;
  }

  return 7;
}

std::string Binder::describeConversion(std::shared_ptr<zir::Type> from,
                                       std::shared_ptr<zir::Type> to) const {
  if (!from || !to) {
    return "invalid conversion";
  }
  int cost = conversionCost(from, to);
  if (cost == 0) {
    return "exact match";
  }
  if (isNullType(from) &&
      (isPointerType(to) || to->getKind() == zir::TypeKind::Class)) {
    return "null-to-reference conversion";
  }
  if (from->isFloatingPoint() && to->isFloatingPoint()) {
    return typeBitWidth(to) >= typeBitWidth(from) ? "floating widening"
                                                  : "floating narrowing";
  }
  if (isSignedIntegerType(from) && isSignedIntegerType(to)) {
    return typeBitWidth(to) >= typeBitWidth(from) ? "signed widening"
                                                  : "signed narrowing";
  }
  if (isUnsignedIntegerType(from) && isUnsignedIntegerType(to)) {
    return typeBitWidth(to) >= typeBitWidth(from) ? "unsigned widening"
                                                  : "unsigned narrowing";
  }
  if (from->isInteger() && to->isFloatingPoint()) {
    return "integer-to-float conversion";
  }
  if (from->isFloatingPoint() && to->isInteger()) {
    return "float-to-integer conversion";
  }
  if (from->isInteger() && to->isInteger()) {
    return "signedness-changing integer conversion";
  }
  return cost >= 1000 ? "not convertible" : "implicit conversion";
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

bool Binder::canConvert(std::shared_ptr<zir::Type> from,
                        std::shared_ptr<zir::Type> to) const {
  if (!from || !to)
    return false;
  if (from->getKind() == to->getKind() && from->toString() == to->toString())
    return true;

  auto key = std::make_pair(from.get(), to.get());
  auto cacheIt = canConvertCache_.find(key);
  if (cacheIt != canConvertCache_.end())
    return cacheIt->second;

  auto &cached = canConvertCache_[key];
  if (isNullType(from) &&
      (isPointerType(to) || to->getKind() == zir::TypeKind::Class))
    return cached = true;
  if (isStringType(from) && isStringType(to))
    return cached = true;
  if (isStringType(from) && isPointerType(to)) {
    auto ptrType = std::static_pointer_cast<zir::PointerType>(to);
    return cached = (ptrType &&
                     ptrType->getBaseType()->getKind() == zir::TypeKind::Char);
  }
  if (from->getKind() == zir::TypeKind::Class &&
      to->getKind() == zir::TypeKind::Class) {
    auto fromClass = std::static_pointer_cast<zir::ClassType>(from);
    auto toClass = std::static_pointer_cast<zir::ClassType>(to);
    if (fromClass->isWeak() && !toClass->isWeak())
      return cached = false;
    for (auto current = fromClass; current; current = current->getBase()) {
      if (current->getName() == toClass->getName())
        return cached = true;
    }
  }
  if (from->getKind() == zir::TypeKind::Array && isVariadicViewType(to)) {
    auto arrayType = std::static_pointer_cast<zir::ArrayType>(from);
    auto viewType = std::static_pointer_cast<zir::RecordType>(to);
    if (!viewType->getFields().empty() &&
        viewType->getFields()[0].type->getKind() == zir::TypeKind::Pointer) {
      auto dataType = std::static_pointer_cast<zir::PointerType>(
          viewType->getFields()[0].type);
      return cached = (arrayType->getBaseType()->toString() ==
                       dataType->getBaseType()->toString());
    }
  }
  if (isFailableType(from) && isFailableType(to)) {
    auto fromValueType = failableValueType(from);
    auto fromErrorType = failableErrorType(from);
    auto toValueType = failableValueType(to);
    auto toErrorType = failableErrorType(to);
    return cached = (canConvert(fromValueType, toValueType) &&
                     canConvert(fromErrorType, toErrorType));
  }
  if (isNumeric(from) && isNumeric(to))
    return cached = true;
  if (from->getKind() == zir::TypeKind::Enum &&
      to->getKind() == zir::TypeKind::Int)
    return cached = true;
  return cached = false;
}

std::shared_ptr<zir::Type>
Binder::getPromotedType(std::shared_ptr<zir::Type> t1,
                        std::shared_ptr<zir::Type> t2) {
  if (t1->toString() == t2->toString())
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
Binder::wrapInCast(std::unique_ptr<BoundExpression> expr,
                   std::shared_ptr<zir::Type> targetType) {
  if (!expr || !targetType)
    return expr;
  if (expr->type->getKind() == targetType->getKind() &&
      expr->type->toString() == targetType->toString()) {
    return expr;
  }
  if (isNullType(expr->type) && isPointerType(targetType)) {
    return std::make_unique<BoundCast>(std::move(expr), targetType);
  }
  return std::make_unique<BoundCast>(std::move(expr), targetType);
}

} // namespace sema

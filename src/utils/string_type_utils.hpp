#pragma once

#include "../ir/type.hpp"
#include <memory>
#include <string>

namespace zap::text {

enum class StringKind {
  NotString,
  String,
  StringView,
};

inline std::string baseTypeName(const std::string &full) {
  auto dot = full.find_last_of('.');
  return dot == std::string::npos ? full : full.substr(dot + 1);
}

inline StringKind classifyRecordName(const std::string &full) {
  const auto base = baseTypeName(full);
  if (base == "String") {
    return StringKind::String;
  }
  if (base == "StringView") {
    return StringKind::StringView;
  }
  return StringKind::NotString;
}

inline bool isStringRecordName(const std::string &full) {
  return classifyRecordName(full) != StringKind::NotString;
}

inline bool isStringType(const std::shared_ptr<zir::Type> &type) {
  if (!type) {
    return false;
  }

  if (type->getKind() == zir::TypeKind::Record) {
    return isStringRecordName(
        static_cast<zir::RecordType *>(type.get())->getName());
  }

  if (type->getKind() == zir::TypeKind::Class) {
    return isStringRecordName(
        static_cast<zir::ClassType *>(type.get())->getName());
  }

  return false;
}

inline bool isStringViewType(const std::shared_ptr<zir::Type> &type) {
  if (!type || type->getKind() != zir::TypeKind::Record) {
    return false;
  }

  return classifyRecordName(
             static_cast<zir::RecordType *>(type.get())->getName()) ==
         StringKind::StringView;
}

} // namespace zap::text

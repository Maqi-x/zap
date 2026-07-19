#pragma once

#include "../runtime/string_layout.h"

namespace codegen {

constexpr unsigned kStringRefCountIndex = ZAP_STRING_REFCOUNT_INDEX;
constexpr unsigned kStringLengthIndex = ZAP_STRING_LENGTH_INDEX;
constexpr unsigned kStringDataIndex = ZAP_STRING_DATA_INDEX;
constexpr int64_t kStringImmortalRefCount = ZAP_STRING_IMMORTAL_REFCOUNT;

} // namespace codegen

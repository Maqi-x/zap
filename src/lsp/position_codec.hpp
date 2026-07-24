#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace zap::lsp {

struct LspPosition {
  int64_t line = 0;
  int64_t character = 0;
};

size_t offsetFromPosition(std::string_view text, int64_t line,
                          int64_t character);
LspPosition positionFromOffset(std::string_view text, size_t offset);

} // namespace zap::lsp

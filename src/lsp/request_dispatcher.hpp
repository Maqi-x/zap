#pragma once

#include <string_view>

namespace zap::lsp {

inline constexpr std::string_view lspVersion = "0.1.0";

int runRequestDispatcher();

} // namespace zap::lsp

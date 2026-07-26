#include "lsp/position_codec.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

} // namespace

int main() {
  const std::string text = u8"aą😀b\n🚀x";

  require(zap::lsp::offsetFromPosition(text, 0, 0) == 0,
          "ASCII line start was not decoded");
  require(zap::lsp::offsetFromPosition(text, 0, 1) == 1,
          "ASCII character was not decoded");
  require(zap::lsp::offsetFromPosition(text, 0, 2) == 3,
          "BMP character must consume one UTF-16 code unit");
  require(zap::lsp::offsetFromPosition(text, 0, 4) == 7,
          "astral character must consume two UTF-16 code units");
  require(zap::lsp::offsetFromPosition(text, 0, 5) == 8,
          "cursor after astral character was decoded incorrectly");
  require(zap::lsp::offsetFromPosition(text, 1, 3) == text.size(),
          "second-line astral character was decoded incorrectly");

  const auto emojiStart = zap::lsp::positionFromOffset(text, 3);
  require(emojiStart.line == 0 && emojiStart.character == 2,
          "emoji start was not encoded as UTF-16");
  const auto afterEmoji = zap::lsp::positionFromOffset(text, 7);
  require(afterEmoji.line == 0 && afterEmoji.character == 4,
          "emoji end was not encoded as UTF-16");
  const auto secondLine = zap::lsp::positionFromOffset(text, text.size());
  require(secondLine.line == 1 && secondLine.character == 3,
          "second line position was not encoded as UTF-16");

  const std::string crlfText = u8"a\r\n😀";
  require(zap::lsp::offsetFromPosition(crlfText, 0, 1) == 1,
          "CRLF line end should stop before carriage return");
  require(zap::lsp::offsetFromPosition(crlfText, 1, 2) == crlfText.size(),
          "CRLF second line was decoded incorrectly");
  const auto crlfEnd = zap::lsp::positionFromOffset(crlfText, crlfText.size());
  require(crlfEnd.line == 1 && crlfEnd.character == 2,
          "CRLF line break was not encoded as one line transition");
}

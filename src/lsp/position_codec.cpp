#include "lsp/position_codec.hpp"

#include <algorithm>

namespace zap::lsp {

namespace {

bool isContinuationByte(unsigned char byte) { return (byte & 0xC0) == 0x80; }

size_t utf8Length(std::string_view text, size_t offset) {
  const auto lead = static_cast<unsigned char>(text[offset]);
  const size_t remaining = text.size() - offset;
  if (lead < 0x80) {
    return 1;
  }
  if (lead >= 0xC2 && lead <= 0xDF && remaining >= 2 &&
      isContinuationByte(static_cast<unsigned char>(text[offset + 1]))) {
    return 2;
  }
  if (lead >= 0xE0 && lead <= 0xEF && remaining >= 3 &&
      isContinuationByte(static_cast<unsigned char>(text[offset + 1])) &&
      isContinuationByte(static_cast<unsigned char>(text[offset + 2]))) {
    return 3;
  }
  if (lead >= 0xF0 && lead <= 0xF4 && remaining >= 4 &&
      isContinuationByte(static_cast<unsigned char>(text[offset + 1])) &&
      isContinuationByte(static_cast<unsigned char>(text[offset + 2])) &&
      isContinuationByte(static_cast<unsigned char>(text[offset + 3]))) {
    return 4;
  }
  return 1;
}

int64_t utf16Units(size_t utf8Bytes) { return utf8Bytes == 4 ? 2 : 1; }

size_t lineStartOffset(std::string_view text, int64_t line) {
  if (line <= 0) {
    return 0;
  }

  size_t offset = 0;
  int64_t currentLine = 0;
  while (offset < text.size() && currentLine < line) {
    if (text[offset++] == '\n') {
      ++currentLine;
    }
  }
  return offset;
}

} // namespace

size_t offsetFromPosition(std::string_view text, int64_t line,
                          int64_t character) {
  size_t offset = lineStartOffset(text, line);
  if (character <= 0) {
    return offset;
  }

  int64_t units = 0;
  while (offset < text.size() && text[offset] != '\n' && text[offset] != '\r') {
    const size_t length = utf8Length(text, offset);
    const int64_t nextUnits = units + utf16Units(length);
    if (nextUnits > character) {
      return offset;
    }
    offset += length;
    units = nextUnits;
    if (units == character) {
      return offset;
    }
  }
  return offset;
}

LspPosition positionFromOffset(std::string_view text, size_t offset) {
  offset = std::min(offset, text.size());
  LspPosition position;
  size_t cursor = 0;
  while (cursor < offset) {
    if (text[cursor] == '\r' && cursor + 1 < text.size() &&
        text[cursor + 1] == '\n') {
      if (cursor + 2 > offset) {
        break;
      }
      ++position.line;
      position.character = 0;
      cursor += 2;
      continue;
    }
    if (text[cursor] == '\n') {
      ++position.line;
      position.character = 0;
      ++cursor;
      continue;
    }

    const size_t length = utf8Length(text, cursor);
    if (cursor + length > offset) {
      break;
    }
    position.character += utf16Units(length);
    cursor += length;
  }
  return position;
}

} // namespace zap::lsp

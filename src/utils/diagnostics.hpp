#pragma once
#include <iostream>
#include <string>
#include <vector>
#include "../token/token.hpp"

namespace zap {

enum class DiagnosticLevel {
  Note,
  Warning,
  Error
};

class DiagnosticEngine {
private:
  const std::string& source;
  std::string fileName;
  size_t errorCount = 0;

public:
  DiagnosticEngine(const std::string& src, const std::string& fname = "input") 
    : source(src), fileName(fname) {}

  void report(SourceSpan span, DiagnosticLevel level, const std::string& message) {
    if (level == DiagnosticLevel::Error) {
      errorCount++;
    }

    std::string levelStr;
    switch (level) {
      case DiagnosticLevel::Note: levelStr = "\033[1;34mnote\033[0m"; break;
      case DiagnosticLevel::Warning: levelStr = "\033[1;33mwarning\033[0m"; break;
      case DiagnosticLevel::Error: levelStr = "\033[1;31merror\033[0m"; break;
    }

    std::cerr << levelStr << ": " << message << std::endl;
    std::cerr << " --> " << fileName << ":" << span.line << ":" << span.column << std::endl;

    printContext(span);
  }

  bool hadErrors() const {
    return errorCount > 0;
  }

private:
  void printContext(SourceSpan span) {
    size_t lineStart = 0;
    size_t line = 1;
    size_t i = 0;
    while (i < source.length() && line < span.line) {
      if (source[i] == '\n') {
        line++;
        lineStart = i + 1;
      }
      i++;
    }
    
    size_t lineEnd = lineStart;
    while (lineEnd < source.length() && source[lineEnd] != '\n') {
      lineEnd++;
    }

    std::string lineContent = source.substr(lineStart, lineEnd - lineStart);
    
    std::string lineNumStr = std::to_string(span.line);
    std::cerr << " " << lineNumStr << " | " << lineContent << "\n";
    
    size_t prefixLen = lineNumStr.length() + 4; 
    for (size_t j = 0; j < prefixLen; ++j) std::cerr << " ";

    size_t startIdx = span.column > 0 ? span.column - 1 : 0;
    for (size_t j = 0; j < startIdx; ++j) {
      std::cerr << " ";
    }

    std::cerr << "\033[1;31m";
    size_t len = span.length > 0 ? span.length : 1;

    if (startIdx >= lineContent.size()) {
      len = 1;
    } else {
      size_t maxAvailable = lineContent.size() - startIdx;
      if (len > maxAvailable) len = maxAvailable;
    }

    const size_t MAX_UNDERLINE = 40;
    if (len > MAX_UNDERLINE) {
      size_t half = MAX_UNDERLINE / 2;
      for (size_t i = 0; i < half; ++i) std::cerr << "^";
      std::cerr << "...";
      for (size_t i = 0; i < half; ++i) std::cerr << "^";
    } else {
      for (size_t j = 0; j < len; ++j) {
        std::cerr << "^";
      }
    }
    std::cerr << "\033[0m" << std::endl;
  }
};

} // namespace zap

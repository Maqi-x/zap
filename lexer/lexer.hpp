#pragma once
#include "../token/token.hpp"
#include "../utils/diagnostics.hpp"
#include <string>
#include <vector>

class Lexer {
public:
  zap::DiagnosticEngine &_diag;
  size_t _pos;
  size_t _line;
  size_t _column;
  std::string _input;

  Lexer(zap::DiagnosticEngine &diag) : _diag(diag) {}
  ~Lexer() {}
  std::vector<Token> tokenize(const std::string &input);
  char Peek2();
  char Peek3();
  bool isAtEnd() const;
};

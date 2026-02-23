#pragma once
#include <string>

enum TokenType {

  IMPORT = 1,
  FUN = 2,
  RETURN = 3,
  IF = 4,
  ELSE = 5,
  WHILE = 6,
  FOR = 7,
  MATCH = 8,
  VAR = 9,
  EXTERN = 10,
  MODULE = 11,
  PUB = 12,
  PRIV = 13,
  RECORD = 14,
  IMPL = 15,
  STATIC = 16,
  ENUM = 17,
  SEMICOLON = 18,
  COLON = 19,
  DOUBLECOLON = 20,
  DOT = 21,
  COMMA = 22,
  LPAREN = 23,
  RPAREN = 24,
  LBRACE = 25,
  RBRACE = 26,
  ARRAY = 27,
  SQUARE_LBRACE = 28,
  SQUARE_RBRACE = 29,
  QUESTION = 30,
  ARROW = 31,
  LAMBDA = 32,
  LESSEQUAL = 33,
  LESS = 34,
  GREATER = 35,
  GREATEREQUAL = 36,
  EQUAL = 37,
  ASSIGN = 38,
  PLUS = 39,
  MINUS = 40,
  MULTIPLY = 41,
  REFERENCE = 42,
  DIVIDE = 43,
  MODULO = 44,
  POW = 45,
  NOT = 46,
  AND = 47,
  OR = 48,
  ID = 49,
  INTEGER = 50,
  FLOAT = 51,
  STRING = 52,
  CHAR = 53,
  BOOL = 54,
  BREAK = 55,
  CONTINUE = 56,
  ELLIPSIS = 57, // ...
  NOTEQUAL = 58,
  CONCAT = 59, // ~
  VAL,
};

///
/// @brief Contains in-file related information like line, column, offset, and length.
///
struct SourceSpan {
  size_t line;
  size_t column;
  size_t offset;
  size_t length;

  SourceSpan(size_t l = 0, size_t c = 0, size_t o = 0, size_t len = 0)
      : line(l), column(c), offset(o), length(len) {}

  static SourceSpan merge(const SourceSpan &start, const SourceSpan &end) {
    size_t newLen = (end.offset + end.length) - start.offset;
    return SourceSpan(start.line, start.column, start.offset, newLen);
  }
};

class Token {
public:
  SourceSpan span;
  TokenType type;
  std::string value;

  Token(TokenType type, const std::string &value, SourceSpan span)
      : span(span), type(type), value(value) {}

  // Helper constructor for when we build span component-wise
  Token(TokenType type, const std::string &value, size_t line, size_t column,
        size_t offset, size_t length)
      : span(line, column, offset, length), type(type), value(value) {}

  ~Token() {}
};

///
/// @brief Turns the provided 'TokenType' to a string.
/// @param type The provided type.
/// @return String version of the token type.
///
inline std::string tokenTypeToString(TokenType type) {
  switch (type) {
    case TokenType::IMPORT: return "import";
    case TokenType::FUN: return "fun";
    case TokenType::RETURN: return "return";
    case TokenType::IF: return "if";
    case TokenType::ELSE: return "else";
    case TokenType::WHILE: return "while";
    case TokenType::FOR: return "for";
    case TokenType::VAR: return "var";
    case TokenType::RECORD: return "record";
    case TokenType::ENUM: return "enum";
    case TokenType::SEMICOLON: return ";";
    case TokenType::COLON: return ":";
    case TokenType::DOUBLECOLON: return "::";
    case TokenType::DOT: return ".";
    case TokenType::COMMA: return ",";
    case TokenType::LPAREN: return "(";
    case TokenType::RPAREN: return ")";
    case TokenType::LBRACE: return "{";
    case TokenType::RBRACE: return "}";
    case TokenType::SQUARE_LBRACE: return "[";
    case TokenType::SQUARE_RBRACE: return "]";
    case TokenType::ASSIGN: return "=";
    case TokenType::EQUAL: return "==";
    case TokenType::NOTEQUAL: return "!=";
    case TokenType::PLUS: return "+";
    case TokenType::MINUS: return "-";
    case TokenType::MULTIPLY: return "*";
    case TokenType::DIVIDE: return "/";
    case TokenType::ID: return "identifier";
    case TokenType::INTEGER: return "integer literal";
    case TokenType::FLOAT: return "float literal";
    case TokenType::STRING: return "string literal";
    case TokenType::BOOL: return "boolean literal";
    case TokenType::VAL: return "val";
    default: return "unknown token";
  }
}
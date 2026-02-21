# Gemini Project Context: Zap Programming Language

This document provides foundational context for the Gemini AI assistant to understand and assist with the development of the Zap programming language project.

## Project Overview

The **Zap compiler (`zapc`)** is a high-level systems programming language designed to be a faster, more modern successor to Go. It addresses Go's limitations while maintaining a familiar syntax.

- **Primary Language:** C++17
- **Backend Technology:** **LLVM**
- **Memory Management:** **Automatic Reference Counting (ARC)**
- **Key Features:** Better error handling, real enums with pattern matching, if-expressions, and native target support.

## Compiler Architecture

The compiler is organized into a modular pipeline:

1.  **Lexer (`lexer/`):** Tokenizes Zap source code (`.zap` files).
2.  **Parser (`parser/`):** Builds an Abstract Syntax Tree (AST) from the token stream.
3.  **AST (`ast/`):** Defines the hierarchical nodes representing the language's grammar.
4.  **Sema (`sema/`):** **Semantic Analysis Stage.**
    - **Binder:** Resolves symbols, manages scopes via a `SymbolTable`, and performs type checking.
    - **Bound AST:** A strongly-typed AST used for code generation.
5.  **IR Generation (`ir/`):** Generates **Zap Intermediate Representation (ZIR)** from the Bound AST.
6.  **Main (`main.cpp`):** The entry point that orchestrates the entire compilation process and handles CLI arguments.

## Building and Running

The project uses **CMake** as its build system.

- **Build the compiler:**
  ```sh
  ./build.sh
  ```
  The executable is placed at `build/zapc`.

- **Run the compiler:**
  ```sh
  ./build/zapc path/to/file.zap [options]
  ```
  Available options include `--zir` to see the intermediate representation and `--debug` for verbose output.

- **Format the codebase:**
  ```sh
  ./fmt.sh
  ```
  Uses `clang-format` to maintain style consistency.

## Development Conventions

### Coding Style
- **Naming:** Built-in types must start with a capital letter (e.g., `Int`, `Float`, `String`).
- **Patterns:** The **Visitor Pattern** is used for traversing both the raw AST (during binding) and the Bound AST (during IR generation).

### Project Structure
- `docs/`: Comprehensive English documentation of the language syntax.
- `example/`: A collection of `.zap` files demonstrating language features (useful for testing and reproduction).
- `sema/`: Contains the logic for symbol resolution and type safety.

### Contribution Guidelines
Always verify changes by compiling the examples and checking the ZIR output for stability and correctness. Avoid regressions in memory management logic as Zap relies on a precise ARC implementation.

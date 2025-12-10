# Ignis Programming Language

> **Status**: Pre-alpha 0.1 - Early-stage development

Ignis is a modern compiled programming language designed with a focus on clarity, efficiency, and type safety. It compiles to C, leveraging the mature C ecosystem while providing a more expressive syntax.

## Table of Contents

- [Features](#features)
- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Installation](#installation)
  - [Building from Source](#building-from-source)
  - [Compiling to C](#compiling-to-c)
  - [Running Programs](#running-programs)
- [Architecture](#architecture)
- [Contributing](#contributing)
- [License](#license)

## Features

- ✨ **Modern syntax** with clear, readable code
- 🔄 **Compiles to C** for maximum compatibility and performance
- 🛡️ **Static typing** with type inference support
- 📦 **Standard library** for common operations
- 🔧 **CLI toolchain** for easy compilation and management
- 🎯 **Early error detection** with comprehensive parsing and semantic analysis

## Getting Started

### Prerequisites

- **C++17** compiler (GCC, Clang, or MSVC)
- **CMake 3.31.6** or higher
- **C compiler** (GCC, Clang) to compile generated C code

### Installation

Ignis is still in early development. To use it, you'll need to build from source.

### Building from Source

```bash
# Clone the repository
git clone https://github.com/funcieqDEV/ignis.git
cd ignis

# Create build directory
mkdir build
cd build

# Configure and build
cmake ..
make

# Run Ignis
./Ignis -h
```

### Hello World
**hello.ign:**
```ignis
fn writeLn(str: string, ...) -> i32;

fn main() -> i32 {
    writeLn("Hello, World!");
    return 0;
}
```

### Compiling to C

Generate C code from an Ignis source file:

```bash
./Ignis -cc input.ign output.c
```

This produces a standalone C file that can be compiled with any C compiler.

### Running Programs

After generating C code, compile with a C compiler:

```bash
gcc -o program output.c ignis_std.c
./program
```

Compile and run:
```bash
./Ignis -cc hello.ign hello.c
gcc -o hello hello.c ignis_std.c
./hello
```
## Architecture

Ignis uses a multi-stage compilation pipeline:

```
Source Code (.ign)
      ↓
   Lexer (Tokenization)
      ↓
   Parser (AST Generation)
      ↓
Semantic Analyzer (Type Checking)
      ↓
Code Generator (C Emission)
      ↓
   C Code (.c)
      ↓
C Compiler (GCC/Clang)
      ↓
  Executable
```

### Components

- **Lexer** (`frontend/lexer/`) - Tokenizes source code
- **Parser** (`frontend/parser/`) - Builds Abstract Syntax Tree (AST)
- **Semantic Analyzer** (`frontend/sema/`) - Type checking and validation
- **Code Generator** (`backend/C/`) - Emits C code
- **AST** (`frontend/ast/`) - Node definitions and arena allocator

## Contributing

Contributions are welcome! As Ignis is in early development, there are many opportunities to help:

- Add new language features
- Improve error messages
- Optimize code generation
- Expand standard library
- Write documentation
- Fix bugs

To contribute:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

| Phase         | feature                                     | Status |
|---------------|---------------------------------------------|--------|
| **pre-alpha** | Basic function definitions                  | ✔️     |
|               | Function calls with arguments               | ✔️     |
|               | Variable declarations                       | ✔️     |
|               | Return statements                           | ✔️     |
|               | Lexer, Parser, Semantic Analyzer            | ✔️     |
|               | C code generation                           | ✔️     |
|               | Control flow (if/else, loops)               | ✔️     |
|               | Operators (arithmetic, logical, comparison) | ✔️     |
|               | Arrays and pointers                         | ⚒️     |
|               | Structs and enums                           | ⚒️     |
|               | Error handling                              | ⏳      |
|               | More type support                           | ⏳      |
|               | Modules and imports                         | ⏳      |
|               | Generics/Templates                          | ⏳      |
| **1.0**       | Direct compilation (LLVM)                   | ⏳      |
|               | Standard library                            | ⚒️     | 
|               | Traits/Interfaces)                          | ⏳      |
|               | Pattern matching                            | ⏳      |

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

For questions or issues, please open a GitHub issue.

# Variables and Types

In Zap, all types start with a capital letter (e.g., `Int`, `Float`, `Bool`, `String`).

## Declaring Variables
Variables are declared using the `var` keyword. You can optionally specify a type and provide an initial value.

```zap
var x: Int = 10;
var name: String = "Zap";
var is_active: Bool = true;
```

### Note on Punctuation
Statements in Zap typically end with a semicolon `;`.

## Basic Types
- `Int`: 64-bit signed integer.
- `Float`: 64-bit floating point number.
- `Bool`: Boolean value (`true` or `false`).
- `String`: UTF-8 encoded string.
- `Void`: Used for functions that do not return a value.

## Arrays
Arrays are fixed-size collections of elements of the same type.

```zap
var simple: [5]Int;
var initialized: [3]Int = {1, 2, 3};
```

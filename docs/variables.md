# Variables and Types

In Zap, all types start with a capital letter (e.g., `Int`, `Float`, `Bool`, `String`).

## Declaring Variables and Constants

### Variables
Variables are declared using the `var` keyword. You can optionally specify a type and provide an initial value. Variables can be reassigned.

```zap
var x: Int = 10;
x = 20; // OK
```

### Constants
Constants are declared using the `const` keyword. They **must** be initialized when declared and cannot be reassigned. Constants can be declared both globally (at the top level) and locally (within functions).

```zap
const PI: Float = 3.14159;
const APP_NAME: String = "Zap Demo";

fun main() {
    const LOCAL_X: Int = 42;
    // LOCAL_X = 43; // Error: Cannot assign to constant
}
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

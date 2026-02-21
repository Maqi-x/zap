# Data Structures

Zap provides built-in support for complex data structures like records, enums, and arrays.

## Records
Records are custom data types that group together variables under a single name.

```zap
record Person {
    name: String,
    age: Int,
    email: String
}
```

Records can be used to pass complex data between functions and organize your application state.

## Enums
Enums are used to define a set of named constants.

```zap
enum Color { Red, Green, Blue }

enum Size { Small, Medium, Large, ExtraLarge }
```

Enums are especially powerful when combined with pattern matching (planned feature).

## Arrays
Arrays are fixed-size, zero-indexed collections of elements.

```zap
var list: [3]Int = {1, 2, 3};
```

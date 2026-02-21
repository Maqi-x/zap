# Functions

Functions are declared using the `fun` keyword. They can have parameters and a return type.

## Declaration
The return type is specified after the parameter list, without a colon (`:`).

```zap
fun add(a: Int, b: Int) Int {
    var result: Int = a + b;
    return result;
}
```

If a function does not return a value, the return type can be omitted (it defaults to `Void`).

```zap
fun say_hello(name: String) {
    // some logic here
}
```

## Function Calls
Functions are called using the standard C-like syntax:

```zap
var sum: Int = add(10, 20);
```

## Recursion
Recursive function calls are fully supported.

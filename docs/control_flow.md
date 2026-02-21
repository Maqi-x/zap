# Control Flow

Zap provides simple and powerful control flow structures.

## While Loops
The `while` loop repeatedly executes a block of code while a condition is true.

```zap
var i: Int = 0;
while i < 10 {
    i = i + 1;
}
```

## If Expressions
In Zap, `if` is an **expression**, meaning it can return a value. This can be used for conditional assignments.

```zap
var age: Int = 18;
var status: String = if age >= 18 { "adult" } else { "minor" };
```

You can also use it as a standard statement:

```zap
if age >= 18 {
    // some logic here
} else {
    // some logic here
}
```

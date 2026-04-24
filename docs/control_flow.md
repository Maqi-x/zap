# Control Flow

Zap provides explicit, predictable control flow with strict type checking.

## `if` Statements

Use `if` to execute a block conditionally:

```/dev/null/examples.zp#L1-7
var age: Int = 20;

if age >= 18 {
    // adult path
} else {
    // minor path
}
```

### `else if` Chains

```/dev/null/examples.zp#L1-11
var score: Int = 78;

if score >= 90 {
    // A
} else if score >= 80 {
    // B
} else if score >= 70 {
    // C
} else {
    // D/F
}
```

## `while` Loops

`while` repeats as long as the condition is `true`:

```/dev/null/examples.zp#L1-5
var i: Int = 0;
while i < 10 {
    i = i + 1;
}
```

## `break` and `continue`

Inside loops:

- `break;` exits the loop immediately
- `continue;` skips to the next iteration

```/dev/null/examples.zp#L1-12
var i: Int = 0;
while i < 10 {
    i = i + 1;

    if i == 3 {
        continue;
    }
    if i == 8 {
        break;
    }
}
```

Using `break`/`continue` outside a loop is a semantic error.

## Ternary Expressions (`?:`)

Zap supports conditional expressions:

```/dev/null/examples.zp#L1-3
var points: Int = 80;
var grade: String = points >= 60 ? "pass" : "fail";
```

### Ternary Rules

- Condition must be `Bool`
- Both branches must be type-compatible
- Result type is inferred from branch compatibility rules

## Condition Type Rules

For `if`, `while`, and ternary:

- Condition **must** be `Bool`
- Numeric and string values are **not** implicitly truthy/falsy

Invalid examples:

```/dev/null/examples.zp#L1-7
var n: Int = 1;
if n {          // error: condition must be Bool
}

var s: String = "ok";
while s {       // error: condition must be Bool
}
```

Preferred explicit style:

```/dev/null/examples.zp#L1-4
var n: Int = 1;
if n != 0 {
    // ...
}
```

## Blocks and Scope

Each `{ ... }` block introduces a scope for local declarations.

```/dev/null/examples.zp#L1-9
var x: Int = 10;

if x > 0 {
    var y: Int = x + 1;
    // y visible only here
}

// y is out of scope here
```

## `return` in Control Flow

Functions can return early from any branch:

```/dev/null/examples.zp#L1-11
fun sign(x: Int) Int {
    if x < 0 {
        return -1;
    } else if x > 0 {
        return 1;
    }
    return 0;
}
```

In non-`Void` functions, missing return paths may produce a warning.

## Common Diagnostic Categories

Typical control-flow-related diagnostics include:

- non-`Bool` conditions (`if` / `while` / ternary)
- incompatible ternary branch types
- `break` / `continue` outside loops
- unreachable or malformed parser constructs (missing tokens, unexpected tokens)

See also: [Diagnostic Codes](diagnostic_codes.md)

## Best Practices

- Keep conditions explicit and boolean.
- Prefer early returns to deeply nested branches.
- Use ternary only for short, readable expressions.
- Keep loop bodies small and intent-revealing.
- Use `break`/`continue` sparingly and clearly.
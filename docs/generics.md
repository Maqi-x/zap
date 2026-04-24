# Generics

Zap supports **static generics** for functions, records/structs, classes, and methods.

Generics let you write reusable code while preserving compile-time type safety. Type arguments are resolved at compile time, and invalid specializations are diagnosed early.

---

## Generic Functions

Define type parameters after the function name:

```/dev/null/examples.zp#L1-3
fun id<T>(value: T) T {
    return value;
}
```

Use with explicit type arguments:

```/dev/null/examples.zp#L1-2
var a: Int = id<Int>(42);
var b: Bool = id<Bool>(true);
```

Or with type inference:

```/dev/null/examples.zp#L1-2
var x: Int = id(7);
var y: Bool = id(false);
```

### Multiple Parameters

```/dev/null/examples.zp#L1-3
fun pickFirst<T>(a: T, b: T) T {
    return a;
}
```

Both arguments must be compatible with the same inferred `T`.

---

## Generic Records and Structs

You can parameterize aggregate types:

```/dev/null/examples.zp#L1-8
record Pair<T> {
    left: T,
    right: T
}

struct Box<T> {
    value: T
}
```

Instantiate with concrete types:

```/dev/null/examples.zp#L1-3
var pair: Pair<Int> = Pair<Int>{ left: 1, right: 2 };
var box: Box<Bool> = Box<Bool>{ value: true };
```

### Generic Type Usage in Functions

```/dev/null/examples.zp#L1-3
fun unwrap<T>(box: Box<T>) T {
    return box.value;
}
```

---

## Generic Classes

Classes can also be generic:

```/dev/null/examples.zp#L1-11
class Box<T> {
    priv value: T;

    fun init(value: T) {
        self.value = value;
    }

    pub fun get() T {
        return self.value;
    }
}
```

Instantiation:

```/dev/null/examples.zp#L1-2
var intBox: Box<Int> = new Box<Int>(42);
var boolBox: Box<Bool> = new Box<Bool>(true);
```

---

## Generic Methods

Methods may define their own type parameters independently of class parameters:

```/dev/null/examples.zp#L1-9
class Util {
    pub fun echo<T>(value: T) T {
        return value;
    }

    pub fun pair<A, B>(a: A, b: B) A {
        return a;
    }
}
```

Generic methods in generic classes are supported.

---

## `where` Constraints

Use `where` to constrain type parameters to required capabilities/types.

### Function Constraint Example

```/dev/null/examples.zp#L1-3
fun speak<T>(value: T) Int where T: Animal {
    return value.sound();
}
```

### Class Constraint Example

```/dev/null/examples.zp#L1-12
class Cage<T> where T: Animal {
    priv pet: T;

    fun init(pet: T) {
        self.pet = pet;
    }

    pub fun speak() Int {
        return self.pet.sound();
    }
}
```

### Method Constraint Example

```/dev/null/examples.zp#L1-5
pub fun inspect<U>(other: U) Int where U: Animal {
    return other.sound();
}
```

If constraints are not satisfied, semantic diagnostics are emitted at compile time.

---

## `iftype` (Compile-Time Type Branching)

Zap supports compile-time branching in generic contexts via `iftype`.

```/dev/null/examples.zp#L1-13
fun classify<T>(value: T) Int {
    iftype T == Int {
        return value;
    } else iftype T == Bool {
        if value {
            return 10;
        }
        return 11;
    } else {
        return 99;
    }
}
```

`iftype` is useful when generic logic needs type-specific behavior while staying statically checked.

### `iftype` in Generic Class Methods

```/dev/null/examples.zp#L1-14
class Box<T> {
    priv value: T;

    fun init(value: T) {
        self.value = value;
    }

    pub fun score() Int {
        iftype T == Int {
            return self.value;
        } else {
            return 0;
        }
    }
}
```

---

## Type Argument Inference

Zap can infer generic arguments from:

- function call arguments
- expected types in context (in selected call-resolution scenarios)

When inference is ambiguous or insufficient, provide explicit arguments:

```/dev/null/examples.zp#L1-2
var v: Int = id<Int>(42);
var w: String = id<String>("zap");
```

---

## Typical Generic Diagnostics

Common generic-related errors include:

- generic argument count mismatch
- failed type inference
- unsatisfied `where` constraints
- invalid generic type instantiation
- invalid `iftype` usage outside generic-instantiation contexts

These diagnostics include stable codes. See:

- [Diagnostic Codes](diagnostic_codes.md)

---

## Best Practices

- Keep type parameter names simple (`T`, `U`, `K`, `V`) unless domain names improve clarity.
- Use `where` constraints to encode real requirements early.
- Prefer inference for readability, but use explicit type args when intent is unclear.
- Keep `iftype` branches focused and minimal.
- Add tests for both valid and invalid instantiations to protect generic APIs.

---
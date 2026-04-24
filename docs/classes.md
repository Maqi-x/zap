# Classes

Zap supports heap-only `class` types with methods, visibility control, inheritance, ARC ownership, and weak references.

This page summarizes current class behavior and recommended usage patterns.

---

## Class Declaration

Define a class with fields and methods:

```/dev/null/examples.zp#L1-15
class Counter {
    priv value: Int;

    fun init(start: Int) {
        self.value = start;
    }

    pub fun inc(step: Int) Int {
        self.value = self.value + step;
        return self.value;
    }
}
```

Notes:

- Fields are declared inside the class body.
- Methods use regular `fun` syntax.
- `self` is available in instance methods.
- Statements end with `;`.

---

## Allocation with `new`

Class instances are heap-allocated and created with `new`:

```/dev/null/examples.zp#L1-7
fun main() Int {
    var c: Counter = new Counter(10);
    return c.inc(5);
}
```

If a constructor-like `init(...)` exists, `new ClassName(...)` passes arguments to it.

---

## Visibility

Zap supports member visibility modifiers:

- `pub` — public
- `priv` — private
- `prot` — protected

Example:

```/dev/null/examples.zp#L1-18
class Box {
    priv secret: Int;
    prot size: Int;
    pub label: String;

    fun init(v: Int, s: Int, l: String) {
        self.secret = v;
        self.size = s;
        self.label = l;
    }

    pub fun read_label() String {
        return self.label;
    }
}
```

Typical rules:

- `priv`: accessible only inside defining class.
- `prot`: accessible in class and derived-class contexts.
- `pub`: accessible from outside.

Invalid access emits semantic diagnostics.

---

## Instance Methods and `self`

Methods declared in a class are instance methods by default.

```/dev/null/examples.zp#L1-14
class Vec2 {
    pub x: Float;
    pub y: Float;

    fun init(x: Float, y: Float) {
        self.x = x;
        self.y = y;
    }

    pub fun length_sq() Float {
        return self.x * self.x + self.y * self.y;
    }
}
```

Call methods using member syntax:

```/dev/null/examples.zp#L1-3
var v: Vec2 = new Vec2(3.0, 4.0);
var s: Float = v.length_sq();
```

---

## Static Methods (if used in your codebase)

Zap supports static-style methods in current compiler behavior.  
Use them when no instance state is required.

```/dev/null/examples.zp#L1-9
class Math2 {
    pub static fun add(a: Int, b: Int) Int {
        return a + b;
    }
}

var x: Int = Math2.add(2, 3);
```

(Prefer project conventions for static usage.)

---

## Inheritance

Zap supports single inheritance:

```/dev/null/examples.zp#L1-20
class Animal {
    pub fun sound() Int {
        return 1;
    }
}

class Dog : Animal {
    pub fun sound() Int {
        return 2;
    }
}

fun main() Int {
    var a: Animal = new Dog();
    return a.sound();
}
```

Behavior supported by current implementation/tests includes base-typed references and polymorphic dispatch in class hierarchies.

---

## ARC and Class Lifetime

Class references participate in ARC:

- copying a class reference retains
- overwriting/leaving scope releases
- destruction occurs when last strong reference is gone

This provides deterministic release behavior and predictable performance.

---

## Weak References

Zap supports weak class references to avoid ownership cycles.

Conceptually:

- strong references keep object alive
- weak references do not own target
- use helper operations such as `alive(...)` / `lock(...)` for safe access patterns

Typical restriction:

- weak references cannot be used like normal strong references without appropriate checks/access pattern.

Code that directly dereferences weak references is rejected by semantic checks.

---

## Constructors and Destructors

- Constructor-style initialization is commonly done via `fun init(...)`.
- Destruction/lifecycle cleanup is managed through runtime/lowering behavior integrated with ARC (and cycle handling strategy).

Keep constructors lightweight and deterministic.

---

## Unsafe Interactions

Classes can contain pointer fields and unsafe methods in unsafe-enabled code.

Important points:

- unsafe features require appropriate unsafe configuration/context
- pointer operations still follow unsafe rules even inside classes
- visibility rules still apply to unsafe members

When mixing classes with pointers:

- isolate unsafe logic
- keep ownership clear
- minimize surface area of raw pointer access

---

## Common Class Diagnostics

Frequent class-related compile errors include:

- private/protected access violations
- unknown fields/methods
- method overload mismatch
- weak-reference misuse
- pointer/unsafe misuse in invalid scope

For stable code families and meanings, see:

- [Diagnostic Codes](diagnostic_codes.md)

---

## Best Practices

- Keep fields private by default; expose behavior via methods.
- Use `init(...)` to establish valid object state.
- Prefer composition unless inheritance clearly improves the model.
- Use weak refs for non-owning/back links in object graphs.
- Keep unsafe code in narrow, well-documented sections.
- Add tests for lifecycle-heavy paths (retains/releases, weak lock/alive checks, inheritance dispatch).

---
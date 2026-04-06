# Classes

Zap now supports heap-only `class` types with instance methods and `new` allocation.

```zap
class Counter {
    priv value: Int;

    fun init(value: Int) {
        self.value = value;
    }

    pub fun inc(step: Int) Int {
        self.value = self.value + step;
        return self.value;
    }
}

fun main() Int {
    var counter: Counter = new Counter(10);
    return counter.inc(5);
}
```

## Notes

- `class` instances are heap allocated and created with `new`.
- Class references use ARC. `deinit()` runs automatically when the last reference is released.
- Methods are declared inside the class as `fun`.
- `self` is injected automatically for instance methods.
- Member visibility supports `pub`, `priv`, and `prot`.
- Single inheritance uses `class Child : Base`.

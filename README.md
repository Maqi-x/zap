<h1 align="center">Zap Programming Language</h1>

<p align="center">
  <img src="art/Logo.svg" alt="Zap Logo" width="275" />
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Status-Early%20Alpha-FF9800?style=for-the-badge" />
  <img src="https://img.shields.io/badge/License-Apache%202.0-4CAF50?style=for-the-badge&logo=apache&logoColor=white" />
  <a href="https://github.com/thezaplang/zap">
    <img src="https://img.shields.io/github/stars/thezaplang/zap?style=for-the-badge" />
  </a>
</p>

> Systems programming that doesn't get in your way.

You want predictable performance. No GC pauses. Real enums.
Error handling that doesn't look like noise.

**Zap is a systems language built for developers who know Go
or are ready to step into systems programming.** ARC memory
model, LLVM backend, modern syntax. Write low-level software
without low-level frustration.

[Discord](https://discord.gg/cVGqffBA6m) · [Roadmap](ROADMAP.md) · [Documentation](docs/README.md)

---

## Why Zap?

> [!WARNING]
> Early alpha — not everything is implemented yet.

| Problem | Zap's answer |
|---|---|
| GC pauses & unpredictable latency | ARC, memory freed deterministically |
| No real enums | Enums with exhaustive pattern matching |
| Verbose error handling | Failable functions |
| Limited generics | Full static generics |
| Concise conditional expressions | Ternary operator `?:` |
| Single-platform compilers | LLVM: x86, ARM, RISC-V, WASM, embedded |
| No lightweight concurrency | Fibers, like goroutines without the runtime cost |
| Hard to triage compiler errors at scale | Stable diagnostic codes (`Pxxxx`, `Sxxxx`, `Wxxxx`, `Nxxxx`) in CLI and LSP |

---

## Cycles

If you know what ARC is, you probably also know that when two objects point to each other, ARC will never delete them even though they are not needed.
Swift added `weak` to prevent this, we also added weak in Zap, but it often happens that you simply don't see that there is a cycle somewhere.
Zap solves this problem thanks to its Cycle Collector, i.e. it is actually a garbage collector operating on objects causing the cycle, REST OF OBJECTS ARE NOT AFFECTED (cycle collector can be disabled if someone needs it!)

---

## Error Handling

> [!WARNING]
> Not yet implemented.

Zap uses **failable functions**: functions that can fail declare it explicitly in their return type.

```zap
enum MathError { DivisionByZero, Overflow }

fun divide(a: Int, b: Int) Int!MathError {
    if b == 0 { fail MathError.DivisionByZero; }
    return a / b;
}

fun main() Int {
    // propagate up with ?
    var x: Int = divide(10, 2)?;

    // fallback value
    var y: Int = divide(10, 0) or 0;

    // handle locally
    var z: Int = divide(10, 0) or err {
        return 1;
    };

    return 0;
}
```

---

## Documentation

Start here: **[docs/README.md](docs/README.md)**

Highlights:
- Language guide: variables, functions, control flow, data structures, classes, memory
- Generics coverage: function/type generics, constraints (`where`), and compile-time `iftype` (see docs sections and tests)
- Diagnostic code reference: **[docs/diagnostic_codes.md](docs/diagnostic_codes.md)**

## Contributing

Zap is in early alpha. **Your feedback directly shapes the language.**

- open issues
- discuss language design
- implement features
- improve diagnostics
- write documentation

---

## Star History

<a href="https://www.star-history.com/?repos=thezaplang%2Fzap&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/image?repos=thezaplang/zap&type=date&theme=dark&legend=top-left" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/image?repos=thezaplang/zap&type=date&legend=top-left" />
   <img alt="Star History Chart" src="https://api.star-history.com/image?repos=thezaplang/zap&type=date&legend=top-left" />
 </picture>
</a>

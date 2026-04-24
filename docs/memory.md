# Memory Management in Zap

Zap uses **Automatic Reference Counting (ARC)** for memory management.

Instead of relying on a tracing garbage collector, Zap tracks object ownership through reference counts and releases memory deterministically when objects are no longer referenced.

---

## ARC Overview

With ARC:

- Every heap object has an internal reference count.
- Assigning/passing/storing references updates that count.
- When the count reaches zero, the object is destroyed immediately.

This gives you:

- **Predictable lifetime behavior**
- **No stop-the-world GC pauses**
- **Lower latency variance for systems workloads**

---

## What ARC Applies To

ARC primarily governs **reference types**, especially class instances.

Typical behavior:

- Creating a new object (`new`) produces an owned reference.
- Copying a class reference increases retain count.
- Overwriting or leaving scope releases references.

---

## Deterministic Destruction

Because destruction is tied to reference count reaching zero, cleanup timing is deterministic (modulo cycles, see below).

If a class defines a destructor/finalizer-style cleanup (for example `deinit` behavior in runtime lowering), that cleanup runs when the object is released for the last time.

This is useful for managing:

- native handles
- buffers
- file/socket wrappers
- short-lived heap objects in hot paths

---

## Strong vs Weak References

### Strong references (default)

Normal class references are strong. They keep objects alive.

### Weak references (`weak`)

A weak reference does **not** keep the target alive.

Use weak references to avoid ownership cycles in graph-like structures (parent/child, linked components, observer lists, etc.).

Zap supports weak-reference workflows such as:

- checking liveness (`alive(...)`)
- attempting temporary strong access (`lock(...)`)

(Exact APIs may evolve; prefer current compiler/runtime docs and tests for concrete usage.)

---

## Cycles and Why They Matter

Pure ARC cannot reclaim a cycle of strong references by itself:

- `A` strongly references `B`
- `B` strongly references `A`
- both counts remain > 0 even if program no longer uses them

Zap addresses this in two ways:

1. **Language-level weak references** for explicit cycle breaking.
2. **Cycle collection support** in runtime/compiler pipeline for cycle-only cleanup (as implemented by current runtime strategy).

This means you can write ARC-style code while still handling accidental strong cycles more robustly than plain ARC alone.

---

## Practical Ownership Patterns

### 1) Tree with parent links

- Children strongly owned by parent
- Parent link in child should often be `weak`

### 2) Observer/callback registries

- Registry strongly owns listener entries
- Back-references from listener to registry often `weak`

### 3) Caches and shared object graphs

- Choose one clear owner path
- Keep secondary links weak if they are non-owning

---

## Performance Characteristics

ARC costs are mostly local retain/release operations.

Compared to GC:

- Better latency predictability
- No global tracing pauses
- Potentially more frequent tiny bookkeeping operations

In practice, ARC performs well for systems-style workloads with explicit ownership design.

---

## Safety and `unsafe`

ARC handles object lifetime for managed references, but it does **not** replace rules around raw pointers and unsafe memory operations.

If you use unsafe pointers:

- you are responsible for pointer validity
- ARC will not protect invalid raw pointer dereference
- keep unsafe code isolated and audited

---

## Common Pitfalls

- Building bidirectional strong links unintentionally
- Holding references in long-lived globals without release path
- Mixing non-owning intent with strong fields
- Assuming weak references are always immediately lockable

---

## Recommended Best Practices

- Model ownership explicitly in data structures.
- Default to strong references for true ownership.
- Use `weak` for non-owning/back references.
- Keep object graphs simple and acyclic where practical.
- Add focused tests for lifecycle-sensitive code paths.
- Use diagnostic output and runtime tests to validate assumptions.

---

## Related Docs

- [Classes](classes.md)
- [Data Structures](data_structures.md)
- [Variables and Types](variables.md)
- [Diagnostic Codes](diagnostic_codes.md)
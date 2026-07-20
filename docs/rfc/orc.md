# RFC: Ownership and Optimized Reference Counting (ORC)

- Status: **Draft**
- Date: 2026-07-20
- Authors: Zap project
- Eventually replaces: heuristic ARC in the LLVM backend and the eager cycle collector

## Summary

Zap will move from ARC, where LLVM code generation guesses ownership decisions,
to a model where ownership is part of verified ZIR. ORC is the implementation
strategy: first produce semantically correct copy, move, borrow, and destroy
operations, then eliminate redundant reference-counting operations.

The goal is neither to import Rust's full borrow checker nor to require users
to annotate every variable. Ordinary code should remain simple, with
deterministic destruction and explicit contracts only for APIs that may take a
value or return a non-owning reference.

This document specifies the target design. It does not change current compiler
behavior or the runtime ABI.

## Motivation

The current backend infers ownership from the kind of bound expression and
private sets of ZIR values. A new expression form can therefore accidentally
skip a retain or release. There is also no contract for a verifier to check
before LLVM lowering.

The target model provides:

- one ownership model for classes, `String`, records, arrays, and failable payloads;
- ownership operations that are explicit and verifiable in ZIR;
- deterministic destruction of acyclic values;
- retain/release elision based on CFG and last use;
- no global collection in the hot `release` path; and
- a clear boundary between language semantics and the LLVM/runtime ABI.

## Goals and non-goals

### Goals

1. Every managed value on every CFG path is moved, copied, or destroyed under
   an explicit contract.
2. Ordinary assignment and ordinary calls remain ergonomic.
3. `sink` lets an API declare that it may take a value.
4. `ref` denotes a borrowed result or value that does not gain ownership.
5. `StringView` cannot outlive its owner.
6. The LLVM backend emits an ownership plan instead of inferring one from
   expression kinds.

### Non-goals of the first version

- a complete, general-purpose borrow checker;
- a public `move` keyword or builtin;
- atomic reference counting or sharing managed objects between threads;
- a new stable FFI ABI; and
- automatic collection of every cycle as an implicit language guarantee.

## Value model

Every ZIR value has one ownership category:

| Category | Meaning |
|---|---|
| `Owned` | Carries one destruction obligation; it can be moved or destroyed. |
| `Borrowed` | Temporary access that creates no `destroy` obligation. |
| `Weak` | Does not keep its target alive; dereferencing requires a safe lock. |
| `Raw` | An unsafe operation outside the automatic ownership model. |

These categories are compiler metadata, not annotations required throughout
user code. `weak` remains a reference qualifier; it is not a flag mutating a
class declaration.

## User syntax and semantics

### Ordinary assignment

Ordinary assignment has copy semantics:

```zap
var first: String = "zap";
var second: String = first;
```

After the assignment, both slots remain valid to use. For a managed type, a
copy creates another strong ownership; aggregate copies act recursively on
their fields.

The compiler **may** internally turn a copy into a move when analysis proves
that the source is never used again. This optimization must not change program
semantics or make a source-level variable unavailable.

### `sink` parameters

`sink` is a contextual type qualifier for a parameter. It means that a
function may take the argument value:

```zap
fun append(value: sink String) Void {
    // value may be stored or moved further.
}
```

Calls need no additional syntax:

```zap
append(text);
```

If `text` is used for the last time, ORC may pass it without a copy. If it is
used later, the compiler creates a copy for the `sink` parameter. `sink` is an
API contract and optimization opportunity, not a linear requirement that the
argument must be consumed.

An ordinary managed parameter is borrowed for the duration of the call. A
function that retains such an argument after returning must make a semantic
copy when storing it in an owned location.

### Borrowed results: `ref`

Zap already distinguishes reference returns. The target meaning of `ref T` is
a borrowed result with no new ownership and no automatic destruction:

```zap
fun first_name(user: ref User) ref String {
    return user.name;
}
```

The binder and verifier must reject a `ref` return that refers to a local or
temporary value. Zap does not add a separate `lent` spelling: the existing
`ref` term matches Zap syntax.

### Explicit `move`

The first version of this RFC does not introduce a `move` keyword or require
one. After last-use analysis exists, a `move(expr)` builtin may be considered
for rare cases where a programmer wants to force a transfer. That is a
separate language decision, not a prerequisite for the first ORC version.

## Fields, aggregates, and returns

An ordinary managed class or record field is owned. `weak` denotes a
non-owning field:

```zap
class Child {
    var parent: weak Parent;
    var name: String;
}
```

Writing an owned field has copy semantics; initialization may use an internal
transfer when the source is used for the last time. Records and fixed-size
arrays receive recursively generated copy/drop glue. A normal function result
returning a class, `String`, or aggregate containing managed values is
`Owned`; a `ref` result is `Borrowed`.

## `String` and `StringView`

`String` is an intrinsic owned value with one `copy`, `move`, and `drop`
semantic model. ABI details, such as `{ptr, len}` and allocation headers, do
not belong in semantic ZIR.

`StringView` is a borrowed view. A local conversion from a live `String` is
valid:

```zap
fun length(view: StringView) Int { return view.len; }

var text: String = "zap";
var n: Int = length(text);
```

Initially, escape analysis must reject at least the following cases when a
view derives from a shorter-lived owner:

- returning it from a function;
- storing it in a field, global, or static value; and
- passing it to `sink` or another location that may retain it.

The compiler does not implicitly copy a `String` when a view escapes. Such a
copy would hide allocation and change the cost of code without changing its
type. Broader lifetime analysis is a separate stage.

## Weak references, cycles, and destruction

Strong references form an ownership graph. A back-reference or observer
relationship should be expressed with `weak`, for example a child-to-parent
link.

`weak` does not extend an object's lifetime. Acquiring a temporary strong
reference requires an explicit lock and may return `null`; the exact lock API
will be unified with the runtime ABI.

The cycle collector must not run from every `release` with a nonzero count.
The migration strategy remains open and requires a separate approved decision
before changing the runtime:

1. require `weak` to break cycles and remove the collector; or
2. retain a trial-deletion collector as an infrequent operation outside the hot path.

Regardless of the strategy, a destructor must not resurrect an object once
destruction has begun, and `weak.lock()` must fail for a logically destroying
object. Destruction order within a cycle must not be an API guarantee.

## Concurrency

The first ORC version is single-threaded for managed objects. Strong and weak
counts are non-atomic, and an object cannot be shared as a managed reference
between threads until a synchronized future model exists. Adding threads
requires a separate RFC covering atomic RC or separate `Shared`/control blocks
and a safe weak lock.

## ZIR and pipeline

Before ABI lowering, ZIR must contain explicit, typed operations:

```text
copy value
move value
borrow value
destroy value
store.initialize
store.assign
```

The ownership verifier checks that:

1. an `alloc` result and an ordinary owned call result are `Owned`;
2. a borrowed result does not become owned without a copy or lock;
3. every owned value is moved or destroyed exactly once on every CFG path;
4. phi, branch, return, store, and call preserve their ownership contracts; and
5. weak and raw values are not confused with strong ownership.

Ownership lowering then inserts concrete retain/release operations and drop
glue. The ORC pass performs liveness/last-use analysis and eliminates redundant
pairs. The LLVM backend only emits the resulting, already verified plan.

```text
typed ZIR
  -> ownership verification
  -> ownership lowering
  -> ORC optimization
  -> ABI lowering
  -> LLVM
```

## Implementation plan

1. Adopt this RFC and resolve the open cycle decision.
2. Add test-only instrumentation for allocation, retain, release, destroy, and collection.
3. Complete ownership metadata in ZIR for calls, casts, weak locks, and aggregates.
4. Add ownership invariants to `ZirVerifier` and CFG tests for early return,
   branches, loops, phi nodes, and failable returns.
5. Extract ownership lowering from LLVM code generation without changing
   observable semantics.
6. Add last-use analysis and `sink` parameters.
7. Replace the eager collector with the strategy approved in a separate RFC.

Every stage must preserve the full suite and include regression tests that
count runtime operations; exit code alone is not a sufficient memory test.

## Open decisions requiring approval

1. Must cycles remain automatically collectible, or is `weak` the required
   way to break them?
2. If a collector remains, when is it scheduled and run, and what is its
   destructor contract?
3. Does `sink` become public syntax immediately, or initially only metadata
   for standard-library APIs and compiler intrinsics?
4. What exact syntax and `lock` API should `weak` use after separating the
   reference qualifier from the class declaration?
5. Which language features should the future concurrency model support first?

## Acceptance criteria

The first ORC stage is complete when:

- the LLVM backend has no private heuristic sets of owned values;
- `ZirVerifier` checks ownership contracts for all instructions;
- classes, `String`, records, arrays, and failable payloads share one
  copy/drop model;
- retain/release does not run global cycle collection in the hot path; and
- sanitizer tests and copy/drop counters cover CFG and lifetime behavior.

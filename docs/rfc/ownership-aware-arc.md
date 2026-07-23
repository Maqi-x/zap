# RFC: Ownership-Aware ARC and Scheduled Cycle Collection

- Status: **Draft**
- Date: 2026-07-23
- Authors: Zap project
- Replaces: heuristic ARC decisions in LLVM code generation and eager
  cycle-collection scheduling

## Summary

Zap uses automatic reference counting with ownership information verified in
ZIR. The target is **ownership-aware ARC with scheduled cycle collection**,
not full ownership-based reference counting and not non-lexical lifetime for
source variables.

The model combines:

- lexical lifetime for named local variables;
- explicit `copy`, `move`, `borrow`, and `destroy` operations in ZIR;
- automatic retain/release lowering;
- owner provenance for borrowed values such as `StringView`; and
- a separate trial-deletion collector for accidental strong cycles.

Observable source semantics, including destructor timing, must be identical in
debug and optimized builds.

## Terminology

In this RFC:

- **ownership-aware ARC** means ARC generated from verified ownership
  operations rather than guessed by the LLVM backend;
- **named lvalue** means a source-level variable or assignable storage
  location with lexical lifetime;
- **temporary** means an owned result with no source-level storage that must
  remain valid after the expression;
- **borrow provenance** is the relation from a borrowed value to every owner
  that must remain alive for that value to be used; and
- **cycle collection** is a separate runtime mechanism. It does not replace
  ARC for ordinary acyclic lifetimes.

## Goals

1. Give every managed ZIR value an explicit and verifiable ownership contract.
2. Keep named local variables valid until the end of their lexical scope.
3. Preserve deterministic destruction for acyclic values.
4. Ensure that `StringView` and future borrowed values cannot outlive their
   owners.
5. Generate recursive copy/drop/trace metadata consistently for managed
   aggregates.
6. Keep ordinary `release` bounded by scheduling cycle collection at
   controlled safe points.
7. Make ownership and destructor behavior independent of optimization level.

## Non-goals

The following are explicitly outside the current design:

- implicit last-use moves from named lvalues;
- non-lexical lifetime for named source variables;
- a public `move` keyword or builtin;
- optimizations that change observable destructor timing;
- a complete general-purpose borrow checker;
- atomic reference counting or managed sharing between threads;
- synchronous graph traversal in the hot `release` path; and
- implementing “full ORC” under the historical project terminology.

Any future explicit `move` syntax or non-lexical lifetime model requires a
separate RFC and must not be inferred from this document.

## Source-language semantics

### Named variables and assignment

A named local owns its value until lexical scope exit. Ordinary assignment has
logical copy semantics:

```zap
var first: String = "zap";
var second: String = first;
```

Both variables remain valid. The compiler may remove redundant physical
retain/release operations only when doing so preserves this source behavior
and the same observable destruction point. It must not empty `first` merely
because the assignment is its last use.

### `sink` parameters

`sink T` declares that the callee receives an owned argument:

```zap
fun consume(value: sink String) {
    println(value);
}
```

Calls use ordinary syntax, but argument preparation is deliberately
predictable:

- an owned temporary or rvalue is moved into the parameter;
- a named lvalue is copied, even when the call is its final use; and
- the named lvalue remains alive until its lexical scope ends.

This rule is independent of CFG last-use analysis and optimization level.

### `ref` and borrowed results

`ref T` denotes borrowed access and creates no destruction obligation. A
borrowed result must be tied to an owner that outlives every use of the result.
The compiler rejects returns, stores, captures, and calls that let the borrow
escape without a valid lifetime contract.

### No public `move`

`move` exists as an internal ZIR operation for owned temporaries and explicit
compiler transfers. It is not currently source syntax and cannot be used to
shorten the lifetime of a named local.

## ZIR ownership model

Managed ZIR values use explicit operations:

```text
copy value
move value
borrow value, owner
destroy value
store.initialize
store.assign
```

Their contracts are:

- `copy` creates an independent destruction obligation;
- `move` transfers an existing obligation from an owned SSA token;
- `borrow` creates no ownership and records its owner provenance;
- `destroy` closes one ownership obligation; and
- store and call modes state whether they borrow or transfer a value.

The ownership verifier must prove that every reachable path uses valid values,
does not move or destroy an owner while a dependent borrow is live, and closes
each owned SSA obligation exactly once. This SSA rule does not authorize
non-lexical lifetime for named local storage.

## Borrow provenance

`StringView` is a borrowed view. Converting an owned `String` produces an
explicit `BorrowInst(result, owner)`.

`BorrowProvenance` carries the relation through:

- phi incoming edges;
- local store/load;
- derived `StringView` values;
- pass-through CFG blocks; and
- loop back-edges.

Liveness, escape checks, ownership verification, and later ownership lowering
must consume the same provenance model. They must not maintain parallel
heuristics for deciding whether a view is local.

Until typed interprocedural escape contracts exist, a borrow derived from a
function-local owner must not:

- be returned;
- be stored in a field, global, static, or caller-provided destination;
- be captured by longer-lived state; or
- be passed to an API that may retain it.

`noescape` and `may-escape` contracts must eventually be represented
consistently in function definitions, external declarations, function
pointers, and call sites.

## Aggregates and metadata

Classes, `String`, records, arrays, tagged unions, failable values, and
containers containing managed elements use one type-driven source of:

- layout information;
- copy glue;
- drop glue; and
- trace metadata for cycle collection.

Tagged unions operate only on the active variant. Dynamic containers delegate
copy/drop/trace to element metadata. LLVM code generation must not infer these
operations from rendered type names or expression kinds.

## Weak references, cycles, and destruction

Strong references form the ARC ownership graph. `weak` expresses intentional
non-owning relationships and does not keep the target alive. Locking a weak
reference may fail once destruction begins.

Accidental strong cycles are handled by scheduled trial deletion:

1. a non-final `release` may record a possible cycle root;
2. crossing a threshold schedules collection without traversing the graph;
3. collection runs at a defined safe point;
4. trial deletion uses type trace metadata without rewriting real strong
   counts; and
5. confirmed cycles are finalized through normal, single-execution drop glue.

The initial runtime is explicitly single-threaded and owns this state through
one `RuntimeContext`. Destructors cannot resurrect objects after logical
destruction begins, and cycle destruction order is not an API guarantee.

## Pipeline

```text
typed ZIR
  -> borrow provenance and escape verification
  -> ownership verification
  -> ownership lowering
  -> retain/release optimization that preserves lexical semantics
  -> ABI lowering
  -> LLVM
```

The LLVM backend emits an already verified plan. It does not decide whether a
named source variable is moved.

## Rejected experiments and invariants

These constraints are normative:

- The removed `sink_last_use` pass and `TakeInst` must not be reintroduced.
  They emptied named local slots and made destructor timing depend on CFG
  analysis.
- Broad edge-release insertion previously caused double release.
- A release-frontier experiment corrupted the heap in
  `fs_extra_test.zp` and `string_view_api_overload_test.zp`.
- Edge destroy/release remains valid only when derived from complete ownership
  dataflow and borrow provenance, with critical-edge splitting where needed.

## Implementation sequence

1. Use `BorrowProvenance` in escape analysis and ownership diagnostics.
2. Add typed `noescape` and `may-escape` contracts.
3. Complete per-path ownership obligations for SSA temporaries across phi,
   loops, early returns, and critical edges.
4. Generate complete copy/drop/trace metadata for aggregates, tagged unions,
   and containers.
5. Introduce a single-threaded `RuntimeContext` and scheduled trial deletion.
6. Define weak-lock, destructor reentrancy, OOM, and safe-point behavior.

Every stage must preserve lexical lifetime for named locals and include
regression tests with exact copy/drop/destroy counters.

## Open decisions

1. Which candidate-root and allocation thresholds schedule collection?
2. What exact syntax and runtime API should weak locking expose?
3. Which safe points are guaranteed, and what reentrancy is allowed there?
4. What OOM behavior is required for collector scratch storage?
5. What separate future design, if any, permits managed sharing across
   threads?

## Acceptance criteria

The ownership-aware ARC milestone is complete when:

- the LLVM backend has no private ownership heuristics;
- verifier and escape analysis use the same borrow provenance;
- every owned SSA obligation is moved or destroyed exactly once on every
  reachable path;
- named lvalues retain lexical lifetime at every optimization level;
- managed aggregates share one complete copy/drop/trace model;
- ordinary `release` does not synchronously traverse the object graph;
- scheduled trial deletion reclaims tested cycles without rewriting strong
  counts; and
- sanitizers plus exact operation counters cover CFG, weak, destructor, cycle,
  and OOM behavior.

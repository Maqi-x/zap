# Strings: `String`, `StringView`, `TextBuf`

This document describes the current string model in Zap and the practical ownership rules.

## Types

### `String` (owned)

- `String` is an owned string value (`ptr + len`) managed by runtime retain/release.
- String literals (for example `"hello"`) are represented as `StringView` and are converted to `String` when needed by context.
- Concatenation (`+`) produces a new owned `String`.

### `StringView` (borrowed)

- `StringView` is a non-owning view:
  - `ptr: *Char`
  - `len: Int`
- It does not own memory and does not retain/release anything by itself.
- Use it for read-only access and cheap slicing.

### `TextBuf` (mutable builder)

- `TextBuf` is a class wrapper around internal owned `String` data.
- It is intended for incremental text building (`push`, `pushChar`) and finalization via `build()`.

## API Behavior (`std/string`)

## `view(s: String) StringView`

- Returns a borrowed view into `s` memory.
- No copy is made.

## `owned(v: StringView) String`

- Creates a new owned `String` by copying bytes from `v.ptr` and `v.len`.
- This is the explicit conversion from borrowed view to owned string.

## `len(s: StringView) Int`

- Returns `s.len`.

## `at(s: StringView, i: Int) Char`

- Bounds-checked read.
- Returns `'\0'` for invalid index or null pointer.

## `slice(s: StringView, start: Int, len: Int) StringView`

- Returns another borrowed view into the same backing memory.
- Does not allocate.
- Clamps bounds:
  - negative `start` becomes `0`
  - oversized `len` is trimmed to available range
  - invalid/empty inputs return `{ ptr: null, len: 0 }`

## `eq(a: StringView, b: StringView) Bool`

- Length-first comparison, then byte-by-byte compare.

## Ownership Rules

1. `String` owns data.
2. `StringView` borrows data.
3. `view` is borrow-only (zero-copy).
4. `owned` is copy-to-own.
5. `TextBuf.build()` returns owned `String`.

## Interop Notes

- Low-level runtime helpers stay in C (allocation, retain/release, C interop boundaries).
- High-level string view operations are implemented in Zap (`std/string`).


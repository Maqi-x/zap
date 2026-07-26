#!/usr/bin/env bash
set -euo pipefail

ZAPC="${1:-}"
INPUT="${2:-}"
OUTPUT="${3:-}"

if [[ -z "$ZAPC" || -z "$INPUT" || -z "$OUTPUT" ]]; then
    echo "Usage: $0 <zapc> <input> <output>" >&2
    exit 1
fi

# Run compiler to emit LLVM IR
if ! compile_output=$("$ZAPC" "$INPUT" -S -emit-llvm -o "$OUTPUT" 2>&1); then
    echo "Instrumented IR compilation failed:" >&2
    echo "$compile_output" >&2
    exit 1
fi

emitted_ir=$(cat "$OUTPUT")

required_symbols=(
    "zap_runtime_ownership_note_copy"
    "zap_runtime_ownership_note_drop"
    "zap_runtime_ownership_note_strong_retain"
    "zap_runtime_ownership_note_strong_release"
    "zap_runtime_ownership_note_destroy"
    "zap_arc_default_context"
    "zap_arc_deallocate"
    "zap_arc_retain_dead_object"
    "zap_arc_collect_at_safepoint"
)

for symbol in "${required_symbols[@]}"; do
    if [[ "$emitted_ir" != *"$symbol"* ]]; then
        echo "Instrumented IR does not call $symbol" >&2
        exit 1
    fi
done

if [[ "$emitted_ir" == *"zap_arc_cycle_collect"* ]]; then
    echo "Instrumented IR calls the collector directly instead of scheduling it" >&2
    exit 1
fi

if [[ "$emitted_ir" != *"call ptr @zap_arc_default_context()"* ]]; then
    echo "Instrumented IR does not pass an ARC runtime context" >&2
    exit 1
fi

echo "IR instrumentation check passed successfully."
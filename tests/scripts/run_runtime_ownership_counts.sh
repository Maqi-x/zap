#!/usr/bin/env bash
set -euo pipefail

ZAPC="${1:-}"
INPUT="${2:-}"
OUTPUT="${3:-}"
ZIR_OUTPUT="${OUTPUT}.zir"

if [[ -z "$ZAPC" || -z "$INPUT" || -z "$OUTPUT" ]]; then
    echo "Usage: $0 <zapc> <input> <output>" >&2
    exit 1
fi

if ! zir_output_log=$("$ZAPC" "$INPUT" -emit-zir -o "$ZIR_OUTPUT" 2>&1); then
    echo "Instrumented ownership-count ZIR emission failed:" >&2
    echo "$zir_output_log" >&2
    exit 1
fi

emitted_zir=$(cat "$ZIR_OUTPUT")

if [[ "$emitted_zir" != *"ownership.destroy."* ]]; then
    echo "Instrumented ownership-count ZIR has no edge cleanup block" >&2
    exit 1
fi

if ! compile_output=$("$ZAPC" "$INPUT" -o "$OUTPUT" 2>&1); then
    echo "Instrumented ownership-count compilation failed:" >&2
    echo "$compile_output" >&2
    exit 1
fi

if ! run_output=$("$OUTPUT" 2>&1); then
    echo "Instrumented ownership-count program failed:" >&2
    echo "$run_output" >&2
    exit 1
fi

echo "Ownership counts test passed successfully."
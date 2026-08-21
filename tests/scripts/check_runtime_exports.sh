#!/usr/bin/env bash
set -euo pipefail

RUNTIME_OBJECT="${1:-}"
EXPECTED_SYMBOLS="${2:-}"

if [[ -z "$RUNTIME_OBJECT" || -z "$EXPECTED_SYMBOLS" ]]; then
    echo "Usage: $0 <runtime-object> <expected-symbols-file>" >&2
    exit 1
fi

if [[ ! -f "$RUNTIME_OBJECT" || ! -f "$EXPECTED_SYMBOLS" ]]; then
    echo "Runtime object or expected symbols file is missing" >&2
    exit 1
fi

exported_symbols="$(nm -g --defined-only "$RUNTIME_OBJECT" | awk '{print $3}')"

while IFS= read -r symbol; do
    [[ -z "$symbol" || "$symbol" == \#* ]] && continue
    if ! grep -Fxq "$symbol" <<<"$exported_symbols"; then
        echo "Runtime object is missing required ABI symbol: $symbol" >&2
        exit 1
    fi
done < "$EXPECTED_SYMBOLS"

echo "Runtime ABI export check passed successfully."

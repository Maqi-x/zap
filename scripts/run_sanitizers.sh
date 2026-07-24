#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$script_dir/.." && pwd)"
build_dir="${ZAP_SANITIZER_BUILD_DIR:-$repo_dir/build-sanitize}"

cmake -S "$repo_dir" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER="${CC:-clang}" \
  -DCMAKE_CXX_COMPILER="${CXX:-clang++}" \
  -DINCLUDE_LSP=OFF \
  -DZAP_ENABLE_SANITIZERS=ON \
  -DZAP_ENABLE_RUNTIME_INSTRUMENTATION=ON
cmake --build "$build_dir" --parallel

leak_detection="${ZAP_DETECT_LEAKS:-1}"
export ASAN_OPTIONS="detect_leaks=$leak_detection:halt_on_error=1:abort_on_error=1"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"

ctest --test-dir "$build_dir" --output-on-failure
cd "$repo_dir"
python3 run_tests.py --zapc "$build_dir/zapc" -j 1 \
  tests/string_ownership_runtime_test.zp \
  tests/string_view_owned_semantics_test.zp \
  tests/class_arc_test.zp \
  tests/class_arc_strong_test.zp \
  tests/class_cycle_weak_tombstone_test.zp \
  tests/class_record_cycle_detect_test.zp \
  tests/class_weak_lock_test.zp \
  tests/failable_class_return_test.zp \
  tests/std_network_test.zp

#!/bin/bash

# Build the compiler first
./build.sh > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "Failed to build the compiler"
    exit 1
fi

ZAPC="./build/zapc"
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

TOTAL=0
PASSED=0

run_test() {
    local file=$1
    local expected_exit_code=$2
    local description=$3

    ((TOTAL++))
    echo -n "Running $description ($file)... "
    
    $ZAPC "$file" > /dev/null 2>&1
    local exit_code=$?
    
    if [ $exit_code -eq $expected_exit_code ]; then
        echo -e "${GREEN}PASS${NC}"
        ((PASSED++))
    else
        echo -e "${RED}FAIL${NC} (expected $expected_exit_code, got $exit_code)"
    fi
}

echo "--- Zap Compiler Test Suite ---"

# Valid code (should pass)
run_test "tests/valid.zap" 0 "Valid program"

# Lexer errors (exit code 1)
run_test "tests/lexer_error.zap" 1 "Lexer error: Unterminated string"

# Syntax errors (exit code 1)
run_test "tests/syntax_error.zap" 1 "Syntax error: Missing semicolons"

# Semantic errors (exit code 1)
run_test "tests/sema_error.zap" 1 "Semantic error: Undefined variable"

# Multiple errors (exit code 1)
run_test "tests/syntax_error.zap" 1 "Multiple syntax errors"

echo "-------------------------------"
echo "Results: $PASSED / $TOTAL passed"

if [ $PASSED -eq $TOTAL ]; then
    echo -e "${GREEN}ALL TESTS PASSED!${NC}"
    exit 0
else
    echo -e "${RED}SOME TESTS FAILED!${NC}"
    exit 1
fi

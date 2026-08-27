#!/usr/bin/env bash
# compare.sh — run the OCode benchmark and an equivalent C++ program,
# side by side, so you can see the actual speed gap.
#
# Usage:  ./compare.sh [N]

set -e
N="${1:-100000}"

echo "============================================"
echo "  Performance Comparison: OCode vs C++"
echo "  (same work, N = $N iterations)"
echo "============================================"
echo ""

# 1. C++ baseline (compiled with -O2)
CPP_SRC="$(dirname "$0")/bench_cpp.cpp"
if [ ! -f "$CPP_SRC" ]; then
    CPP_SRC="/home/z/my-project/scripts/bench_cpp.cpp"
fi
CPP_BIN=/tmp/ocode_bench_cpp
g++ -std=c++17 -O2 "$CPP_SRC" -o "$CPP_BIN" 2>/dev/null

echo "--- C++ (compiled, -O2) ---"
"$CPP_BIN" "$N" | grep -E "Time:|Lines/sec:"
echo ""

# 2. OCode interpreter
OCODE_BIN="${OCODE_BIN:-/home/z/.local/bin/ocode}"
if [ ! -x "$OCODE_BIN" ]; then
    OCODE_BIN="$(dirname "$0")/../work/OCode/ocode"
fi
if [ ! -x "$OCODE_BIN" ]; then
    OCODE_BIN="$(dirname "$0")/ocode"
fi

echo "--- OCode (interpreted) ---"
echo "$N" | "$OCODE_BIN" "$(dirname "$0")/../work/OCode/examples/normal/benchmark.oc" 2>&1 | \
    grep -E "Time:|Lines/sec:"

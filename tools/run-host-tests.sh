#!/usr/bin/env bash
# Compile and run the host-only C++ tests under android/sharedCode/src/test/cpp
# with the same flags CI uses for tests/drag_sample_policy_test.cpp. No Android
# toolchain needed. Exit status is non-zero if any test fails to build or run.
#
# Usage: tools/run-host-tests.sh [CXX]   (defaults to $CXX, then c++)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX_BIN="${1:-${CXX:-c++}}"
SRC="$ROOT/android/sharedCode/src/main/cpp"
TESTS="$ROOT/android/sharedCode/src/test/cpp"
OUT="${TMPDIR:-/tmp}/viro-host-tests.$$"
mkdir -p "$OUT"
trap 'rm -rf "$OUT"' EXIT

FLAGS=(-std=c++14 -Wall -Wextra -Werror -I"$SRC" -I"$SRC/include")

status=0
for test in VROHandPinchStateTest VROOpenXRSimpleControllerBindingsTest; do
    echo "== $test"
    if "$CXX_BIN" "${FLAGS[@]}" "$TESTS/$test.cpp" -o "$OUT/$test" && "$OUT/$test"; then
        echo "PASS $test"
    else
        echo "FAIL $test"
        status=1
    fi
done
exit $status

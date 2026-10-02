#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/viro-eskiu-input.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
ESKIUC="${ESKIUC:-eskiuc}"
CXX="${CXX:-c++}"

"$ESKIUC" "$ROOT/tests/eskiu/input_ring_backend.esk" -c -O2 -o "$WORK/input_ring.o"
"$CXX" -std=c++17 -O2   -I"$ROOT/ViroRenderer/extension"   "$ROOT/tests/eskiu/input_ring_backend_test.cpp"   "$WORK/input_ring.o"   -o "$WORK/input_ring_test"
"$WORK/input_ring_test"

#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${TMPDIR:-/tmp}/viro-engine-contract-test"
"${CXX:-c++}" -std=c++17   "$ROOT/ViroRenderer/extensions/VROEngineContract.cpp"   "$ROOT/tests/eskiu/engine_contract_test.cpp"   -I"$ROOT/ViroRenderer/extensions"   -o "$OUT"
"$OUT"

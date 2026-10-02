#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${TMPDIR:-/tmp}/viro-engine-contract-test"
CXX_BIN="${CXX:-c++}"

"$CXX_BIN" -std=c++17 -Wall -Wextra -Werror \
  "$ROOT/ViroRenderer/VROEngineContract.cpp" \
  "$ROOT/tests/engine_contract/engine_contract_test.cpp" \
  -I"$ROOT/ViroRenderer" \
  -o "$OUT"

"$OUT"

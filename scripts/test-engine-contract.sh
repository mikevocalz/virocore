#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-c++}"
CC="${CC:-cc}"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/viro-engine-contract.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT

INCLUDE="-I$ROOT/ViroRenderer/extension"

"$CC" -std=c11 -Wall -Wextra -Werror $INCLUDE   -c "$ROOT/tests/extension/engine_abi_c_compile.c"   -o "$BUILD_DIR/engine_abi_c_compile.o"

"$CXX" -std=c++17 -Wall -Wextra -Werror $INCLUDE   "$ROOT/ViroRenderer/extension/VROEngineContract.cpp"   "$ROOT/tests/extension/engine_contract_test.cpp"   -o "$BUILD_DIR/engine_contract_test"

"$BUILD_DIR/engine_contract_test"

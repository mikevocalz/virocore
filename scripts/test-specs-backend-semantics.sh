#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CC="${CC:-cc}"
CXX="${CXX:-c++}"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/viro-specs-semantics.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT
"$CC" -std=c11 -Wall -Wextra -Werror -I"$ROOT/ViroRenderer/extension" "$ROOT/tests/xr_backend/specs_semantic_contract_c_test.c" -lm -o "$BUILD_DIR/c_test"
"$CXX" -std=c++17 -Wall -Wextra -Werror \
  -Wno-unused-variable -Wno-unknown-pragmas \
  -DWASM_PLATFORM \
  -I"$ROOT/ViroRenderer/extension" -I"$ROOT/ViroRenderer" \
  "$ROOT/ViroRenderer/VROMath.cpp" \
  "$ROOT/ViroRenderer/VROVector3f.cpp" \
  "$ROOT/ViroRenderer/VROVector4f.cpp" \
  "$ROOT/ViroRenderer/VROMatrix4f.cpp" \
  "$ROOT/ViroRenderer/VROQuaternion.cpp" \
  "$ROOT/tests/xr_backend/specs_semantic_contract_test.cpp" \
  -lm -o "$BUILD_DIR/cpp_test"
"$BUILD_DIR/c_test"
"$BUILD_DIR/cpp_test"

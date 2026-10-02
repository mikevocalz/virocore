#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CC="${CC:-cc}"
CXX="${CXX:-c++}"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/viro-spatial.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT
INCLUDE="-I$ROOT/ViroRenderer/extension"

"$CC" -std=c11 -Wall -Wextra -Werror $INCLUDE   -c "$ROOT/tests/extension/engine_spatial_c_compile.c"   -o "$BUILD_DIR/c_probe.o"

"$CXX" -std=c++17 -Wall -Wextra -Werror $INCLUDE   "$ROOT/ViroRenderer/extension/VROEngineSpatialABI.cpp"   "$ROOT/tests/extension/engine_spatial_test.cpp"   "$BUILD_DIR/c_probe.o"   -o "$BUILD_DIR/spatial_test"

"$BUILD_DIR/spatial_test"

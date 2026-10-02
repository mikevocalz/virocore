#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CC="${CC:-cc}"
CXX="${CXX:-c++}"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/viro-surface.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT
INCLUDE="-I$ROOT/ViroRenderer/extension"

"$CC" -std=c11 -Wall -Wextra -Werror $INCLUDE   -c "$ROOT/tests/extension/engine_surface_c_compile.c"   -o "$BUILD_DIR/c_probe.o"

"$CXX" -std=c++17 -Wall -Wextra -Werror $INCLUDE   "$ROOT/ViroRenderer/extension/VROEngineSurfaceABI.cpp"   "$ROOT/tests/extension/engine_surface_test.cpp"   "$BUILD_DIR/c_probe.o"   -o "$BUILD_DIR/surface_test"

"$BUILD_DIR/surface_test"

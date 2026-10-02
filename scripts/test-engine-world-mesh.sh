#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CC="${CC:-cc}"
CXX="${CXX:-c++}"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/viro-world-mesh.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT
INCLUDE="-I$ROOT/ViroRenderer/extension"

"$CC" -std=c11 -Wall -Wextra -Werror $INCLUDE   -c "$ROOT/tests/extension/engine_world_mesh_c_compile.c"   -o "$BUILD_DIR/c_probe.o"

"$CXX" -std=c++17 -Wall -Wextra -Werror $INCLUDE   "$ROOT/ViroRenderer/extension/VROEngineGeometryABI.cpp"   "$ROOT/ViroRenderer/extension/VROEngineWorldMeshABI.cpp"   "$ROOT/tests/extension/engine_world_mesh_test.cpp"   "$BUILD_DIR/c_probe.o"   -o "$BUILD_DIR/world_mesh_test"

"$BUILD_DIR/world_mesh_test"

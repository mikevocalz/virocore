#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ESKIUC="${ESKIUC:-eskiuc}"; CXX="${CXX:-c++}"
BUILD="$(mktemp -d "${TMPDIR:-/tmp}/viro-eskiu-surface.XXXXXX")"; trap 'rm -rf "$BUILD"' EXIT
"$ESKIUC" "$ROOT/experimental/eskiu/VROEngineSurfaceEskiu.esk" -c -O2 -o "$BUILD/surface.o"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -I"$ROOT/ViroRenderer/extension"  "$ROOT/ViroRenderer/extension/VROEngineSurfaceABI.cpp"  "$ROOT/tests/eskiu/surface_differential.cpp" "$BUILD/surface.o" -o "$BUILD/test"
"$BUILD/test"

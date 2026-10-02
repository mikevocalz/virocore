#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ESKIUC="${ESKIUC:-eskiuc}"
CXX="${CXX:-c++}"
EXPECTED_VERSION="${ESKIU_EXPECTED_VERSION:-0.9.2}"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/viro-eskiu-geometry.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT

version="$("$ESKIUC" --version 2>&1)"
actual="$(printf '%s\n' "$version" | sed -nE 's/^Eskiu[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+)([[:space:]].*)?$/\1/p' | head -n 1)"
if [[ "$actual" != "$EXPECTED_VERSION" ]]; then
  echo "error: expected Eskiu $EXPECTED_VERSION, got: $version" >&2
  exit 2
fi

"$ESKIUC"   "$ROOT/experimental/eskiu/VROEngineGeometryEskiu.esk"   -c -O2 -o "$BUILD_DIR/geometry_eskiu.o"

"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror   -I"$ROOT/ViroRenderer/extension"   "$ROOT/ViroRenderer/extension/VROEngineGeometryABI.cpp"   "$ROOT/tests/eskiu/geometry_differential.cpp"   "$BUILD_DIR/geometry_eskiu.o"   -o "$BUILD_DIR/geometry_differential"

"$BUILD_DIR/geometry_differential"

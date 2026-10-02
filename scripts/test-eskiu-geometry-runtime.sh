#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/viro-eskiu-geometry-runtime.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

ESKIUC="${ESKIUC:-eskiuc}"
CXX="${CXX:-c++}"
EXPECTED_VERSION="${ESKIU_EXPECTED_VERSION:-0.9.2}"

version="$("$ESKIUC" --version 2>&1)"
actual="$(printf '%s\n' "$version" | sed -nE 's/^Eskiu[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+)([[:space:]].*)?$/\1/p' | head -n 1)"
if [[ "$actual" != "$EXPECTED_VERSION" ]]; then
  echo "error: expected Eskiu $EXPECTED_VERSION, got: $version" >&2
  exit 2
fi

"$ESKIUC" \
  "$ROOT/experimental/eskiu/VROEngineGeometryEskiu.esk" \
  -c -O2 -o "$WORK/geometry_eskiu.o"

"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror \
  -DVRO_ENGINE_ESKIU_GEOMETRY_AVAILABLE=1 \
  -I"$ROOT/ViroRenderer/extension" \
  "$ROOT/ViroRenderer/extension/VROEngineGeometryABI.cpp" \
  "$ROOT/ViroRenderer/extension/VROEngineBackendSelector.cpp" \
  "$ROOT/ViroRenderer/extension/VROEngineGeometryBackend.cpp" \
  "$ROOT/tests/eskiu/geometry_runtime_backend_test.cpp" \
  "$WORK/geometry_eskiu.o" \
  -o "$WORK/geometry_runtime_test"

"$WORK/geometry_runtime_test"

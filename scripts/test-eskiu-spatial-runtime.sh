#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/viro-eskiu-spatial-runtime.XXXXXX")"
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

"$ESKIUC" "$ROOT/experimental/eskiu/VROEngineSpatialEskiu.esk" \
  -c -O2 -o "$WORK/spatial.o"

"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror \
  -DVRO_ENGINE_ESKIU_SPATIAL_AVAILABLE=1 \
  -I"$ROOT/ViroRenderer/extension" \
  "$ROOT/ViroRenderer/extension/VROEngineBackendSelector.cpp" \
  "$ROOT/ViroRenderer/extension/VROEngineSpatialABI.cpp" \
  "$ROOT/ViroRenderer/extension/VROEngineSpatialBackend.cpp" \
  "$ROOT/tests/eskiu/spatial_runtime_backend_test.cpp" \
  "$WORK/spatial.o" -lm \
  -o "$WORK/spatial_runtime_test"

"$WORK/spatial_runtime_test"

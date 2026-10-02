#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-c++}"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/viro-input-shadow.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT

"$CXX" -std=c++17 -Wall -Wextra -Werror \
  -I"$ROOT/ViroRenderer" \
  -I"$ROOT/ViroRenderer/extension" \
  "$ROOT/ViroRenderer/extension/VROEngineInputABI.cpp" \
  "$ROOT/ViroRenderer/extension/VROEngineInputShadow.cpp" \
  "$ROOT/tests/extension/engine_input_shadow_test.cpp" \
  -o "$BUILD_DIR/engine_input_shadow_test"

"$BUILD_DIR/engine_input_shadow_test"

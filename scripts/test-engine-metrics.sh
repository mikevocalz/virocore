#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-c++}"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/viro-engine-metrics.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT

INCLUDE="-I$ROOT/ViroRenderer/extension"

"$CXX" -std=c++17 -Wall -Wextra -Werror -DVRO_ENGINE_METRICS_ENABLED=1 $INCLUDE \
  "$ROOT/ViroRenderer/extension/VROEngineMetrics.cpp" \
  "$ROOT/tests/extension/engine_metrics_test.cpp" \
  -o "$BUILD_DIR/enabled"

"$BUILD_DIR/enabled"

"$CXX" -std=c++17 -Wall -Wextra -Werror $INCLUDE \
  "$ROOT/ViroRenderer/extension/VROEngineMetrics.cpp" \
  "$ROOT/tests/extension/engine_metrics_disabled_test.cpp" \
  -o "$BUILD_DIR/disabled"

"$BUILD_DIR/disabled"

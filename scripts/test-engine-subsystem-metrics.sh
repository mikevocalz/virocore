#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CC="${CC:-cc}"
CXX="${CXX:-c++}"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/viro-subsystem-metrics.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT
INCLUDE="-I$ROOT/ViroRenderer/extension"

"$CC" -std=c11 -Wall -Wextra -Werror $INCLUDE   -c "$ROOT/tests/extension/engine_subsystem_metrics_c_compile.c"   -o "$BUILD_DIR/c_probe.o"

"$CXX" -std=c++17 -Wall -Wextra -Werror   -DVRO_ENGINE_METRICS_ENABLED=1 $INCLUDE   "$ROOT/ViroRenderer/extension/VROEngineSubsystemMetrics.cpp"   "$ROOT/tests/extension/engine_subsystem_metrics_test.cpp"   "$BUILD_DIR/c_probe.o"   -o "$BUILD_DIR/subsystem_metrics_test"

"$BUILD_DIR/subsystem_metrics_test"

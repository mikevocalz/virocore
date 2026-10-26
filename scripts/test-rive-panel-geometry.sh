#!/usr/bin/env bash
# Host unit test for VRORivePanelGeometry (fit/alignment, rounded corners,
# pointer routing). Links rive-runtime's own src/renderer.cpp and src/math so
# the alignment transform is checked against rive::computeAlignment.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-c++}"
RIVE="$ROOT/ViroRenderer/rive-runtime"
SRC="$ROOT/android/sharedCode/src/main/cpp"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/viro-rive-panel.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT

# Same language flags as the Android archives (C++17, no RTTI, no exceptions).
# rive sources are third-party: built without -Werror.
"$CXX" -std=c++17 -fno-rtti -fno-exceptions -w -I"$RIVE/include" \
    -c "$RIVE/src/renderer.cpp" -o "$BUILD_DIR/renderer.o"
MATH_OBJS=()
for f in "$RIVE"/src/math/*.cpp; do
    o="$BUILD_DIR/math_$(basename "$f" .cpp).o"
    "$CXX" -std=c++17 -fno-rtti -fno-exceptions -w -I"$RIVE/include" -c "$f" -o "$o"
    MATH_OBJS+=("$o")
done
"$CXX" -std=c++17 -fno-rtti -fno-exceptions -Wall -Wextra -Werror \
    -I"$SRC" -I"$ROOT/ViroRenderer" -I"$ROOT/tests/host_shims" -isystem "$RIVE/include" \
    "$SRC/VRORivePanelGeometry.cpp" \
    "$ROOT/tests/rive/rive_panel_geometry_test.cpp" \
    "$BUILD_DIR/renderer.o" "${MATH_OBJS[@]}" \
    -o "$BUILD_DIR/panel_geometry_test"
"$BUILD_DIR/panel_geometry_test"

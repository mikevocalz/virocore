#!/usr/bin/env bash
# Host test for VRORivePanel's worker thread: dispose() from the status
# listener, alone and while another thread joins. Builds the real
# VRORivePanel.cpp with fakes for its EGL, ring and scene halves
# (tests/rive/rive_panel_worker_test.cpp) under ThreadSanitizer.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-c++}"
SRC="$ROOT/android/sharedCode/src/main/cpp"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/viro-rive-worker.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT

# VRORivePanel.cpp is built with exceptions on Android; so is this test.
"$CXX" -std=c++17 -O1 -g -fsanitize=thread -pthread -Wall -Wextra -Werror \
    -DVRO_HAS_RIVE_RUNTIME=1 \
    -I"$ROOT/tests/host_shims" -I"$SRC" -I"$ROOT/ViroRenderer" \
    "$SRC/VRORivePanel.cpp" \
    "$SRC/VRORivePanelGeometry.cpp" \
    "$ROOT/tests/rive/rive_panel_worker_test.cpp" \
    -o "$BUILD_DIR/rive_panel_worker_test"
TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1}" "$BUILD_DIR/rive_panel_worker_test"

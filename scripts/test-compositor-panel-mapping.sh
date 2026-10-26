#!/usr/bin/env bash
# Host test for the JNI codes of com.viro.core.RiveCompositorPanel: the real
# mapping header, the vendored OpenXR headers and the EGL/GLES/JNI type shims.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-c++}"
SRC="$ROOT/android/sharedCode/src/main/cpp"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/viro-panel-mapping.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT

"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -Wno-missing-field-initializers \
    -I"$ROOT/tests/host_shims" -I"$SRC" -isystem "$SRC/include" \
    "$ROOT/tests/xr_backend/compositor_panel_mapping_test.cpp" \
    -o "$BUILD_DIR/compositor_panel_mapping_test"
"$BUILD_DIR/compositor_panel_mapping_test"

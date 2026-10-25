#!/usr/bin/env bash
# Host test for VROOpenXRPanelCompositor's eviction reporting: the real
# compositor source, the vendored OpenXR headers, a fake runtime and minimal
# EGL/GLES/JNI type shims.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-c++}"
SRC="$ROOT/android/sharedCode/src/main/cpp"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/viro-panel-eviction.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT

# -Wno-missing-field-initializers: the OpenXR `{ XR_TYPE_... }` idiom zero-fills the rest.
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -Wno-missing-field-initializers \
    -I"$ROOT/tests/xr_backend/host_shims" -I"$SRC" -isystem "$SRC/include" \
    "$SRC/VROOpenXRPanelCompositor.cpp" \
    "$ROOT/tests/xr_backend/panel_eviction_test.cpp" \
    -o "$BUILD_DIR/panel_eviction_test"
"$BUILD_DIR/panel_eviction_test"

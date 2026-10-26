#!/usr/bin/env bash
# Host test for VROOpenXRRenderQuality.h: eye-buffer scale resolution and the
# foveation code mapping. The header has no GL, EGL or OpenXR dependency.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-c++}"
SRC="$ROOT/android/sharedCode/src/main/cpp"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/viro-eye-buffer-scale.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT

"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror ${CXXFLAGS:-} \
    -I"$SRC" \
    "$ROOT/tests/xr_backend/eye_buffer_scale_test.cpp" \
    -o "$BUILD_DIR/eye_buffer_scale_test"
"$BUILD_DIR/eye_buffer_scale_test"

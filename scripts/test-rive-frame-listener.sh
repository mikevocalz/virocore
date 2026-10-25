#!/usr/bin/env bash
# Host test for the Rive compositor panel frame listener
# (VROOpenXRRiveFrameListener.h): coalescing, and no reference cycle through
# the scene fallback. Built with AddressSanitizer so a task that touched a
# freed link would be reported.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-c++}"
SRC="$ROOT/android/sharedCode/src/main/cpp"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/viro-rive-frame-listener.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT

"$CXX" -std=c++17 -O1 -g -fsanitize=address -Wall -Wextra -Werror -I"$SRC" \
    "$ROOT/tests/xr_backend/rive_frame_listener_test.cpp" \
    -o "$BUILD_DIR/rive_frame_listener_test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" "$BUILD_DIR/rive_frame_listener_test"

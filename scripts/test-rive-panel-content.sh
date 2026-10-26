#!/usr/bin/env bash
# Host test for VRORivePanelContent teardown order, under AddressSanitizer.
# Builds the rive-runtime core (src/ without the GPU renderer, Lua and WASM)
# with rive's NoOpFactory, then replaces live content loaded from
# tests/rive/fixtures/panel-content.riv. Text and layout stay off here; the
# Android archives turn them on, which changes no code this test reaches.
#
# RIVE_HOST_OBJ_DIR, when set, keeps the rive core objects between runs and
# reuses any already there. The caller owns invalidation: CI keys that
# directory on the rive sources, this script and the compiler version.
#
# The fixture is built from tests/rive/fixtures/panel-content/scene.rml with
# the Rive CLI: rive tests/rive/fixtures/panel-content --once, then move
# build/panel-content.riv next to the folder. CI only reads the committed file.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-c++}"
RIVE="$ROOT/ViroRenderer/rive-runtime"
SRC="$ROOT/android/sharedCode/src/main/cpp"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/viro-rive-content.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

# Same language flags as the Android archives (C++17, no RTTI, no exceptions).
FLAGS=(-std=c++17 -fno-rtti -fno-exceptions -O1 -g -fsanitize=address -fno-omit-frame-pointer)
RIVE_FLAGS=("${FLAGS[@]}" -w -D_RIVE_INTERNAL_ -DYOGA_EXPORT= -I"$RIVE/include" -I"$RIVE")
RIVE_OBJ_DIR="${RIVE_HOST_OBJ_DIR:-$BUILD_DIR/rive}"
mkdir -p "$RIVE_OBJ_DIR"
export CXX RIVE_OBJ_DIR
export RIVE_FLAGS_STR="${RIVE_FLAGS[*]}"

# rive sources are third-party: built without -Werror. Each object is written
# to a temporary name and renamed, so an interrupted run leaves no partial
# object for the next run to reuse.
(cd "$RIVE" && find src -name '*.cpp' -not -path 'src/renderer/*' -not -path 'src/lua/*' \
        -not -path 'src/wasm/*' -print; echo utils/no_op_factory.cpp) |
    (cd "$RIVE" && xargs -P "$JOBS" -I{} sh -c \
        'o="$RIVE_OBJ_DIR/$(echo "$1" | tr / _).o"; [ -f "$o" ] && exit 0; $CXX $RIVE_FLAGS_STR -c "$1" -o "$o.tmp.$$" && mv "$o.tmp.$$" "$o" || { rm -f "$o.tmp.$$"; echo "error: $1" >&2; exit 255; }' \
        _ {})

"$CXX" "${FLAGS[@]}" -Wall -Wextra -Werror -D_RIVE_INTERNAL_ -DYOGA_EXPORT= \
    -I"$SRC" -isystem "$RIVE/include" -isystem "$RIVE" \
    -c "$SRC/VRORivePanelContent.cpp" -o "$BUILD_DIR/content.o"
"$CXX" "${FLAGS[@]}" -Wall -Wextra -Werror -D_RIVE_INTERNAL_ -DYOGA_EXPORT= \
    -I"$SRC" -isystem "$RIVE/include" -isystem "$RIVE" \
    -c "$ROOT/tests/rive/rive_panel_content_test.cpp" -o "$BUILD_DIR/test.o"
"$CXX" -fsanitize=address "$RIVE_OBJ_DIR"/*.o "$BUILD_DIR/content.o" "$BUILD_DIR/test.o" \
    -o "$BUILD_DIR/panel_content_test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:abort_on_error=0}" \
    "$BUILD_DIR/panel_content_test" "$ROOT/tests/rive/fixtures/panel-content.riv"

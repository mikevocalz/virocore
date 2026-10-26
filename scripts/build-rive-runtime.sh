#!/bin/bash
# Build the rive-runtime static libraries that libviro_renderer.so links (VIRO_WITH_RIVE).
#
#   scripts/build-rive-runtime.sh          # arm64-v8a release, the only ABI ViroCore links
#   scripts/build-rive-runtime.sh clean    # wipe android/rive/out first
#
# Uses rive-runtime's own build/build_rive.sh (premake + ninja) with the NDK that
# android/viroreact/build.gradle pins. Module choices are explained in docs/RIVE_RUNTIME.md.
# Needs network access on the first run: build_rive.sh clones premake, premake-ninja, and the
# harfbuzz / SheenBidi / yoga / miniaudio / ply sources at the tags rive-runtime pins.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RIVE_RUNTIME="$REPO_ROOT/ViroRenderer/rive-runtime"
WORK_DIR="$REPO_ROOT/android/rive"
OUT_REL="out/android_arm64_release"
RIVE_COMMIT="$(tr -d '[:space:]' < "$WORK_DIR/RIVE_RUNTIME_COMMIT")"

NDK_VERSION="$(sed -n 's/^[[:space:]]*ndkVersion[[:space:]]*"\([^"]*\)".*/\1/p' \
    "$REPO_ROOT/android/viroreact/build.gradle")"
if [[ -z "$NDK_VERSION" ]]; then
    echo "error: no ndkVersion in android/viroreact/build.gradle" >&2
    exit 1
fi

SDK_DIR="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
if [[ -z "$SDK_DIR" && -f "$REPO_ROOT/android/local.properties" ]]; then
    SDK_DIR="$(sed -n 's/^sdk\.dir=//p' "$REPO_ROOT/android/local.properties")"
fi
SDK_DIR="${SDK_DIR:-$HOME/Library/Android/sdk}"
NDK_DIR="$SDK_DIR/ndk/$NDK_VERSION"
if [[ ! -f "$NDK_DIR/source.properties" ]]; then
    echo "error: NDK $NDK_VERSION not found at $NDK_DIR" >&2
    exit 1
fi

export ANDROID_NDK="$NDK_DIR"
# rive-runtime pins NDK r27c. Accept the NDK ViroCore builds with instead, so the archives and
# libviro_renderer.so come from one toolchain (local change in rive_build_config.lua).
export RIVE_NDK_REVISION_OVERRIDE="$NDK_VERSION"
# text and layout on, audio, canvas (ORE), scripting and the image decoders off. Layout is on
# because text without layout does not compile at the pinned commit (text_input.cpp calls
# LayoutComponent::hugsContentWidth, declared only under WITH_RIVE_LAYOUT). See
# docs/RIVE_RUNTIME.md for the evidence behind each choice.
export RIVE_PREMAKE_ARGS="--with_rive_text --with_rive_layout --no-rive-decoders --android_api=24"

CLEAN=()
if [[ "${1:-}" == "clean" ]]; then
    CLEAN=(clean)
fi

cd "$WORK_DIR"
"$RIVE_RUNTIME/build/build_rive.sh" android arm64 release ${CLEAN[@]+"${CLEAN[@]}"} -- \
    rive rive_pls_renderer rive_harfbuzz rive_sheenbidi rive_yoga

for lib in librive.a librive_pls_renderer.a librive_harfbuzz.a librive_sheenbidi.a \
    librive_yoga.a; do
    if [[ ! -f "$WORK_DIR/$OUT_REL/$lib" ]]; then
        echo "error: $OUT_REL/$lib was not produced" >&2
        exit 1
    fi
done
# android/rive/ViroRive.cmake rebuilds (with "clean") when this stamp does not match.
printf '%s %s\n' "$RIVE_COMMIT" "$RIVE_PREMAKE_ARGS" > "$WORK_DIR/$OUT_REL/viro_rive.stamp"
echo "rive-runtime $RIVE_COMMIT archives: $WORK_DIR/$OUT_REL"

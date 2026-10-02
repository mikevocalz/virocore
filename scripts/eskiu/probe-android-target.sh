#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ESKIUC_BIN="${ESKIUC:-$(command -v eskiuc || true)}"
NDK_ROOT="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
API="${NDK_API:-26}"
WORK="${TMPDIR:-/tmp}/viro-eskiu-android-probe"

if [[ -z "$ESKIUC_BIN" ]]; then
  echo "error: eskiuc not found; set ESKIUC=/path/to/eskiuc" >&2
  exit 2
fi
if [[ -z "$NDK_ROOT" || ! -d "$NDK_ROOT" ]]; then
  echo "error: ANDROID_NDK_HOME/ANDROID_NDK_ROOT is not set to an NDK" >&2
  exit 2
fi

rm -rf "$WORK"
mkdir -p "$WORK"

echo "== Eskiu version =="
"$ESKIUC_BIN" --version

echo "== Stage A: emit Android AArch64 object =="
"$ESKIUC_BIN" "$ROOT/tests/eskiu/android_target_probe.esk"   -c -o "$WORK/android_target_probe.o"   --target aarch64-linux-android

PREBUILT_DIR="$(find "$NDK_ROOT/toolchains/llvm/prebuilt" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
if [[ -z "$PREBUILT_DIR" ]]; then
  echo "error: unable to locate NDK LLVM prebuilt toolchain" >&2
  exit 3
fi

READELF="$PREBUILT_DIR/bin/llvm-readelf"
CLANG="$PREBUILT_DIR/bin/aarch64-linux-android${API}-clang"

"$READELF" -h "$WORK/android_target_probe.o"

echo "== Stage B: link JNI-loadable shared library with NDK clang =="
"$CLANG" -shared "$WORK/android_target_probe.o" -o "$WORK/libviro_eskiu_probe.so"
"$READELF" -h "$WORK/libviro_eskiu_probe.so"
"$READELF" -d "$WORK/libviro_eskiu_probe.so"

echo "== Stage C: inspect exported symbol =="
"$PREBUILT_DIR/bin/llvm-nm" -g --defined-only "$WORK/libviro_eskiu_probe.so"   | grep "viro_eskiu_android_probe"

echo "Eskiu Android target probe: PASS"

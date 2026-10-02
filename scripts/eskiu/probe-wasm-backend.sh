#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LLVM_DIR="${LLVM_DIR:-/usr/lib/llvm-22/lib/cmake/llvm}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/viro-eskiu-wasm.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

ESKIU_REPO="${ESKIU_REPO:-https://github.com/doranteseduardo/eskiu.git}"
ESKIU_COMMIT="${ESKIU_COMMIT:-95d7ece7ea8d72bc106a141bc031a1648ed8f2ac}"

git clone --quiet "$ESKIU_REPO" "$WORK/eskiu"
git -C "$WORK/eskiu" checkout --quiet "$ESKIU_COMMIT"
git -C "$WORK/eskiu" apply "$ROOT/tools/eskiu/patches/0001-enable-webassembly-backend.patch"

cmake -S "$WORK/eskiu" -B "$WORK/build"   -G Ninja   -DLLVM_DIR="$LLVM_DIR"   -DCMAKE_BUILD_TYPE=Release
cmake --build "$WORK/build" --target eskiuc -j2

"$WORK/build/eskiuc" "$ROOT/tests/eskiu/wasm_target_probe.esk"   -c -o "$WORK/viro_eskiu_wasm_probe.o"   --target wasm32-unknown-unknown   --reloc static

description="$(file "$WORK/viro_eskiu_wasm_probe.o")"
echo "$description"
if [[ "$description" != *"WebAssembly"* ]]; then
  echo "error: patched Eskiu compiler did not emit a WebAssembly object" >&2
  exit 3
fi

if command -v wasm-ld-22 >/dev/null 2>&1; then
  wasm-ld-22 --no-entry --export-all     "$WORK/viro_eskiu_wasm_probe.o"     -o "$WORK/viro_eskiu_wasm_probe.wasm"
elif command -v wasm-ld >/dev/null 2>&1; then
  wasm-ld --no-entry --export-all     "$WORK/viro_eskiu_wasm_probe.o"     -o "$WORK/viro_eskiu_wasm_probe.wasm"
else
  echo "error: wasm-ld not found" >&2
  exit 4
fi

file "$WORK/viro_eskiu_wasm_probe.wasm"
echo "Patched Eskiu WebAssembly backend probe: PASS"

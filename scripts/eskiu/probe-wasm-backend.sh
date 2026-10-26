#!/usr/bin/env bash
set -euo pipefail

# Since Eskiu 0.13.0 the release compiler registers LLVM's WebAssembly backend,
# so this probe uses the pinned eskiuc instead of a patched source build.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ESKIUC_BIN="${ESKIUC:-$(command -v eskiuc || true)}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/viro-eskiu-wasm.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

if [[ -z "$ESKIUC_BIN" ]]; then
  echo "error: eskiuc not found; set ESKIUC=/path/to/eskiuc" >&2
  exit 2
fi

"$ESKIUC_BIN" --version

"$ESKIUC_BIN" "$ROOT/tests/eskiu/wasm_target_probe.esk"   -c -o "$WORK/viro_eskiu_wasm_probe.o"   --target wasm32-unknown-unknown   --reloc static

description="$(file "$WORK/viro_eskiu_wasm_probe.o")"
echo "$description"
if [[ "$description" != *"WebAssembly"* ]]; then
  echo "error: Eskiu compiler did not emit a WebAssembly object" >&2
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
echo "Eskiu WebAssembly backend probe: PASS"

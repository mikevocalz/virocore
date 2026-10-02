#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ESKIUC="${ESKIUC:-eskiuc}"
CXX="${CXX:-c++}"
EXPECTED_VERSION="${ESKIU_EXPECTED_VERSION:-0.9.2}"

if ! command -v "$ESKIUC" >/dev/null 2>&1; then
  echo "error: eskiuc was not found. Install Eskiu v$EXPECTED_VERSION or set ESKIUC." >&2
  exit 2
fi

VERSION="$("$ESKIUC" --version 2>&1 || true)"
ACTUAL_VERSION="$(printf '%s\n' "$VERSION" | sed -nE 's/^Eskiu[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+)([[:space:]].*)?$/\1/p' | head -n 1)"
if [[ -z "$ACTUAL_VERSION" || "$ACTUAL_VERSION" != "$EXPECTED_VERSION" ]]; then
  echo "error: expected exactly Eskiu $EXPECTED_VERSION, got: $VERSION" >&2
  exit 3
fi

BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/viro-eskiu-abi.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT

"$ESKIUC" "$ROOT/tests/eskiu/abi_probe.esk" -c -o "$BUILD_DIR/abi_probe.o"
"$CXX" -std=c++17 "$ROOT/tests/eskiu/abi_probe.cpp" "$BUILD_DIR/abi_probe.o" -o "$BUILD_DIR/abi_probe"
"$BUILD_DIR/abi_probe"

#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORK="${TMPDIR:-/tmp}/viro-eskiu-host-abi"
ESKIUC_BIN="${ESKIUC:-$(command -v eskiuc || true)}"
CXX_BIN="${CXX:-c++}"

if [[ -z "$ESKIUC_BIN" ]]; then
  echo "eskiuc not found. Install/pin Eskiu v0.9.2 or set ESKIUC=/path/to/eskiuc." >&2
  exit 2
fi

rm -rf "$WORK"
mkdir -p "$WORK"

"$ESKIUC_BIN" "$ROOT/tests/eskiu/host_abi_probe.esk" -c -o "$WORK/host_abi_probe.o"
"$CXX_BIN" -std=c++17 "$ROOT/tests/eskiu/host_abi_probe.cpp" "$WORK/host_abi_probe.o" -o "$WORK/host_abi_probe"
"$WORK/host_abi_probe"

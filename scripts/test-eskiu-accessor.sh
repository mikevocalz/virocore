#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ESKIUC="${ESKIUC:-eskiuc}"; CXX="${CXX:-c++}"
B="$(mktemp -d "${TMPDIR:-/tmp}/viro-eskiu-accessor.XXXXXX")"; trap 'rm -rf "$B"' EXIT
"$ESKIUC" "$ROOT/experimental/eskiu/VROEngineAccessorEskiu.esk" -c -O2 -o "$B/accessor.o"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -I"$ROOT/ViroRenderer/extension"  "$ROOT/ViroRenderer/extension/VROEngineAccessorABI.cpp"  "$ROOT/tests/eskiu/accessor_differential.cpp" "$B/accessor.o" -o "$B/test"
"$B/test"

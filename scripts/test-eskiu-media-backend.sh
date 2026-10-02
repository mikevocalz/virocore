#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; ESKIUC="${ESKIUC:-eskiuc}"; CXX="${CXX:-c++}"
B="$(mktemp -d "${TMPDIR:-/tmp}/viro-eskiu-media.XXXXXX")"; trap 'rm -rf "$B"' EXIT
"$ESKIUC" "$ROOT/experimental/eskiu/VROEngineMediaEskiu.esk" -c -O2 -o "$B/media.o"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -I"$ROOT/ViroRenderer/extension"  "$ROOT/ViroRenderer/extension/VROEngineSurfaceABI.cpp" "$ROOT/ViroRenderer/extension/VROEngineMediaABI.cpp"  "$ROOT/tests/eskiu/media_differential.cpp" "$B/media.o" -o "$B/test"
"$B/test"

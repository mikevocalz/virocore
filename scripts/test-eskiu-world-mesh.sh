#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; ESKIUC="${ESKIUC:-eskiuc}"; CXX="${CXX:-c++}"
B="$(mktemp -d "${TMPDIR:-/tmp}/viro-eskiu-world.XXXXXX")"; trap 'rm -rf "$B"' EXIT
"$ESKIUC" "$ROOT/experimental/eskiu/VROEngineGeometryEskiu.esk" -c -O2 -o "$B/geometry.o"
"$ESKIUC" "$ROOT/experimental/eskiu/VROEngineWorldMeshEskiu.esk" -c -O2 -o "$B/world.o"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -I"$ROOT/ViroRenderer/extension"  "$ROOT/ViroRenderer/extension/VROEngineGeometryABI.cpp" "$ROOT/ViroRenderer/extension/VROEngineWorldMeshABI.cpp"  "$ROOT/tests/eskiu/world_mesh_differential.cpp" "$B/geometry.o" "$B/world.o" -o "$B/test"
"$B/test"

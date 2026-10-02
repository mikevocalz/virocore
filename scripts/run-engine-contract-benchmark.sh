#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-c++}"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/viro-engine-bench.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT
OUT="${1:-$ROOT/engine-benchmark.json}"

"$CXX" -std=c++17 -O2 -DNDEBUG   -I"$ROOT/ViroRenderer/extension"   "$ROOT/ViroRenderer/extension/VROEngineContract.cpp"   "$ROOT/ViroRenderer/extension/VROEngineInputABI.cpp"   "$ROOT/ViroRenderer/extension/VROEngineSpatialABI.cpp"   "$ROOT/ViroRenderer/extension/VROEngineSurfaceABI.cpp"   "$ROOT/ViroRenderer/extension/VROEngineGeometryABI.cpp"   "$ROOT/tests/benchmark/engine_contract_benchmark.cpp"   -o "$BUILD_DIR/engine_benchmark"

"$BUILD_DIR/engine_benchmark" --backend=cpp > "$OUT"
python3 -m json.tool "$OUT" >/dev/null
cat "$OUT"

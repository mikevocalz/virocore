#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
B="$(mktemp -d "${TMPDIR:-/tmp}/viro-backend-selector.XXXXXX")"
trap 'rm -rf "$B"' EXIT
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror   -I"$ROOT/ViroRenderer/extension"   "$ROOT/ViroRenderer/extension/VROEngineBackendSelector.cpp"   "$ROOT/tests/extension/engine_backend_selector_test.cpp"   -o "$B/test"
"$B/test"

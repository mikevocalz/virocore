#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
B="$(mktemp -d "${TMPDIR:-/tmp}/viro-accessor-abi.XXXXXX")"
trap 'rm -rf "$B"' EXIT
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror   -I"$ROOT/ViroRenderer/extension"   "$ROOT/ViroRenderer/extension/VROEngineAccessorABI.cpp"   "$ROOT/tests/extension/engine_accessor_test.cpp"   -o "$B/test"
"$B/test"

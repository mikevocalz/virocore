#!/usr/bin/env bash
# Host unit test for ViroRenderer/VROPanelCoverage.h, the shared rounded-rect
# coverage function. Builds and runs tests/panel/panel_coverage_test.cpp, then
# compiles the GLSL string with the NDK's glslc (when an NDK is installed).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-c++}"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/viro-panel-coverage.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT

# -ffp-contract=off: no fused multiply-add, so the host evaluates the shim's
# binary32 operations one rounding at a time.
"$CXX" -std=c++11 -O2 -ffp-contract=off -Wall -Wextra -Werror \
    -I"$ROOT/ViroRenderer" \
    "$ROOT/tests/panel/panel_coverage_test.cpp" \
    -o "$BUILD_DIR/panel_coverage_test"
"$BUILD_DIR/panel_coverage_test"

SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Library/Android/sdk}}"
GLSLC="$(ls -1 "$SDK"/ndk/*/shader-tools/*/glslc 2>/dev/null | tail -n 1 || true)"
if [[ -z "$GLSLC" ]]; then
    echo "glslc not found under $SDK/ndk; GLSL compile check skipped"
    exit 0
fi

# Print the GLSL string exactly as the renderer embeds it.
cat > "$BUILD_DIR/print_glsl.cpp" <<'EOF'
#include "VROPanelCoverage.h"
#include <cstdio>
int main() { std::fputs(kVROPanelCoverageGLSL, stdout); return 0; }
EOF
"$CXX" -std=c++11 -I"$ROOT/ViroRenderer" "$BUILD_DIR/print_glsl.cpp" -o "$BUILD_DIR/print_glsl"
SNIPPET="$("$BUILD_DIR/print_glsl")"

# glslc's SPIR-V front end takes ES shaders from #version 310 es up, not 100 or
# 300 es. The snippet uses only GLSL ES 1.00 built-ins (abs, max, min, clamp,
# length, vector constructors) and no 3.x syntax, so this checks the same text
# the 1.00 resolve shader and the 3.00 finish pass embed.
{
    echo "#version 310 es"
    echo "precision highp float;"
    echo "$SNIPPET"
    echo "layout(location = 0) uniform vec2 u_size; layout(location = 1) uniform vec2 u_radius;"
    echo "layout(location = 0) out vec4 o_color;"
    echo "void main() { o_color = vec4(vro_panel_coverage(gl_FragCoord.xy, u_size, u_radius)); }"
} > "$BUILD_DIR/coverage310.frag"
"$GLSLC" -fshader-stage=frag --target-env=opengl "$BUILD_DIR/coverage310.frag" \
    -o "$BUILD_DIR/coverage310.spv"
echo "glslc: GLSL snippet compiles ($GLSLC)"

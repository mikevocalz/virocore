#!/usr/bin/env bash
# Compiles every panel shader variant in
# android/sharedCode/src/main/cpp/VROPanelShaderSources.h with the NDK's glslc,
# so a GLSL error shows up here instead of as a black panel on a headset.
#
# glslc's SPIR-V front end accepts ES shaders from #version 310 es up. The
# 3.00 shaders are compiled with only the version line raised. The 1.00
# resolve shader is rewritten mechanically first (samplerExternalOES ->
# sampler2D, attribute/varying -> in/out, texture2D -> texture, gl_FragColor
# -> an out variable); everything else in it, including the shared coverage
# snippet, is compiled as written.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-c++}"
SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Library/Android/sdk}}"
GLSLC="$(ls -1 "$SDK"/ndk/*/shader-tools/*/glslc 2>/dev/null | tail -n 1 || true)"
if [[ -z "$GLSLC" ]]; then
    echo "glslc not found under $SDK/ndk" >&2
    exit 1
fi
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/viro-panel-shaders.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT

cat > "$BUILD_DIR/dump.cpp" <<'EOF'
#include "VROPanelShaderSources.h"
#include <cstdio>
#include <string>
static void write(const char *path, const std::string &text) {
    FILE *f = std::fopen(path, "w");
    std::fputs(text.c_str(), f);
    std::fclose(f);
}
int main(int, char **argv) {
    std::string dir = argv[1];
    write((dir + "/resolve.vert").c_str(), kVROPanelResolveVertexShader);
    write((dir + "/resolve_srgb.frag").c_str(), VROPanelResolveFragmentShader(true));
    write((dir + "/resolve_rgba8.frag").c_str(), VROPanelResolveFragmentShader(false));
    write((dir + "/copy.vert").c_str(), kVROPanelCopyVertexShader);
    write((dir + "/copy_linear.frag").c_str(), VROPanelCopyFragmentShader(true));
    write((dir + "/copy_encoded.frag").c_str(), VROPanelCopyFragmentShader(false));
    return 0;
}
EOF
"$CXX" -std=c++11 -Wall -Wextra -Werror -I"$ROOT/ViroRenderer" \
    -I"$ROOT/android/sharedCode/src/main/cpp" "$BUILD_DIR/dump.cpp" -o "$BUILD_DIR/dump"
mkdir -p "$BUILD_DIR/src" "$BUILD_DIR/es31"
"$BUILD_DIR/dump" "$BUILD_DIR/src"

python3 - "$BUILD_DIR/src" "$BUILD_DIR/es31" <<'EOF'
import os, re, sys
src, dst = sys.argv[1], sys.argv[2]
for name in sorted(os.listdir(src)):
    text = open(os.path.join(src, name)).read()
    stage = 'vert' if name.endswith('.vert') else 'frag'
    if text.startswith('#version 100\n'):
        text = text.replace('#version 100\n', '#version 310 es\n', 1)
        text = text.replace('#extension GL_OES_EGL_image_external : require\n', '')
        text = text.replace('samplerExternalOES', 'sampler2D')
        text = re.sub(r'\btexture2D\b', 'texture', text)
        if stage == 'vert':
            text = re.sub(r'\battribute\b', 'in', text)
            text = re.sub(r'\bvarying\b', 'out', text)
        else:
            text = re.sub(r'\bvarying\b', 'in', text)
            text = re.sub(r'\bgl_FragColor\b', 'o_color', text)
            text = text.replace('void main() {', 'out vec4 o_color;\nvoid main() {', 1)
    elif text.startswith('#version 300 es\n'):
        text = text.replace('#version 300 es\n', '#version 310 es\n', 1)
    else:
        sys.exit('unexpected version line in ' + name)
    open(os.path.join(dst, name), 'w').write(text)
EOF

for f in "$BUILD_DIR"/es31/*; do
    name="$(basename "$f")"
    stage="${name##*.}"
    "$GLSLC" -fshader-stage="$stage" --target-env=opengl -fauto-map-locations \
        -fauto-bind-uniforms "$f" -o "$f.spv"
    echo "ok  $name"
done
echo "panel shaders: all variants compile ($GLSLC)"
